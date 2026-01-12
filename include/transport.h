#ifndef TRANSPORT_H
#define TRANSPORT_H

#include <Arduino.h>

static constexpr size_t TRANSPORT_MAX_PACKET_BYTES = 768;
static constexpr size_t TRANSPORT_QUEUE_DEPTH = 8;
static constexpr size_t TRANSPORT_MAX_HOST_LEN = 64;
static constexpr size_t TRANSPORT_MAX_PATH_LEN = 128;
static constexpr size_t TRANSPORT_MAX_SHARED_KEYS = 128;
static constexpr size_t TRANSPORT_MAX_TOKEN_LEN = 80;

#ifndef TRANSPORT_DEFAULT_BEAM_HOST
#define TRANSPORT_DEFAULT_BEAM_HOST "beam.soracom.io"
#endif
#ifndef TRANSPORT_DEFAULT_BEAM_PORT
#define TRANSPORT_DEFAULT_BEAM_PORT 23080
#endif
#ifndef TRANSPORT_DEFAULT_TB_HOST
#define TRANSPORT_DEFAULT_TB_HOST "thingsboard.vardanetworks.com"
#endif
#ifndef TRANSPORT_DEFAULT_TB_PORT
#define TRANSPORT_DEFAULT_TB_PORT 443
#endif
#ifndef TRANSPORT_DEFAULT_SHARED_KEYS
#define TRANSPORT_DEFAULT_SHARED_KEYS "report_period_s,rpm_alert,ota_url"
#endif
#ifndef TRANSPORT_DEFAULT_BASE_RETRY_MS
#define TRANSPORT_DEFAULT_BASE_RETRY_MS 1000UL
#endif
#ifndef TRANSPORT_DEFAULT_JITTER_MS
#define TRANSPORT_DEFAULT_JITTER_MS 200UL
#endif
#ifndef TRANSPORT_DEFAULT_MAX_ATTEMPTS
#define TRANSPORT_DEFAULT_MAX_ATTEMPTS 6
#endif

enum class TransportPacketKind : uint8_t {
    Telemetry = 0,
    Attributes = 1
};

struct TransportConfig {
    char beamHost[TRANSPORT_MAX_HOST_LEN];
    uint16_t beamPort;
    char thingsboardHost[TRANSPORT_MAX_HOST_LEN];
    uint16_t thingsboardPort;
    char accessToken[TRANSPORT_MAX_TOKEN_LEN];
    char sharedKeys[TRANSPORT_MAX_SHARED_KEYS];
    uint8_t maxAttempts;
    uint32_t baseRetryDelayMs;
    uint32_t jitterMs;
};

struct TransportStats {
    uint32_t queued = 0;
    uint32_t sent = 0;
    uint32_t retries = 0;
    uint32_t dropped = 0;
    uint32_t failed = 0;
};

TransportConfig transport_makeDefaultConfig();
bool transport_configure(const TransportConfig& cfg);
bool transport_init();
bool transport_sendTelemetry(const char* json, size_t len);
bool transport_sendAttributes(const char* json, size_t len);
bool transport_fetchShared(char* out, size_t outSz);
void transport_process();
TransportStats transport_getStats();
void transport_resetStats();

class WiFiUDP;
class TinyGsm;
class TinyGsmUDP;

void transport_attachWiFiUdp(WiFiUDP* udp);
void transport_attachTinyGsm(TinyGsm* modem, TinyGsmUDP* udp);

#endif // TRANSPORT_H
