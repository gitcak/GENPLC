#include "rs485_adapter.h"

Rs485Adapter::Rs485Adapter()
    : stampPLC(nullptr), lastFrameTime(0), startTime(millis()), errorCount(0) {}

void Rs485Adapter::setStampPLC(BasicStampPLC* plc) {
    stampPLC = plc;
}

Rs485AdapterStatus Rs485Adapter::getStatus() {
    Rs485AdapterStatus status;
    status.uptimeSeconds = (millis() - startTime) / 1000;
    status.errorCount = errorCount;

    if (!stampPLC) {
        status.available = false;
        status.initialized = false;
        status.lastFrameTime = 0;
        status.lineVoltage = 0.0f;
        status.statusText = "No StampPLC";
        status.formattedSummary = "RS485: Module not available\nStatus: Offline";
        return status;
    }

    status.available = true;
    status.initialized = stampPLC->isReady();
    status.lastFrameTime = lastFrameTime;

    // Try to read line voltage from analog input (assuming AI0 is voltage sense)
    if (status.initialized) {
        uint16_t rawVoltage = stampPLC->getAnalogInput(0); // Assume AI0 is voltage sense
        status.lineVoltage = (rawVoltage * 3.3f) / 1023.0f; // Convert to voltage (assuming 3.3V reference)
    } else {
        status.lineVoltage = 0.0f;
    }

    // Determine status text
    if (!status.initialized) {
        status.statusText = "Not initialized";
    } else if (getLastFrameAge() > 30) { // No frames for 30+ seconds
        status.statusText = "Initialized but inactive";
    } else {
        status.statusText = "Active";
    }

    // Format summary
    status.formattedSummary = "RS485 Bus Status\n";
    status.formattedSummary += "State: " + status.statusText + "\n";
    status.formattedSummary += "Errors: " + String(status.errorCount) + "\n";

    if (status.lineVoltage > 0.1f) {
        status.formattedSummary += "Line Voltage: " + String(status.lineVoltage, 2) + "V\n";
    } else {
        status.formattedSummary += "Line Voltage: --\n";
    }

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

void Rs485Adapter::recordFrameReceived() {
    lastFrameTime = millis();
}

void Rs485Adapter::recordError() {
    errorCount++;
}

uint32_t Rs485Adapter::getLastFrameAge() const {
    if (lastFrameTime == 0) return UINT32_MAX;
    return (millis() - lastFrameTime) / 1000;
}

Rs485AdapterStatus Rs485AdapterStatus::fromStampPLC(BasicStampPLC* stampPLC) {
    static Rs485Adapter adapter; // Static instance for simplicity
    adapter.setStampPLC(stampPLC);
    return adapter.getStatus();
}
