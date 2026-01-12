#pragma once

#include <Arduino.h>

// Forward declaration
struct GNSSData;

/**
 * Enhanced GNSS status DTO with ready-to-render values
 */
struct GnssStatus {
    // Raw data
    double latitude;
    double longitude;
    double altitude;
    float speed;
    uint8_t satellites;
    bool isValid;
    uint32_t lastUpdate;
    
    // Derived/formatted values
    uint16_t satellitesPercentage;  // 0-100% based on satellites count
    String fixStatus;               // "Valid Fix", "No Fix"
    String positionText;            // Formatted lat/lon text
    String altitudeText;            // Formatted altitude text
    String speedText;               // Formatted speed text
    String satellitesText;          // "X satellites (Y%)"
    String formattedSummary;        // Multi-line text for display
    
    // Helper method to calculate derived values from raw data
    static GnssStatus fromGNSSData(const GNSSData& data);
};
