#pragma once

#include <Arduino.h>

// Forward declaration
class PWRCANModule;

/**
 * Enhanced CAN bus status DTO with ready-to-render values
 */
struct CanStatus {
    // Raw data
    bool initialized;
    bool started;
    uint32_t errorCount;
    uint32_t lastUpdate;

    // Extended diagnostics
    uint32_t framesReceived;
    uint32_t framesTransmitted;
    uint32_t busLoadPercent;
    uint32_t lastErrorCode;
    uint32_t lastBusActivityTime;

    // Derived/formatted values
    String statusText;           // "Initialized: Yes/No", "Started: Yes/No"
    String errorText;            // "Error Count: X"
    String trafficText;          // "RX: X, TX: Y frames"
    String busLoadText;          // "Bus Load: Z%"
    String lastErrorText;        // "Last Error: description"
    String activityText;         // "Last Activity: time ago"
    String formattedSummary;     // Complete summary text

    // Helper method to create CanStatus with derived values
    static CanStatus create(const class PWRCANModule& module);
};
