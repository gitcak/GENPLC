#include "basic_stamplc.h"

/**
 * @brief Construct a new BasicStampPLC::BasicStampPLC object.
 * Initializes the isInitialized flag to false.
 */
BasicStampPLC::BasicStampPLC() {
    isInitialized = false;
}

/**
 * @brief Initializes the M5Stack StampPLC.
 * This function sets up the StampPLC configuration, disables the SD card to avoid conflicts,
 * and initializes the internal state for digital inputs, analog inputs, and relays.
 * @return true if initialization is successful.
 */
bool BasicStampPLC::begin() {
    // Initialize the M5StamPLC library
    // IMPORTANT: We don't call M5.begin() here as it's already called in the main setup
    // Disable SD card support here to avoid conflicts with SDCardModule
    // SD card will be handled by dedicated SDCardModule
    {
        auto cfg = stamPLC.config();
        cfg.enableSdCard = false; // Let SDCardModule handle SD card
        stamPLC.config(cfg);
    }
    stamPLC.begin();

    // Initialize digital inputs and relay outputs
    for (int i = 0; i < 8; i++) {
        digitalInputs[i] = false;
    }

    for (int i = 0; i < 4; i++) {
        analogInputs[i] = 0;
    }

    for (int i = 0; i < 2; i++) {
        relayOutputs[i] = false;
    }

    isInitialized = true;
    return true;
}

/**
 * @brief Updates the state of the StampPLC's inputs.
 * This function reads the current state of the digital and analog inputs
 * and updates the internal state variables. It should be called periodically.
 */
void BasicStampPLC::update() {
    if (!isInitialized) return;

    // Ensure underlying StamPLC subsystem (incl. buttons via IO expander) is updated
    stamPLC.update();

    // Read digital inputs
    for (int i = 0; i < 8; i++) {
        digitalInputs[i] = stamPLC.readPlcInput(i);
    }

    // Read analog inputs (simplified for now)
    for (int i = 0; i < 4; i++) {
        analogInputs[i] = stamPLC.readPlcInput(i) ? 1023 : 0;
    }

}

/**
 * @brief Gets the state of a specific digital input.
 * @param channel The channel of the digital input to read (0-7).
 * @return The state of the input (true for HIGH, false for LOW). Returns false if the channel is invalid.
 */
bool BasicStampPLC::getDigitalInput(uint8_t channel) {
    if (channel >= 8) return false;
    return digitalInputs[channel];
}

/**
 * @brief Gets the value of a specific analog input.
 * @param channel The channel of the analog input to read (0-3).
 * @return The 16-bit value of the analog input. Returns 0 if the channel is invalid.
 */
uint16_t BasicStampPLC::getAnalogInput(uint8_t channel) {
    if (channel >= 4) return 0;
    return analogInputs[channel];
}

/**
 * @brief Sets the state of a specific relay output.
 * @param channel The channel of the relay to set (0-1).
 * @param state The desired state (true for ON, false for OFF).
 * @return true if the relay state was set successfully, false if the channel is invalid.
 */
bool BasicStampPLC::setRelayOutput(uint8_t channel, bool state) {
    if (channel >= 2) return false;

    // Set relay state
    relayOutputs[channel] = state;

    // Update physical relay
    stamPLC.writePlcRelay(channel, state);

    return true;
}

/**
 * @brief Gets the current state of a specific relay output.
 * @param channel The channel of the relay to read (0-1).
 * @return The current state of the relay (true for ON, false for OFF). Returns false if the channel is invalid.
 */
bool BasicStampPLC::getRelayOutput(uint8_t channel) {
    if (channel >= 2) return false;
    return relayOutputs[channel];
}

/**
 * @brief Prints the current status of all inputs and outputs to the Serial monitor.
 * This is useful for debugging purposes.
 */
void BasicStampPLC::printStatus() {
    if (!isInitialized) {
        Serial.println("StampPLC: Not initialized");
        return;
    }

    // Print digital inputs
    Serial.print("StampPLC: Digital inputs: ");
    for (int i = 0; i < 8; i++) {
        Serial.print(digitalInputs[i] ? "1" : "0");
    }
    Serial.println();

    // Print analog inputs
    for (int i = 0; i < 4; i++) {
        Serial.printf("StampPLC: Analog %d = %d\n", i, analogInputs[i]);
    }

    // Print relay outputs
    Serial.print("StampPLC: Relay outputs: ");
    for (int i = 0; i < 2; i++) {
        Serial.print(relayOutputs[i] ? "1" : "0");
    }
    Serial.println();
}