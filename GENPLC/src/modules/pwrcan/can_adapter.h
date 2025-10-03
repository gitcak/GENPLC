#pragma once

#include <Arduino.h>
#include "pwrcan_module.h"

/**
 * CAN Adapter - Reports CAN bus availability, error counts, and last communication
 */
struct CanAdapterStatus {
    bool available;
    bool initialized;
    bool started;
    uint32_t errorCount;
    uint32_t lastFrameTime;
    uint32_t uptimeSeconds;
    String statusText;
    String formattedSummary;

    // Helper to create from PWRCANModule
    static CanAdapterStatus fromCanModule(PWRCANModule* canModule);
};

/**
 * CAN Adapter class for monitoring CAN bus health
 */
class CanAdapter {
private:
    PWRCANModule* canModule;
    uint32_t lastFrameTime;
    uint32_t startTime;

public:
    CanAdapter();

    // Initialize with CAN module reference
    void setCanModule(PWRCANModule* module);

    // Get current status
    CanAdapterStatus getStatus();

    // Record frame reception (call when frames are received)
    void recordFrameReceived();

    // Get last frame age in seconds
    uint32_t getLastFrameAge() const;
};
