#pragma once

#include <Arduino.h>

// Forward declarations
struct CellularData;

/**
 * Enhanced cellular status DTO with ready-to-render values
 */
struct CellStatus {
    // Raw data
    bool isConnected;
    String operatorName;
    int8_t signalStrength;
    String imei;
    String apn;
    String ipAddress;
    uint8_t registrationState;
    String registrationText;
    uint32_t lastUpdate;
    String lastDetachReason;

    uint64_t txBytes;
    uint64_t rxBytes;
    uint32_t txBps;
    uint32_t rxBps;

    // Derived/formatted values
    uint16_t signalPercentage;  // 0-100%
    String signalText;          // "Excellent", "Good", "Fair", "Poor", "No Signal"
    String statusText;          // "Connected to [Operator]", "Offline", etc.
    String rssiText;            // "-XX dBm (YY%)"
    String throughputText;      // "Tx: 1.2 MB (8 kbps) / Rx: ..."
    String formattedSummary;    // Multi-line text for display
    
    // Helper method to calculate derived values from raw data
    static CellStatus fromCellularData(const CellularData& data, const String& apnStr, const String& imeiStr);
};

