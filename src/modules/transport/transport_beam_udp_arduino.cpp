#include "transport.h"
#include "../logging/log_buffer.h"

#include <algorithm>
#include <cstring>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#if __has_include(<WiFi.h>) && __has_include(<WiFiUdp.h>)
#include <WiFi.h>
#include <WiFiUdp.h>
#define TRANSPORT_HAS_WIFIUDP 1
#else
#define TRANSPORT_HAS_WIFIUDP 0
#endif

#if defined(TRANSPORT_ENABLE_TINYGSM) && __has_include(<TinyGsmClient.h>)
#include <TinyGsmClient.h>
#define TRANSPORT_HAS_TINYGSM 1
#else
#define TRANSPORT_HAS_TINYGSM 0
#endif

namespace {
struct PendingPacket {
    bool inUse = false;
    TransportPacketKind kind = TransportPacketKind::Telemetry;
    size_t length = 0;
    uint8_t attempts = 0;          // completed attempts
    uint32_t firstQueuedAtMs = 0;
    uint32_t nextSendAtMs = 0;
    char payload[TRANSPORT_MAX_PACKET_BYTES];
};

constexpr uint32_t kMaxBackoffMs = 60000UL;

TransportConfig gConfig = transport_makeDefaultConfig();
TransportStats gStats;
PendingPacket gQueue[TRANSPORT_QUEUE_DEPTH];
size_t gQueueCount = 0;
SemaphoreHandle_t gQueueMutex = nullptr;

#if TRANSPORT_HAS_WIFIUDP
WiFiUDP* gWifiUdp = nullptr;
bool gWifiUdpBegun = false;
#endif

#if TRANSPORT_HAS_TINYGSM
TinyGsm* gTinyGsm = nullptr;
TinyGsmUDP* gTinyGsmUdp = nullptr;
#endif

inline void copyString(char* dest, size_t destSize, const char* src) {
    if (!dest || destSize == 0) {
        return;
    }
    if (!src) {
        dest[0] = '\0';
        return;
    }
    size_t len = strlen(src);
    if (len >= destSize) {
        len = destSize - 1;
    }
    memcpy(dest, src, len);
    dest[len] = '\0';
}

uint32_t computeBackoffMs(uint8_t attemptNumber) {
    // attemptNumber starts at 1 for first retry
    if (attemptNumber == 0) {
        return 0;
    }
    uint32_t factor = 1UL << std::min<uint8_t>(attemptNumber - 1, 5);
    uint32_t base = gConfig.baseRetryDelayMs * factor;
    if (base > kMaxBackoffMs) {
        base = kMaxBackoffMs;
    }
    long jitter = 0;
    if (gConfig.jitterMs > 0) {
        long span = static_cast<long>(gConfig.jitterMs);
        jitter = random(-span, span + 1);
    }
    long result = static_cast<long>(base) + jitter;
    if (result < 0) {
        result = 0;
    }
    return static_cast<uint32_t>(result);
}

PendingPacket* findOldestPacketLocked() {
    PendingPacket* oldest = nullptr;
    for (auto& pkt : gQueue) {
        if (!pkt.inUse) {
            continue;
        }
        if (!oldest || (int32_t)(pkt.firstQueuedAtMs - oldest->firstQueuedAtMs) < 0) {
            oldest = &pkt;
        }
    }
    return oldest;
}

bool enqueueInternal(const PendingPacket& packet, bool isRetry) {
    if (!gQueueMutex) {
        return false;
    }
    if (xSemaphoreTake(gQueueMutex, pdMS_TO_TICKS(5)) != pdTRUE) {
        return false;
    }

    PendingPacket* slot = nullptr;
    for (auto& pkt : gQueue) {
        if (!pkt.inUse) {
            slot = &pkt;
            break;
        }
    }

    if (!slot) {
        PendingPacket* oldest = findOldestPacketLocked();
        if (oldest) {
            logbuf_printf("transport: dropping oldest packet after %lums", static_cast<unsigned long>(millis() - oldest->firstQueuedAtMs));
            gStats.dropped++;
            *oldest = PendingPacket();
            slot = oldest;
            if (gQueueCount > 0) {
                gQueueCount--;
            }
        } else {
            xSemaphoreGive(gQueueMutex);
            return false;
        }
    }

    *slot = packet;
    slot->inUse = true;
    gQueueCount++;
    if (isRetry) {
        gStats.retries++;
    } else {
        gStats.queued++;
    }

    xSemaphoreGive(gQueueMutex);
    return true;
}

bool dequeueReadyPacket(PendingPacket& outPacket) {
    if (!gQueueMutex) {
        return false;
    }
    if (xSemaphoreTake(gQueueMutex, pdMS_TO_TICKS(5)) != pdTRUE) {
        return false;
    }

    uint32_t now = millis();
    PendingPacket* candidate = nullptr;
    for (auto& pkt : gQueue) {
        if (!pkt.inUse) {
            continue;
        }
        if (pkt.nextSendAtMs != 0 && (int32_t)(now - pkt.nextSendAtMs) < 0) {
            continue;
        }
        if (!candidate || (int32_t)(pkt.firstQueuedAtMs - candidate->firstQueuedAtMs) < 0) {
            candidate = &pkt;
        }
    }

    if (!candidate) {
        xSemaphoreGive(gQueueMutex);
        return false;
    }

    outPacket = *candidate;
    *candidate = PendingPacket();
    if (gQueueCount > 0) {
        gQueueCount--;
    }

    xSemaphoreGive(gQueueMutex);
    return true;
}

bool sendViaWifi(const PendingPacket& pkt) {
#if TRANSPORT_HAS_WIFIUDP
    if (!gWifiUdp) {
        return false;
    }
    if (WiFi.status() != WL_CONNECTED) {
        logbuf_printf("transport: WiFi not connected, deferring packet");
        return false;
    }
    if (!gWifiUdpBegun) {
        gWifiUdp->begin(0);
        gWifiUdpBegun = true;
    }
    if (gWifiUdp->beginPacket(gConfig.beamHost, gConfig.beamPort) != 1) {
        logbuf_printf("transport: WiFiUDP beginPacket failed");
        return false;
    }
    size_t written = gWifiUdp->write(reinterpret_cast<const uint8_t*>(pkt.payload), pkt.length);
    if (written != pkt.length) {
        logbuf_printf("transport: WiFiUDP short write (%u/%u)", static_cast<unsigned>(written), static_cast<unsigned>(pkt.length));
        gWifiUdp->stop();
        gWifiUdpBegun = false;
        return false;
    }
    if (gWifiUdp->endPacket() != 1) {
        logbuf_printf("transport: WiFiUDP endPacket failed");
        return false;
    }
    return true;
#else
    (void)pkt;
    return false;
#endif
}

bool sendViaTinyGsm(const PendingPacket& pkt) {
#if TRANSPORT_HAS_TINYGSM
    if (!gTinyGsmUdp) {
        return false;
    }
    if (gTinyGsm && !gTinyGsm->isGprsConnected()) {
        logbuf_printf("transport: modem not attached, deferring packet");
        return false;
    }
    if (!gTinyGsmUdp->beginPacket(gConfig.beamHost, gConfig.beamPort)) {
        logbuf_printf("transport: TinyGsm beginPacket failed");
        return false;
    }
    size_t written = gTinyGsmUdp->write(reinterpret_cast<const uint8_t*>(pkt.payload), pkt.length);
    if (written != pkt.length) {
        logbuf_printf("transport: TinyGsm short write (%u/%u)", static_cast<unsigned>(written), static_cast<unsigned>(pkt.length));
        gTinyGsmUdp->endPacket();
        return false;
    }
    if (!gTinyGsmUdp->endPacket()) {
        logbuf_printf("transport: TinyGsm endPacket failed");
        return false;
    }
    return true;
#else
    (void)pkt;
    return false;
#endif
}

bool transmitPacket(const PendingPacket& pkt) {
    if (pkt.length == 0) {
        return true;
    }
    bool success = false;
#if TRANSPORT_HAS_TINYGSM
    if (gTinyGsmUdp) {
        success = sendViaTinyGsm(pkt);
    }
#endif
#if TRANSPORT_HAS_WIFIUDP
    if (!success && gWifiUdp) {
        success = sendViaWifi(pkt);
    }
#endif
    if (!success) {
        logbuf_printf("transport: no transport path available");
    }
    return success;
}

bool enqueueNewPacket(TransportPacketKind kind, const char* json, size_t len) {
    if (!json || len == 0) {
        return false;
    }
    if (len >= TRANSPORT_MAX_PACKET_BYTES) {
        logbuf_printf("transport: payload too large (%u bytes)", static_cast<unsigned>(len));
        return false;
    }
    PendingPacket pkt;
    pkt.kind = kind;
    pkt.length = len;
    memcpy(pkt.payload, json, len);
    if (len < TRANSPORT_MAX_PACKET_BYTES) {
        pkt.payload[len] = '\0';
    }
    pkt.firstQueuedAtMs = millis();
    pkt.nextSendAtMs = 0;
    pkt.attempts = 0;
    return enqueueInternal(pkt, false);
}

bool enqueueRetryPacket(PendingPacket& pkt, uint8_t attemptNumber) {
    pkt.nextSendAtMs = millis() + computeBackoffMs(attemptNumber);
    return enqueueInternal(pkt, true);
}

} // namespace

