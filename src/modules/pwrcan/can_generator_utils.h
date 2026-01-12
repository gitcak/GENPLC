#pragma once

#include <Arduino.h>
#include "can_generator_protocol.h"

// Forward declaration
class PWRCANModule;
#include "../../hardware/generator_status.h"

/**
 * CAN Generator Utilities - Helper functions for sending generator data over CAN
 */

// Send generator sensor data over CAN
bool sendGeneratorSensors(PWRCANModule& canModule, const CanGeneratorSensors& sensors);

// Send generator runtime data over CAN
bool sendGeneratorRuntime(PWRCANModule& canModule, const CanGeneratorRuntime& runtime);

// Send generator relay/input data over CAN
bool sendGeneratorRelays(PWRCANModule& canModule, const CanGeneratorRelays& relays);

// Send generator filter hours over CAN
bool sendGeneratorFilterHours(PWRCANModule& canModule, const CanGeneratorFilterHours& filterHours);

// Send complete generator status over CAN (all messages)
bool sendGeneratorStatus(PWRCANModule& canModule, const GeneratorStatus& status);

// Pack data structures into CAN message format
void packGeneratorSensors(const CanGeneratorSensors& sensors, uint8_t* data, uint8_t& length);
void packGeneratorRuntime(const CanGeneratorRuntime& runtime, uint8_t* data, uint8_t& length);
void packGeneratorRelays(const CanGeneratorRelays& relays, uint8_t* data, uint8_t& length);
void packGeneratorFilterHours(const CanGeneratorFilterHours& filterHours, uint8_t* data, uint8_t& length);
