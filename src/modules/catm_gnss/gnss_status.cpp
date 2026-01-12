#include "gnss_status.h"
#include "catm_gnss_module.h"

// Helper to format floating point numbers consistently
static String formatFloat(double value, uint8_t digits) {
    char buf[32];
    dtostrf(value, 0, digits, buf);
    return String(buf);
}

GnssStatus GnssStatus::fromGNSSData(const GNSSData& data) {
    GnssStatus status;
    
    // Copy raw values
    status.latitude = data.latitude;
    status.longitude = data.longitude;
    status.altitude = data.altitude;
    status.speed = data.speed;
    status.satellites = data.satellites;
    status.isValid = data.isValid;
    status.lastUpdate = data.lastUpdate;
    
    // Calculate satellites percentage (20+ satellites = 100%)
    status.satellitesPercentage = (data.satellites >= 20) ? 100 : (uint16_t)((data.satellites * 100U) / 20U);
    
    // Fix status text
    status.fixStatus = data.isValid ? "Valid Fix" : "No Fix";
    
    // Position text
    status.positionText = formatFloat(data.latitude, 5) + ", " + formatFloat(data.longitude, 5);
    
    // Altitude text
    status.altitudeText = formatFloat(data.altitude, 1) + " m";
    
    // Speed text
    status.speedText = formatFloat(data.speed, 2) + " m/s";
    
    // Satellites text
    status.satellitesText = String((int)data.satellites) + " satellites (" + String((int)status.satellitesPercentage) + "%)";
    
    // Format the full summary text (matches ui_pages.cpp format)
    status.formattedSummary = "Satellites: " + String((int)data.satellites) + " (" + String((int)status.satellitesPercentage) + "%)\n";
    status.formattedSummary += "Fix: " + status.fixStatus + "\n";
    status.formattedSummary += "Lat: " + formatFloat(data.latitude, 5) + "\n";
    status.formattedSummary += "Lon: " + formatFloat(data.longitude, 5) + "\n";
    status.formattedSummary += "Speed: " + formatFloat(data.speed, 2) + " m/s\n";
    status.formattedSummary += "Altitude: " + formatFloat(data.altitude, 1) + " m";
    
    return status;
}
