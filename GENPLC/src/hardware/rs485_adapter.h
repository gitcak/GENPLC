#pragma once

#include <Arduino.h>
#include "basic_stamplc.h"

/**
 * RS485 Adapter - Reports RS485 bus availability, error counts, and last communication
 */
struct Rs485AdapterStatus {
    bool available;
    bool initialized;
    uint32_t errorCount;
    uint32_t lastFrameTime;
    uint32_t uptimeSeconds;
    float lineVoltage; // If available from analog inputs
    String statusText;
    String formattedSummary;

    // Helper to create from BasicStampPLC
    static Rs485AdapterStatus fromStampPLC(BasicStampPLC* stampPLC);
};

/**
 * RS485 Adapter class for monitoring RS485 bus health
 */
class Rs485Adapter {
private:
    BasicStampPLC* stampPLC;
    uint32_t lastFrameTime;
    uint32_t startTime;
    uint32_t errorCount;

public:
    Rs485Adapter();

    // Initialize with StampPLC reference
    void setStampPLC(BasicStampPLC* plc);

    // Get current status
    Rs485AdapterStatus getStatus();

    // Record successful communication
    void recordFrameReceived();

    // Record communication error
    void recordError();

    // Get last frame age in seconds
    uint32_t getLastFrameAge() const;
};
