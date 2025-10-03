#pragma once

#include <Arduino.h>
#include <stdint.h>

// CANBUS Protocol for Generator Data
// Standard CAN IDs (11-bit) for generator communication

// Generator Status Messages (0x100-0x1FF range)
#define CAN_ID_GENERATOR_STATUS      0x100  // Overall status
#define CAN_ID_GENERATOR_SENSORS     0x101  // Sensor readings
#define CAN_ID_GENERATOR_RUNTIME     0x102  // Runtime counters
#define CAN_ID_GENERATOR_RELAYS      0x103  // Relay states
#define CAN_ID_GENERATOR_FILTER_HOURS 0x105  // Filter service hours
#define CAN_ID_GENERATOR_INPUTS      0x104  // Digital inputs

// Generator Control Messages (0x200-0x2FF range)
#define CAN_ID_GENERATOR_CONTROL     0x200  // Control commands
#define CAN_ID_GENERATOR_CALIBRATE   0x201  // Calibration data

// Generator Data Structures for CAN messages

// CAN Message: Generator Sensors (ID: 0x101)
// Data: [fuel_level(2), fuel_filter(2), oil_level(2), oil_filter(2)]
// All values are raw ADC readings (0-1023)
struct CanGeneratorSensors {
    uint16_t fuelLevel;      // Raw ADC value 0-1023
    uint16_t fuelFilter;     // Raw ADC value 0-1023
    uint16_t oilLevel;       // Raw ADC value 0-1023
    uint16_t oilFilter;      // Raw ADC value 0-1023
};

// CAN Message: Generator Runtime (ID: 0x102)
// Data: [total_hours(4), last_service(4)]
struct CanGeneratorRuntime {
    uint32_t totalRunTimeHours;
    uint32_t lastServiceTimestamp;
};

// CAN Message: Generator Relays (ID: 0x103)
// Data: [relay_states(1), digital_inputs_low(1), digital_inputs_high(1)]
// relay_states: bit 0=R0, bit 1=R1
// digital_inputs_low: bits 0-7 = DI0-DI7
// digital_inputs_high: bits 0-7 = DI8-DI15 (if needed)
struct CanGeneratorRelays {
    uint8_t relayStates;        // Bitfield: bit0=R0, bit1=R1
    uint8_t digitalInputsLow;   // DI0-DI7 states
    uint8_t digitalInputsHigh;  // DI8-DI15 states (reserved)
};

// CAN Message: Generator Filter Hours (ID: 0x105)
// Data: [fuel_filter_hours(4), oil_filter_hours(4), oil_change_hours(4)]
struct CanGeneratorFilterHours {
    uint32_t fuelFilterHours;
    uint32_t oilFilterHours;
    uint32_t oilChangeHours;
};

// Generator CAN Protocol Handler Class
class CanGeneratorProtocol {
private:
    // Latest received data with timestamps
    CanGeneratorSensors lastSensors = {0};
    CanGeneratorRuntime lastRuntime = {0};
    CanGeneratorRelays lastRelays = {0};
    CanGeneratorFilterHours lastFilterHours = {0};

    uint32_t lastSensorsTime = 0;
    uint32_t lastRuntimeTime = 0;
    uint32_t lastRelaysTime = 0;
    uint32_t lastFilterHoursTime = 0;

    uint32_t timeoutMs = 5000; // 5 second timeout for stale data

public:
    CanGeneratorProtocol();

    // Process incoming CAN messages
    bool processMessage(uint32_t canId, const uint8_t* data, uint8_t length);

    // Get latest data (with timeout checking)
    bool getSensors(CanGeneratorSensors& sensors) const;
    bool getRuntime(CanGeneratorRuntime& runtime) const;
    bool getRelays(CanGeneratorRelays& relays) const;
    bool getFilterHours(CanGeneratorFilterHours& filterHours) const;

    // Check if data is fresh (within timeout)
    bool isSensorsFresh() const;
    bool isRuntimeFresh() const;
    bool isRelaysFresh() const;
    bool isFilterHoursFresh() const;

    // Get data age in milliseconds
    uint32_t getSensorsAge() const;
    uint32_t getRuntimeAge() const;
    uint32_t getRelaysAge() const;
    uint32_t getFilterHoursAge() const;
};
