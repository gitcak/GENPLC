#include "rs485_adapter.h"

/**
 * @brief Construct a new Rs485Adapter::Rs485Adapter object.
 *
 * Initializes the adapter with a null StampPLC pointer, sets the start time,
 * and resets the last frame time and error count.
 */
Rs485Adapter::Rs485Adapter()
    : stampPLC(nullptr), lastFrameTime(0), startTime(millis()), errorCount(0) {}

/**
 * @brief Sets the BasicStampPLC instance for the adapter.
 *
 * This must be called before the adapter can be used to get status, as it
 * provides the necessary hardware interface.
 *
 * @param plc A pointer to the BasicStampPLC instance.
 */
void Rs485Adapter::setStampPLC(BasicStampPLC* plc) {
    stampPLC = plc;
}

/**
 * @brief Retrieves the current status and metrics of the RS485 adapter.
 *
 * This function compiles a comprehensive status report, including uptime,
 * error count, initialization state, line voltage (if available), and
 * human-readable status summaries.
 *
 * @return An Rs485AdapterStatus struct containing the current status.
 */
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

/**
 * @brief Records the successful reception of a communication frame.
 *
 * This function updates the `lastFrameTime` to the current time, which is used
 * to determine if the bus is active.
 */
void Rs485Adapter::recordFrameReceived() {
    lastFrameTime = millis();
}

/**
 * @brief Records a communication error.
 *
 * This function increments the internal error counter.
 */
void Rs485Adapter::recordError() {
    errorCount++;
}

/**
 * @brief Calculates the time elapsed since the last frame was received.
 *
 * @return The age of the last frame in seconds. Returns UINT32_MAX if no
 *         frame has ever been received.
 */
uint32_t Rs485Adapter::getLastFrameAge() const {
    if (lastFrameTime == 0) return UINT32_MAX;
    return (millis() - lastFrameTime) / 1000;
}

/**
 * @brief A static factory method to get the status of the RS485 adapter.
 *
 * This function uses a static instance of the `Rs485Adapter` for convenience,
 * allowing status retrieval without needing to manage an instance of the adapter
 * in the calling code.
 *
 * @param stampPLC A pointer to the BasicStampPLC instance.
 * @return An Rs485AdapterStatus struct with the current status.
 */
Rs485AdapterStatus Rs485AdapterStatus::fromStampPLC(BasicStampPLC* stampPLC) {
    static Rs485Adapter adapter; // Static instance for simplicity
    adapter.setStampPLC(stampPLC);
    return adapter.getStatus();
}