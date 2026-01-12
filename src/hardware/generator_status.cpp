#include "generator_status.h"
#include "generator_calibration.h"

// Helper function to format time duration
static String formatTimeSince(uint32_t timestamp) {
    if (timestamp == 0) return "Never";

    uint32_t now = millis() / 1000; // Current time in seconds
    uint32_t diff = now - timestamp;

    if (diff < 3600) { // Less than 1 hour
        return String(diff / 60) + " min ago";
    } else if (diff < 86400) { // Less than 1 day
        return String(diff / 3600) + " hours ago";
    } else {
        return String(diff / 86400) + " days ago";
    }
}

GeneratorStatus GeneratorStatus::fromCanData(
    const CanGeneratorProtocol& canProtocol,
    bool canModuleReady
) {
    GeneratorStatus status;
    status.moduleReady = canModuleReady;

    // Get sensor data from CAN
    CanGeneratorSensors sensors;
    if (canProtocol.getSensors(sensors)) {
        status.fuelLevelRaw = sensors.fuelLevel;
        status.fuelFilterRaw = sensors.fuelFilter;
        status.oilLevelRaw = sensors.oilLevel;
        status.oilFilterRaw = sensors.oilFilter;
    } else {
        // Use fallback values if no CAN data
        status.fuelLevelRaw = 0;
        status.fuelFilterRaw = 0;
        status.oilLevelRaw = 0;
        status.oilFilterRaw = 0;
        status.moduleReady = false;
    }

    // Get relay/input data from CAN
    CanGeneratorRelays relays;
    if (canProtocol.getRelays(relays)) {
        status.relayOutputs[0] = (relays.relayStates & 0x01) != 0; // Bit 0 = R0
        status.relayOutputs[1] = (relays.relayStates & 0x02) != 0; // Bit 1 = R1

        // Extract digital inputs from bitfields
        for (int i = 0; i < 8; i++) {
            status.digitalInputs[i] = (relays.digitalInputsLow & (1 << i)) != 0;
        }
    } else {
        // Use fallback values
        status.relayOutputs[0] = false;
        status.relayOutputs[1] = false;
        memset(status.digitalInputs, 0, sizeof(status.digitalInputs));
        status.moduleReady = false;
    }

    // Get runtime data from CAN
    CanGeneratorRuntime runtime;
    if (canProtocol.getRuntime(runtime)) {
        status.totalRunTimeHours = runtime.totalRunTimeHours;
        status.lastServiceTimestamp = runtime.lastServiceTimestamp;
    } else {
        status.totalRunTimeHours = 0;
        status.lastServiceTimestamp = 0;
    }

    // Get filter hours from CAN
    CanGeneratorFilterHours filterHours;
    if (canProtocol.getFilterHours(filterHours)) {
        status.fuelFilterHours = filterHours.fuelFilterHours;
        status.oilFilterHours = filterHours.oilFilterHours;
        status.oilChangeHours = filterHours.oilChangeHours;
    } else {
        status.fuelFilterHours = 0;
        status.oilFilterHours = 0;
        status.oilChangeHours = 0;
    }

    // Get calibration settings
    GeneratorCalibration cal = getGeneratorCalibration();

    // Apply calibration
    status.fuelLevelPercent = cal.calibrateSensor(status.fuelLevelRaw, cal.fuelLevelCal);
    status.fuelFilterLifePercent = 100 - cal.calibrateSensor(status.fuelFilterRaw, cal.fuelFilterCal); // Inverted
    status.oilLevelPercent = cal.calibrateSensor(status.oilLevelRaw, cal.oilLevelCal);
    status.oilFilterLifePercent = 100 - cal.calibrateSensor(status.oilFilterRaw, cal.oilFilterCal); // Inverted

    // Format display strings
    status.runTimeText = String(status.totalRunTimeHours) + " hours total";
    status.lastServiceText = formatTimeSince(status.lastServiceTimestamp);
    status.fuelFilterText = String(status.fuelFilterLifePercent) + "% life remaining";
    status.oilFilterText = String(status.oilFilterLifePercent) + "% life remaining";
    status.fuelLevelText = String(status.fuelLevelPercent) + "% remaining";
    status.oilLevelText = String(status.oilLevelPercent) + "% remaining";

    // Add CAN status info
    status.canStatusText = canModuleReady ? "CAN: Connected" : "CAN: Offline";
    if (canModuleReady) {
        uint32_t sensorsAge = canProtocol.getSensorsAge();
        uint32_t relaysAge = canProtocol.getRelaysAge();
        status.canStatusText += "\nSensors: " + String(sensorsAge < 5000 ? "OK" : "STALE");
        status.canStatusText += " (" + String(sensorsAge/1000) + "s ago)";
        status.canStatusText += "\nRelays: " + String(relaysAge < 5000 ? "OK" : "STALE");
        status.canStatusText += " (" + String(relaysAge/1000) + "s ago)";
    }

    // Format relay status table
    status.relayStatusText = "Relays:\n";
    for (int i = 0; i < 2; i++) {
        status.relayStatusText += "R" + String(i) + ": " + (status.relayOutputs[i] ? "ON" : "OFF");
        if (i == 0) status.relayStatusText += " | ";
    }
    status.relayStatusText += "\n\nInputs:\n";
    for (int i = 0; i < 8; i++) {
        status.relayStatusText += "DI" + String(i) + ": " + (status.digitalInputs[i] ? "HIGH" : "LOW");
        if (i < 7) status.relayStatusText += " ";
        if ((i + 1) % 4 == 0) status.relayStatusText += "\n";
    }

    return status;
}

