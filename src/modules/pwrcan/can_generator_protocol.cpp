#include "can_generator_protocol.h"

CanGeneratorProtocol::CanGeneratorProtocol() {
    // Initialize with zeros
    lastSensors = {0};
    lastRuntime = {0};
    lastRelays = {0};
    lastFilterHours = {0};
}

bool CanGeneratorProtocol::processMessage(uint32_t canId, const uint8_t* data, uint8_t length) {
    uint32_t now = millis();

    switch (canId) {
        case CAN_ID_GENERATOR_SENSORS:
            if (length >= 8) { // 4 x uint16_t = 8 bytes
                lastSensors.fuelLevel = (data[1] << 8) | data[0];
                lastSensors.fuelFilter = (data[3] << 8) | data[2];
                lastSensors.oilLevel = (data[5] << 8) | data[4];
                lastSensors.oilFilter = (data[7] << 8) | data[6];
                lastSensorsTime = now;
                return true;
            }
            break;

        case CAN_ID_GENERATOR_RUNTIME:
            if (length >= 8) { // 2 x uint32_t = 8 bytes
                lastRuntime.totalRunTimeHours =
                    (data[3] << 24) | (data[2] << 16) | (data[1] << 8) | data[0];
                lastRuntime.lastServiceTimestamp =
                    (data[7] << 24) | (data[6] << 16) | (data[5] << 8) | data[4];
                lastRuntimeTime = now;
                return true;
            }
            break;

        case CAN_ID_GENERATOR_RELAYS:
            if (length >= 3) { // 3 bytes minimum
                lastRelays.relayStates = data[0];
                lastRelays.digitalInputsLow = data[1];
                lastRelays.digitalInputsHigh = (length >= 3) ? data[2] : 0;
                lastRelaysTime = now;
                return true;
            }
            break;

        case CAN_ID_GENERATOR_FILTER_HOURS:
            if (length >= 8) { // Packed filter hours (8 bytes)
                lastFilterHours.fuelFilterHours =
                    (data[3] << 24) | (data[2] << 16) | (data[1] << 8) | data[0];
                lastFilterHours.oilFilterHours =
                    (data[5] << 8) | data[4];  // Only 16 bits available
                lastFilterHours.oilChangeHours =
                    (data[7] << 8) | data[6];  // Only 16 bits available
                lastFilterHoursTime = now;
                return true;
            }
            break;

        default:
            // Unknown CAN ID - ignore
            break;
    }

    return false;
}

bool CanGeneratorProtocol::getSensors(CanGeneratorSensors& sensors) const {
    if (isSensorsFresh()) {
        sensors = lastSensors;
        return true;
    }
    return false;
}

bool CanGeneratorProtocol::getRuntime(CanGeneratorRuntime& runtime) const {
    if (isRuntimeFresh()) {
        runtime = lastRuntime;
        return true;
    }
    return false;
}

bool CanGeneratorProtocol::getRelays(CanGeneratorRelays& relays) const {
    if (isRelaysFresh()) {
        relays = lastRelays;
        return true;
    }
    return false;
}

bool CanGeneratorProtocol::getFilterHours(CanGeneratorFilterHours& filterHours) const {
    if (isFilterHoursFresh()) {
        filterHours = lastFilterHours;
        return true;
    }
    return false;
}

bool CanGeneratorProtocol::isSensorsFresh() const {
    return (millis() - lastSensorsTime) < timeoutMs;
}

bool CanGeneratorProtocol::isRuntimeFresh() const {
    return (millis() - lastRuntimeTime) < timeoutMs;
}

bool CanGeneratorProtocol::isRelaysFresh() const {
    return (millis() - lastRelaysTime) < timeoutMs;
}

bool CanGeneratorProtocol::isFilterHoursFresh() const {
    return (millis() - lastFilterHoursTime) < timeoutMs;
}

uint32_t CanGeneratorProtocol::getSensorsAge() const {
    return millis() - lastSensorsTime;
}

uint32_t CanGeneratorProtocol::getRuntimeAge() const {
    return millis() - lastRuntimeTime;
}

uint32_t CanGeneratorProtocol::getRelaysAge() const {
    return millis() - lastRelaysTime;
}

uint32_t CanGeneratorProtocol::getFilterHoursAge() const {
    return millis() - lastFilterHoursTime;
}
