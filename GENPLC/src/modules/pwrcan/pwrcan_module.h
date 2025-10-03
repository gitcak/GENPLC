// PWRCAN module skeleton for StampPLC (ESP32-S3)
// Safe to compile even if not used yet. Uses ESP-IDF TWAI driver when available.

#ifndef PWRCAN_MODULE_H
#define PWRCAN_MODULE_H

#include <Arduino.h>
#include "can_status.h"
#include "can_generator_protocol.h"

#if defined(ARDUINO_ARCH_ESP32)
extern "C" {
#include "driver/twai.h"
}
#endif

class PWRCANModule {
private:
    CanGeneratorProtocol generatorProtocol;

public:
    PWRCANModule();
    ~PWRCANModule();

    // Initialize CAN with given TX/RX pins and bitrate (kbps). Returns true on success.
    bool begin(int txPin, int rxPin, int bitrateKbps = 250);

    // Stop and uninstall driver.
    void end();

    // Status helpers
    bool isInitialized() const { return isInitializedFlag; }
    bool isStarted() const { return isStartedFlag; }
    uint32_t getErrorCount() const { return errorCount; }

    // Extended status information
    uint32_t getFramesReceived() const { return framesReceived; }
    uint32_t getFramesTransmitted() const { return framesTransmitted; }
    uint32_t getBusLoad() const { return busLoadPercent; }
    uint32_t getLastErrorCode() const { return lastErrorCode; }
    String getLastErrorDescription() const;

    // Enhanced DTO with ready-to-render values
    CanStatus getCanStatus() const;

    // Generator protocol methods
    bool processReceivedFrame(uint32_t identifier, const uint8_t* payload, uint8_t length);
    CanGeneratorProtocol& getGeneratorProtocol() { return generatorProtocol; }

    // Send a CAN frame (Standard ID by default). Returns true if queued.
    bool sendFrame(uint32_t identifier,
                   const uint8_t *payload,
                   uint8_t length,
                   bool isExtendedId = false,
                   bool isRemoteRequest = false,
                   uint32_t timeoutMs = 10);

    // Receive a CAN frame if available (non-blocking when timeoutMs == 0).
    bool receiveFrame(uint32_t &identifier,
                      uint8_t *payload,
                      uint8_t &length,
                      bool &isExtendedId,
                      bool &isRemoteRequest,
                      uint32_t timeoutMs = 0);

private:
    bool isInitializedFlag;
    bool isStartedFlag;
    uint32_t errorCount;

    // Extended diagnostics
    uint32_t framesReceived;
    uint32_t framesTransmitted;
    uint32_t busLoadPercent;
    uint32_t lastErrorCode;
    uint32_t lastBusActivityTime;

#if defined(ARDUINO_ARCH_ESP32)
    twai_timing_config_t selectTimingConfig(int bitrateKbps);
#endif
};

#endif // PWRCAN_MODULE_H


