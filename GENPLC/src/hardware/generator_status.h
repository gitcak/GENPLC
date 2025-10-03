#pragma once

#include <Arduino.h>
#include <stdint.h>
#include "../modules/pwrcan/can_generator_protocol.h"

/**
 * @file generator_status.h
 * @brief Defines data structures for generator status and calibration.
 *
 * This file contains the definitions for the `GeneratorStatus` and
 * `GeneratorCalibration` structs, which are used throughout the application
 * to manage and represent the generator's state and configuration.
 */

/**
 * @struct GeneratorStatus
 * @brief A Data Transfer Object (DTO) that holds the generator's current status.
 *
 * This struct aggregates raw sensor data, calibrated values, service counters,
 * and formatted text for display purposes. It provides a comprehensive snapshot
 * of the generator's state at any given time.
 */
struct GeneratorStatus {
    // Raw sensor data
    uint16_t fuelLevelRaw;      ///< Raw analog value for fuel level (0-1023).
    uint16_t fuelFilterRaw;     ///< Raw analog value for fuel filter (0-1023).
    uint16_t oilLevelRaw;       ///< Raw analog value for oil level (0-1023).
    uint16_t oilFilterRaw;      ///< Raw analog value for oil filter (0-1023).

    // Calibrated values (0-100%)
    uint16_t fuelLevelPercent;      ///< Calibrated fuel level percentage.
    uint16_t fuelFilterLifePercent; ///< Calibrated fuel filter life percentage.
    uint16_t oilLevelPercent;       ///< Calibrated oil level percentage.
    uint16_t oilFilterLifePercent;  ///< Calibrated oil filter life percentage.

    // Service counters
    uint32_t totalRunTimeHours;    ///< Total accumulated running hours of the generator.
    uint32_t lastServiceTimestamp; ///< Unix timestamp of the last service event.
    uint32_t fuelFilterHours;      ///< Hours of operation since the last fuel filter service.
    uint32_t oilFilterHours;       ///< Hours of operation since the last oil filter service.
    uint32_t oilChangeHours;       ///< Hours of operation since the last oil change.

    // Relay and digital input states
    bool relayOutputs[2];  ///< State of the two relay outputs.
    bool digitalInputs[8]; ///< State of the eight digital inputs.

    // Status flags
    bool moduleReady; ///< Flag indicating if the generator module is ready.

    // Formatted display strings
    String runTimeText;         ///< Formatted string for total run time (e.g., "XXX hours total").
    String lastServiceText;     ///< Formatted string for last service time (e.g., "X days ago").
    String fuelFilterText;      ///< Formatted string for fuel filter life (e.g., "XX% life remaining").
    String oilFilterText;       ///< Formatted string for oil filter life (e.g., "XX% life remaining").
    String fuelLevelText;       ///< Formatted string for fuel level (e.g., "XX% remaining").
    String oilLevelText;        ///< Formatted string for oil level (e.g., "XX% remaining").
    String relayStatusText;     ///< Formatted table showing the status of relays.
    String canStatusText;      ///< Formatted string for CAN connection status.

    /**
     * @brief Creates a GeneratorStatus object from CAN protocol data.
     * @param canProtocol The CAN protocol data.
     * @param canModuleReady The status of the CAN module.
     * @return A populated GeneratorStatus object.
     */
    static GeneratorStatus fromCanData(
        const CanGeneratorProtocol& canProtocol,
        bool canModuleReady
    );

    /**
     * @brief Creates a GeneratorStatus object from raw data (legacy method).
     * @deprecated This method is for backward compatibility and may be removed.
     * @return A populated GeneratorStatus object.
     */
    static GeneratorStatus fromRawData(
        uint16_t fuelRaw, uint16_t fuelFilterRaw, uint16_t oilRaw, uint16_t oilFilterRaw,
        bool relays[2], bool inputs[8], bool ready, uint32_t runTimeHours, uint32_t lastServiceTs,
        uint32_t fuelFilterHrs, uint32_t oilFilterHrs, uint32_t oilChangeHrs
    );
};

/**
 * @struct GeneratorCalibration
 * @brief Holds the calibration settings for the generator's sensors and service intervals.
 *
 * This struct defines the 5-point calibration tables for various sensors and the
 * service interval durations in hours.
 */
struct GeneratorCalibration {
    // Sensor calibration points (raw values for 0%, 25%, 50%, 75%, 100%)
    uint16_t fuelLevelCal[5];      ///< Calibration points for fuel level sensor.
    uint16_t fuelFilterCal[5];     ///< Calibration points for fuel filter life sensor.
    uint16_t oilLevelCal[5];       ///< Calibration points for oil level sensor.
    uint16_t oilFilterCal[5];      ///< Calibration points for oil filter life sensor.

    // Service intervals (in hours)
    uint32_t fuelFilterIntervalHours; ///< Service interval for the fuel filter.
    uint32_t oilFilterIntervalHours;  ///< Service interval for the oil filter.
    uint32_t oilChangeIntervalHours;  ///< Service interval for oil changes.

    /**
     * @brief Gets a default set of calibration values.
     * @return A GeneratorCalibration struct with default settings.
     */
    static GeneratorCalibration getDefaults();

    /**
     * @brief Calibrates a raw sensor value using a 5-point table.
     * @param rawValue The raw sensor value.
     * @param calPoints The 5-point calibration table.
     * @return The calibrated value as a percentage (0-100).
     */
    uint16_t calibrateSensor(uint16_t rawValue, const uint16_t calPoints[5]) const;
};