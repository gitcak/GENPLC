#include "can_status.h"
#include "pwrcan_module.h"

CanStatus CanStatus::create(const PWRCANModule& module) {
    CanStatus status;

    // Set raw values
    status.initialized = module.isInitialized();
    status.started = module.isStarted();
    status.errorCount = module.getErrorCount();
    status.lastUpdate = millis();

    // Set extended diagnostics
    status.framesReceived = module.getFramesReceived();
    status.framesTransmitted = module.getFramesTransmitted();
    status.busLoadPercent = module.getBusLoad();
    status.lastErrorCode = module.getLastErrorCode();
    status.lastBusActivityTime = module.getLastErrorCode(); // Using error code as placeholder for activity time

    // Set derived text values
    if (status.initialized) {
        status.statusText = "Initialized: Yes\nStarted: " + String(status.started ? "Yes" : "No");
    } else {
        status.statusText = "Initialized: No";
    }

    status.errorText = "Error Count: " + String(status.errorCount);
    status.trafficText = "RX: " + String(status.framesReceived) + ", TX: " + String(status.framesTransmitted);
    status.busLoadText = "Bus Load: " + String(status.busLoadPercent) + "%";

    String errorDesc = module.getLastErrorDescription();
    status.lastErrorText = "Last Error: " + (errorDesc.length() > 0 ? errorDesc : "None");

    if (status.lastBusActivityTime > 0) {
        uint32_t age = (millis() - status.lastBusActivityTime) / 1000;
        if (age < 60) {
            status.activityText = "Last Activity: " + String(age) + "s ago";
        } else {
            status.activityText = "Last Activity: " + String(age / 60) + "m ago";
        }
    } else {
        status.activityText = "Last Activity: Never";
    }

    // Format the full summary
    if (status.initialized) {
        status.formattedSummary = "Initialized: Yes\n";
        status.formattedSummary += "Started: " + String(status.started ? "Yes" : "No") + "\n";
        status.formattedSummary += "Error Count: " + String(status.errorCount) + "\n";
        status.formattedSummary += "Traffic: RX " + String(status.framesReceived) + ", TX " + String(status.framesTransmitted) + "\n";
        status.formattedSummary += "Bus Load: " + String(status.busLoadPercent) + "%\n";
        status.formattedSummary += status.lastErrorText + "\n";
        status.formattedSummary += status.activityText;
    } else {
        status.formattedSummary = "CAN bus module not initialized";
    }

    return status;
}
