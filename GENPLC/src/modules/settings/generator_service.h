#pragma once

#include <Arduino.h>

/**
 * Generator service counter data
 */
struct GeneratorServiceData {
    uint32_t totalRunTimeHours;    // Total running hours
    uint32_t lastServiceTimestamp; // Unix timestamp of last service
    uint32_t fuelFilterHours;      // Hours since fuel filter service
    uint32_t oilFilterHours;       // Hours since oil filter service
    uint32_t oilChangeHours;       // Hours since oil change
    uint32_t lastUpdateTimestamp;  // Last time counters were updated
};

// Load generator service data from NVS
GeneratorServiceData loadGeneratorServiceData();

// Save generator service data to NVS
bool saveGeneratorServiceData(const GeneratorServiceData& data);

// Reset generator service data to defaults
void resetGeneratorServiceData();

// Update service counters (call periodically when generator is running)
void updateGeneratorServiceCounters(bool generatorRunning);

// Reset service counters after maintenance (call when service is performed)
void resetGeneratorServiceCounters(uint32_t currentTimestamp);
