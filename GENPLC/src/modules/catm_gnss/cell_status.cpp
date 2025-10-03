#include "cell_status.h"
#include "catm_gnss_module.h"

CellStatus CellStatus::fromCellularData(const CellularData& data, const String& apnStr, const String& imeiStr) {
    CellStatus status;
    
    // Copy raw values
    status.isConnected = data.isConnected;
    status.operatorName = data.operatorName;
    status.signalStrength = data.signalStrength;
    status.imei = imeiStr;
    status.apn = apnStr.length() ? apnStr : data.apn;
    status.ipAddress = data.ipAddress;
    status.registrationState = data.registrationState;
    status.registrationText = "Unknown";
    status.lastUpdate = data.lastUpdate;
    status.lastDetachReason = data.lastDetachReason;
    
    // Calculate signal percentage (same formula as in ui_pages.cpp)
    if (data.signalStrength <= -113) {
        status.signalPercentage = 0;
    } else if (data.signalStrength >= -51) {
        status.signalPercentage = 100;
    } else {
        status.signalPercentage = (uint16_t)((data.signalStrength + 113) * 100 / 62);
    }
    
    switch (status.registrationState) {
        case 0: status.registrationText = "Not registered"; break;
        case 1: status.registrationText = "Registered (home)"; break;
        case 2: status.registrationText = "Searching"; break;
        case 3: status.registrationText = "Registration denied"; break;
        case 4: status.registrationText = "Unknown"; break;
        case 5: status.registrationText = "Registered (roaming)"; break;
        default: status.registrationText = String("State ") + status.registrationState; break;
    }

    // Signal quality text
    if (status.signalPercentage >= 80) {
        status.signalText = "Excellent";
    } else if (status.signalPercentage >= 60) {
        status.signalText = "Good";
    } else if (status.signalPercentage >= 40) {
        status.signalText = "Fair";
    } else if (status.signalPercentage > 0) {
        status.signalText = "Poor";
    } else {
        status.signalText = "No Signal";
    }
    
    // Status text
    if (status.isConnected) {
        if (status.operatorName.length() > 0) {
            status.statusText = "Connected to " + status.operatorName;
        } else {
            status.statusText = "Connected";
        }
    } else {
        status.statusText = "Offline";
    }
    
    // RSSI text
    status.rssiText = String(status.signalStrength) + " dBm (" + String(status.signalPercentage) + "%)";
    
    // Format the full summary text (matches ui_pages.cpp format)
    status.formattedSummary = "Status: " + String(status.isConnected ? "Online" : "Offline") + "\n";
    status.formattedSummary += "Operator: " + (status.operatorName.length() ? status.operatorName : String("--")) + "\n";
    status.formattedSummary += "RSSI: " + status.rssiText + "\n";
    status.formattedSummary += "APN: " + status.apn + "\n";
    status.formattedSummary += "IP: " + (status.ipAddress.length() ? status.ipAddress : String("--")) + "\n";
    status.formattedSummary += "Reg: " + status.registrationText + "\n";
    status.formattedSummary += "IMEI: " + status.imei;
    
    return status;
}