TransportConfig transport_makeDefaultConfig() {
    TransportConfig cfg{};
    copyString(cfg.beamHost, sizeof(cfg.beamHost), TRANSPORT_DEFAULT_BEAM_HOST);
    cfg.beamPort = TRANSPORT_DEFAULT_BEAM_PORT;
    copyString(cfg.thingsboardHost, sizeof(cfg.thingsboardHost), TRANSPORT_DEFAULT_TB_HOST);
    cfg.thingsboardPort = TRANSPORT_DEFAULT_TB_PORT;
    cfg.accessToken[0] = '\0';
    copyString(cfg.sharedKeys, sizeof(cfg.sharedKeys), TRANSPORT_DEFAULT_SHARED_KEYS);
    cfg.maxAttempts = TRANSPORT_DEFAULT_MAX_ATTEMPTS;
    cfg.baseRetryDelayMs = TRANSPORT_DEFAULT_BASE_RETRY_MS;
    cfg.jitterMs = TRANSPORT_DEFAULT_JITTER_MS;
    return cfg;
}

bool transport_configure(const TransportConfig& cfg) {
    copyString(gConfig.beamHost, sizeof(gConfig.beamHost), cfg.beamHost);
    gConfig.beamPort = cfg.beamPort ? cfg.beamPort : TRANSPORT_DEFAULT_BEAM_PORT;
    copyString(gConfig.thingsboardHost, sizeof(gConfig.thingsboardHost), cfg.thingsboardHost);
    gConfig.thingsboardPort = cfg.thingsboardPort ? cfg.thingsboardPort : TRANSPORT_DEFAULT_TB_PORT;
    copyString(gConfig.accessToken, sizeof(gConfig.accessToken), cfg.accessToken);
    copyString(gConfig.sharedKeys, sizeof(gConfig.sharedKeys), cfg.sharedKeys);
    gConfig.maxAttempts = cfg.maxAttempts ? cfg.maxAttempts : TRANSPORT_DEFAULT_MAX_ATTEMPTS;
    gConfig.baseRetryDelayMs = cfg.baseRetryDelayMs ? cfg.baseRetryDelayMs : TRANSPORT_DEFAULT_BASE_RETRY_MS;
    gConfig.jitterMs = cfg.jitterMs;
    return true;
}

