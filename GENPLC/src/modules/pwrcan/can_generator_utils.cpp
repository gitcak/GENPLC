#include "can_generator_utils.h"
#include "pwrcan_module.h"

bool sendGeneratorSensors(PWRCANModule& canModule, const CanGeneratorSensors& sensors) {
    uint8_t data[8];
    uint8_t length;
    packGeneratorSensors(sensors, data, length);
    return canModule.sendFrame(CAN_ID_GENERATOR_SENSORS, data, length);
}

bool sendGeneratorRuntime(PWRCANModule& canModule, const CanGeneratorRuntime& runtime) {
    uint8_t data[8];
    uint8_t length;
    packGeneratorRuntime(runtime, data, length);
    return canModule.sendFrame(CAN_ID_GENERATOR_RUNTIME, data, length);
}

bool sendGeneratorRelays(PWRCANModule& canModule, const CanGeneratorRelays& relays) {
    uint8_t data[8];
    uint8_t length;
    packGeneratorRelays(relays, data, length);
    return canModule.sendFrame(CAN_ID_GENERATOR_RELAYS, data, length);
}

bool sendGeneratorFilterHours(PWRCANModule& canModule, const CanGeneratorFilterHours& filterHours) {
    uint8_t data[8];
    uint8_t length;
    packGeneratorFilterHours(filterHours, data, length);
    return canModule.sendFrame(CAN_ID_GENERATOR_FILTER_HOURS, data, length);
}

bool sendGeneratorStatus(PWRCANModule& canModule, const GeneratorStatus& status) {
    // Send all generator data messages
    CanGeneratorSensors sensors = {
        status.fuelLevelRaw,
        status.fuelFilterRaw,
        status.oilLevelRaw,
        status.oilFilterRaw
    };

    CanGeneratorRuntime runtime = {
        status.totalRunTimeHours,
        status.lastServiceTimestamp
    };

    CanGeneratorRelays relays = {0};
    relays.relayStates = (status.relayOutputs[1] << 1) | (status.relayOutputs[0] << 0);
    for (int i = 0; i < 8; i++) {
        if (status.digitalInputs[i]) {
            relays.digitalInputsLow |= (1 << i);
        }
    }

    CanGeneratorFilterHours filterHours = {
        status.fuelFilterHours,
        status.oilFilterHours,
        status.oilChangeHours
    };

    // Send all messages
    bool success = true;
    success &= sendGeneratorSensors(canModule, sensors);
    success &= sendGeneratorRuntime(canModule, runtime);
    success &= sendGeneratorRelays(canModule, relays);
    success &= sendGeneratorFilterHours(canModule, filterHours);

    return success;
}

void packGeneratorSensors(const CanGeneratorSensors& sensors, uint8_t* data, uint8_t& length) {
    // Pack 4 uint16_t values (8 bytes total)
    data[0] = sensors.fuelLevel & 0xFF;
    data[1] = (sensors.fuelLevel >> 8) & 0xFF;
    data[2] = sensors.fuelFilter & 0xFF;
    data[3] = (sensors.fuelFilter >> 8) & 0xFF;
    data[4] = sensors.oilLevel & 0xFF;
    data[5] = (sensors.oilLevel >> 8) & 0xFF;
    data[6] = sensors.oilFilter & 0xFF;
    data[7] = (sensors.oilFilter >> 8) & 0xFF;
    length = 8;
}

void packGeneratorRuntime(const CanGeneratorRuntime& runtime, uint8_t* data, uint8_t& length) {
    // Pack 2 uint32_t values (8 bytes total)
    data[0] = runtime.totalRunTimeHours & 0xFF;
    data[1] = (runtime.totalRunTimeHours >> 8) & 0xFF;
    data[2] = (runtime.totalRunTimeHours >> 16) & 0xFF;
    data[3] = (runtime.totalRunTimeHours >> 24) & 0xFF;
    data[4] = runtime.lastServiceTimestamp & 0xFF;
    data[5] = (runtime.lastServiceTimestamp >> 8) & 0xFF;
    data[6] = (runtime.lastServiceTimestamp >> 16) & 0xFF;
    data[7] = (runtime.lastServiceTimestamp >> 24) & 0xFF;
    length = 8;
}

void packGeneratorRelays(const CanGeneratorRelays& relays, uint8_t* data, uint8_t& length) {
    // Pack relay states and digital inputs
    data[0] = relays.relayStates;
    data[1] = relays.digitalInputsLow;
    data[2] = relays.digitalInputsHigh;
    length = 3;
}

void packGeneratorFilterHours(const CanGeneratorFilterHours& filterHours, uint8_t* data, uint8_t& length) {
    // Pack 3 uint32_t values into 8 bytes (truncated for CAN limits)
    // Note: CAN messages are limited to 8 bytes, so we pack the most important data
    // fuelFilterHours (4 bytes) + oilFilterHours (2 bytes) + oilChangeHours (2 bytes high bits)
    data[0] = filterHours.fuelFilterHours & 0xFF;
    data[1] = (filterHours.fuelFilterHours >> 8) & 0xFF;
    data[2] = (filterHours.fuelFilterHours >> 16) & 0xFF;
    data[3] = (filterHours.fuelFilterHours >> 24) & 0xFF;
    data[4] = filterHours.oilFilterHours & 0xFF;
    data[5] = (filterHours.oilFilterHours >> 8) & 0xFF;
    data[6] = filterHours.oilChangeHours & 0xFF;
    data[7] = (filterHours.oilChangeHours >> 8) & 0xFF;
    length = 8;
}
