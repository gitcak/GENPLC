#include "can_adapter.h"

CanAdapter::CanAdapter()
    : canModule(nullptr), lastFrameTime(0), startTime(millis()) {}

void CanAdapter::setCanModule(PWRCANModule* module) {
    canModule = module;
}

CanAdapterStatus CanAdapter::getStatus() {
    CanAdapterStatus status;
    status.uptimeSeconds = (millis() - startTime) / 1000;

    if (!canModule) {
        status.available = false;
        status.initialized = false;
        status.started = false;
        status.errorCount = 0;
        status.lastFrameTime = 0;
        status.statusText = "No CAN module";
        status.formattedSummary = "CAN: Module not available\nStatus: Offline";
        return status;
    }

    status.available = true;
    status.initialized = canModule->isInitialized();
    status.started = canModule->isStarted();
    status.errorCount = canModule->getErrorCount();
    status.lastFrameTime = lastFrameTime;

    // Determine status text
    if (!status.initialized) {
        status.statusText = "Not initialized";
    } else if (!status.started) {
        status.statusText = "Initialized but not started";
    } else if (getLastFrameAge() > 30) { // No frames for 30+ seconds
        status.statusText = "Started but inactive";
    } else {
        status.statusText = "Active";
    }

    // Format summary
    status.formattedSummary = "CAN Bus Status\n";
    status.formattedSummary += "State: " + status.statusText + "\n";
    status.formattedSummary += "Errors: " + String(status.errorCount) + "\n";

    if (status.lastFrameTime > 0) {
        uint32_t age = getLastFrameAge();
        status.formattedSummary += "Last Frame: ";
        if (age < 60) {
            status.formattedSummary += String(age) + "s ago";
        } else {
            status.formattedSummary += String(age / 60) + "m ago";
        }
    } else {
        status.formattedSummary += "Last Frame: Never";
    }

    status.formattedSummary += "\nUptime: " + String(status.uptimeSeconds / 60) + "m " +
                               String(status.uptimeSeconds % 60) + "s";

    return status;
}

void CanAdapter::recordFrameReceived() {
    lastFrameTime = millis();
}

uint32_t CanAdapter::getLastFrameAge() const {
    if (lastFrameTime == 0) return UINT32_MAX;
    return (millis() - lastFrameTime) / 1000;
}

CanAdapterStatus CanAdapterStatus::fromCanModule(PWRCANModule* canModule) {
    static CanAdapter adapter; // Static instance for simplicity
    adapter.setCanModule(canModule);
    return adapter.getStatus();
}