bool transport_init() {
    if (!gQueueMutex) {
        gQueueMutex = xSemaphoreCreateMutex();
        if (!gQueueMutex) {
            logbuf_printf("transport: failed to create queue mutex");
            return false;
        }
    }
    if (xSemaphoreTake(gQueueMutex, portMAX_DELAY) == pdTRUE) {
        for (auto& pkt : gQueue) {
            pkt = PendingPacket();
        }
        gQueueCount = 0;
        xSemaphoreGive(gQueueMutex);
    }
    transport_resetStats();
    logbuf_printf("transport: Beam UDP init host=%s port=%u", gConfig.beamHost, static_cast<unsigned>(gConfig.beamPort));
    return true;
}

bool transport_sendTelemetry(const char* json, size_t len) {
    if (!enqueueNewPacket(TransportPacketKind::Telemetry, json, len)) {
        return false;
    }
    transport_process();
    return true;
}

bool transport_sendAttributes(const char* json, size_t len) {
    if (!enqueueNewPacket(TransportPacketKind::Attributes, json, len)) {
        return false;
    }
    transport_process();
    return true;
}

bool transport_fetchShared(char* out, size_t outSz) {
    (void)out;
    (void)outSz;
    logbuf_printf("transport: fetchShared not implemented for Beam UDP");
    return false;
}

void transport_process() {
    PendingPacket pkt;
    while (dequeueReadyPacket(pkt)) {
        uint8_t attemptNumber = pkt.attempts + 1; // about to attempt send
        bool ok = transmitPacket(pkt);
        if (ok) {
            gStats.sent++;
            continue;
        }
        pkt.attempts = attemptNumber;
        if (pkt.attempts >= gConfig.maxAttempts) {
            gStats.failed++;
            logbuf_printf("transport: dropping packet after %u attempts", static_cast<unsigned>(pkt.attempts));
            continue;
        }
        if (!enqueueRetryPacket(pkt, pkt.attempts)) {
            gStats.failed++;
            logbuf_printf("transport: retry enqueue failed, dropping packet");
        }
    }
}

TransportStats transport_getStats() {
    return gStats;
}

void transport_resetStats() {
    gStats = TransportStats();
}

void transport_attachWiFiUdp(WiFiUDP* udp) {
#if TRANSPORT_HAS_WIFIUDP
    gWifiUdp = udp;
    gWifiUdpBegun = false;
#else
    (void)udp;
    logbuf_printf("transport: WiFiUDP support not compiled in");
#endif
}

void transport_attachTinyGsm(TinyGsm* modem, TinyGsmUDP* udp) {
#if TRANSPORT_HAS_TINYGSM
    gTinyGsm = modem;
    gTinyGsmUdp = udp;
#else
    (void)modem;
    (void)udp;
    logbuf_printf("transport: TinyGSM support not compiled in");
#endif
}
