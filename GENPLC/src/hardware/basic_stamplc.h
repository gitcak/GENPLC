#ifndef BASIC_STAMPLC_H
#define BASIC_STAMPLC_H

#include <Arduino.h>
#include <M5Unified.h>
#include <M5StamPLC.h>

/**
 * @class BasicStampPLC
 * @brief Provides a simplified interface for the M5Stack StampPLC.
 *
 * This class abstracts the core functionalities of the StampPLC, including
 * digital inputs, analog inputs, and relay outputs. It simplifies initialization
 * and state management, making it easier to integrate into larger applications.
 */
class BasicStampPLC {
private:
    m5::M5_STAMPLC stamPLC; ///< Instance of the M5_STAMPLC library.
    bool isInitialized;     ///< Flag indicating if the PLC has been initialized.

    // State variables
    bool digitalInputs[8];      ///< Array to store the state of 8 digital inputs.
    uint16_t analogInputs[4];   ///< Array to store the values of 4 analog inputs.
    bool relayOutputs[2];       ///< Array to store the state of 2 relay outputs.

public:
    /**
     * @brief Construct a new BasicStampPLC object.
     */
    BasicStampPLC();

    /**
     * @brief Initializes the StampPLC hardware.
     * @return True if initialization is successful, false otherwise.
     */
    bool begin();

    /**
     * @brief Updates the state of all inputs and outputs.
     * This function should be called periodically in the main loop.
     */
    void update();

    /**
     * @brief Gets the state of a specific digital input channel.
     * @param channel The digital input channel to read (0-7).
     * @return The state of the digital input (true for HIGH, false for LOW).
     */
    bool getDigitalInput(uint8_t channel);

    /**
     * @brief Gets the value of a specific analog input channel.
     * @param channel The analog input channel to read (0-3).
     * @return The 16-bit analog value.
     */
    uint16_t getAnalogInput(uint8_t channel);

    /**
     * @brief Sets the state of a specific relay output channel.
     * @param channel The relay output channel to control (0-1).
     * @param state The desired state (true for ON, false for OFF).
     * @return True if the state was set successfully, false otherwise.
     */
    bool setRelayOutput(uint8_t channel, bool state);

    /**
     * @brief Gets the current state of a specific relay output channel.
     * @param channel The relay output channel to query (0-1).
     * @return The current state of the relay (true for ON, false for OFF).
     */
    bool getRelayOutput(uint8_t channel);

    /**
     * @brief Prints the current status of all inputs and outputs to the serial monitor.
     */
    void printStatus();

    /**
     * @brief Checks if the StampPLC is initialized and ready.
     * @return True if ready, false otherwise.
     */
    bool isReady() { return isInitialized; }

    /**
     * @brief Provides direct access to the underlying M5_STAMPLC object.
     * @return A pointer to the M5_STAMPLC instance for advanced operations.
     */
    m5::M5_STAMPLC* getStamPLC() { return &stamPLC; }
};

#endif // BASIC_STAMPLC_H