// Legacy method for backward compatibility
GeneratorStatus GeneratorStatus::fromRawData(
    uint16_t fuelRaw, uint16_t fuelFilterRaw, uint16_t oilRaw, uint16_t oilFilterRaw,
    bool relays[2], bool inputs[8], bool ready, uint32_t runTimeHours, uint32_t lastServiceTs,
    uint32_t fuelFilterHrs, uint32_t oilFilterHrs, uint32_t oilChangeHrs
) {
    GeneratorStatus status;

    // Copy raw data
    status.fuelLevelRaw = fuelRaw;
    status.fuelFilterRaw = fuelFilterRaw;
    status.oilLevelRaw = oilRaw;
    status.oilFilterRaw = oilFilterRaw;

    memcpy(status.relayOutputs, relays, sizeof(status.relayOutputs));
    memcpy(status.digitalInputs, inputs, sizeof(status.digitalInputs));

    status.moduleReady = ready;
    status.totalRunTimeHours = runTimeHours;
    status.lastServiceTimestamp = lastServiceTs;
    status.fuelFilterHours = fuelFilterHrs;
    status.oilFilterHours = oilFilterHrs;
    status.oilChangeHours = oilChangeHrs;

    // Get calibration settings
    GeneratorCalibration cal = getGeneratorCalibration();

    // Apply calibration
    status.fuelLevelPercent = cal.calibrateSensor(fuelRaw, cal.fuelLevelCal);
    status.fuelFilterLifePercent = 100 - cal.calibrateSensor(fuelFilterRaw, cal.fuelFilterCal); // Inverted
    status.oilLevelPercent = cal.calibrateSensor(oilRaw, cal.oilLevelCal);
    status.oilFilterLifePercent = 100 - cal.calibrateSensor(oilFilterRaw, cal.oilFilterCal); // Inverted

    // Format display strings
    status.runTimeText = String(runTimeHours) + " hours total";
    status.lastServiceText = formatTimeSince(lastServiceTs);
    status.fuelFilterText = String(status.fuelFilterLifePercent) + "% life remaining";
    status.oilFilterText = String(status.oilFilterLifePercent) + "% life remaining";
    status.fuelLevelText = String(status.fuelLevelPercent) + "% remaining";
    status.oilLevelText = String(status.oilLevelPercent) + "% remaining";

    // Format relay status table
    status.relayStatusText = "Relays:\n";
    for (int i = 0; i < 2; i++) {
        status.relayStatusText += "R" + String(i) + ": " + (relays[i] ? "ON" : "OFF");
        if (i == 0) status.relayStatusText += " | ";
    }
    status.relayStatusText += "\n\nInputs:\n";
    for (int i = 0; i < 8; i++) {
        status.relayStatusText += "DI" + String(i) + ": " + (inputs[i] ? "HIGH" : "LOW");
        if (i < 7) status.relayStatusText += " ";
        if ((i + 1) % 4 == 0) status.relayStatusText += "\n";
    }

    return status;
}
