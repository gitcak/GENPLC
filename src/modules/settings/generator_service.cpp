#include "generator_service.h"
#include <Preferences.h>

#define GEN_SERVICE_NAMESPACE "gen_service"

GeneratorServiceData loadGeneratorServiceData() {
    Preferences prefs;
    GeneratorServiceData data = {0, 0, 0, 0, 0, 0}; // Initialize to zeros

    if (prefs.begin(GEN_SERVICE_NAMESPACE, true)) { // Read-only mode
        data.totalRunTimeHours = prefs.getULong("totalHrs", 0);
        data.lastServiceTimestamp = prefs.getULong("lastSvc", 0);
        data.fuelFilterHours = prefs.getULong("fuelFHrs", 0);
        data.oilFilterHours = prefs.getULong("oilFHrs", 0);
        data.oilChangeHours = prefs.getULong("oilCHrs", 0);
        data.lastUpdateTimestamp = prefs.getULong("lastUpd", 0);
        prefs.end();
    }

    return data;
}

bool saveGeneratorServiceData(const GeneratorServiceData& data) {
    Preferences prefs;

    if (!prefs.begin(GEN_SERVICE_NAMESPACE, false)) { // Read-write mode
        return false;
    }

    prefs.putULong("totalHrs", data.totalRunTimeHours);
    prefs.putULong("lastSvc", data.lastServiceTimestamp);
    prefs.putULong("fuelFHrs", data.fuelFilterHours);
    prefs.putULong("oilFHrs", data.oilFilterHours);
    prefs.putULong("oilCHrs", data.oilChangeHours);
    prefs.putULong("lastUpd", data.lastUpdateTimestamp);

    prefs.end();
    return true;
}

void resetGeneratorServiceData() {
    GeneratorServiceData data = {0, 0, 0, 0, 0, 0};
    saveGeneratorServiceData(data);
}

void updateGeneratorServiceCounters(bool generatorRunning) {
    static uint32_t lastUpdateTime = 0;
    uint32_t now = millis();

    // Only update every hour (3600000 ms) to avoid excessive wear on NVS
    if ((now - lastUpdateTime) < 3600000) {
        return;
    }
    lastUpdateTime = now;

    GeneratorServiceData data = loadGeneratorServiceData();

    if (generatorRunning) {
        // Increment counters when generator is running
        uint32_t hoursElapsed = 1; // Assuming this function is called every hour

        data.totalRunTimeHours += hoursElapsed;
        data.fuelFilterHours += hoursElapsed;
        data.oilFilterHours += hoursElapsed;
        data.oilChangeHours += hoursElapsed;
    }

    data.lastUpdateTimestamp = now / 1000; // Store as seconds since boot
    saveGeneratorServiceData(data);
}

void resetGeneratorServiceCounters(uint32_t currentTimestamp) {
    GeneratorServiceData data = loadGeneratorServiceData();

    // Reset all service interval counters to zero
    data.fuelFilterHours = 0;
    data.oilFilterHours = 0;
    data.oilChangeHours = 0;

    // Update last service timestamp
    data.lastServiceTimestamp = currentTimestamp;
    data.lastUpdateTimestamp = millis() / 1000;

    saveGeneratorServiceData(data);
}
