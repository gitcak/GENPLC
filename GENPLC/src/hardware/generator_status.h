#pragma once

#include <Arduino.h>
#include <stdint.h>
#include "../modules/pwrcan/can_generator_protocol.h"

/**
 * Generator status DTO with calibrated sensor values and service counters
 */
struct GeneratorStatus {
    // Raw sensor data
    uint16_t fuelLevelRaw;      // Raw analog value 0-1023
    uint16_t fuelFilterRaw;     // Raw analog value 0-1023
    uint16_t oilLevelRaw;       // Raw analog value 0-1023
    uint16_t oilFilterRaw;      // Raw analog value 0-1023

    // Calibrated values (0-100%)
    uint16_t fuelLevelPercent;
    uint16_t fuelFilterLifePercent;
    uint16_t oilLevelPercent;
    uint16_t oilFilterLifePercent;

    // Service counters
    uint32_t totalRunTimeHours;    // Total running hours
    uint32_t lastServiceTimestamp; // Unix timestamp of last service
    uint32_t fuelFilterHours;      // Hours since fuel filter service
    uint32_t oilFilterHours;       // Hours since oil filter service
    uint32_t oilChangeHours;       // Hours since oil change

    // Relay states
    bool relayOutputs[2];
    bool digitalInputs[8];

    // Status
    bool moduleReady;

    // Formatted display strings
    String runTimeText;         // "XXX hours total"
    String lastServiceText;     // "X days ago" or "Never"
    String fuelFilterText;      // "XX% life remaining"
    String oilFilterText;       // "XX% life remaining"
    String fuelLevelText;       // "XX% remaining"
    String oilLevelText;        // "XX% remaining"

    String relayStatusText;     // Formatted table of relay states
    String canStatusText;      // CAN connection status

    // Helper method to create from CAN protocol data with calibration
    static GeneratorStatus fromCanData(
        const CanGeneratorProtocol& canProtocol,
        bool canModuleReady
    );

    // Legacy method for backward compatibility (deprecated)
    static GeneratorStatus fromRawData(
        uint16_t fuelRaw, uint16_t fuelFilterRaw, uint16_t oilRaw, uint16_t oilFilterRaw,
        bool relays[2], bool inputs[8], bool ready, uint32_t runTimeHours, uint32_t lastServiceTs,
        uint32_t fuelFilterHrs, uint32_t oilFilterHrs, uint32_t oilChangeHrs
    );
};

/**
 * Generator calibration settings
 */
struct GeneratorCalibration {
    // Sensor calibration points (raw values for 0%, 25%, 50%, 75%, 100%)
    uint16_t fuelLevelCal[5];      // Calibration points for fuel level
    uint16_t fuelFilterCal[5];     // Calibration points for fuel filter life
    uint16_t oilLevelCal[5];       // Calibration points for oil level
    uint16_t oilFilterCal[5];      // Calibration points for oil filter life

    // Service intervals (in hours)
    uint32_t fuelFilterIntervalHours;
    uint32_t oilFilterIntervalHours;
    uint32_t oilChangeIntervalHours;

    // Helper methods
    static GeneratorCalibration getDefaults();
    uint16_t calibrateSensor(uint16_t rawValue, const uint16_t calPoints[5]) const;
};
