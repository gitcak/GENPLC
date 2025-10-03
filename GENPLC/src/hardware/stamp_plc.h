/*
 * StampPLC Hardware Abstraction Layer
 * FreeRTOS-safe industrial I/O management with navigation system
 */

#ifndef STAMP_PLC_H
#define STAMP_PLC_H

#include <Arduino.h>
#include <M5StamPLC.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/queue.h>
#include <freertos/task.h>

/**
 * @file stamp_plc.h
 * @brief A thread-safe hardware abstraction layer for the M5Stack StampPLC.
 *
 * This file defines the classes and data structures for managing the StampPLC
 * in a FreeRTOS environment, including I/O control and a simple UI navigation system.
 */

// Forward declarations
struct PLCData;
struct PLCNavigationState;

// ============================================================================
// PLC DATA STRUCTURES
// ============================================================================

/**
 * @struct PLCData
 * @brief Holds a snapshot of the PLC's I/O and sensor data.
 *
 * This struct is used to store the state of digital inputs, relay outputs,
 * and analog sensor values at a specific point in time.
 */
struct PLCData {
    // Digital inputs (8 channels)
    bool digitalInputs[8]; ///< State of the 8 digital inputs.

    // Relay outputs (4 channels)
    bool relayOutputs[4]; ///< State of the 4 relay outputs.

    // Analog sensor values
    float voltage;      ///< Measured voltage.
    float current;      ///< Measured current.
    float temperature;  ///< Measured temperature.

    // Timestamp
    unsigned long timestamp; ///< Timestamp when the data was captured (millis).

    /**
     * @brief Construct a new PLCData object and initialize all values to zero/false.
     */
    PLCData() {
        memset(digitalInputs, 0, sizeof(digitalInputs));
        memset(relayOutputs, 0, sizeof(relayOutputs));
        voltage = 0.0f;
        current = 0.0f;
        temperature = 0.0f;
        timestamp = 0;
    }
};

/**
 * @struct PLCNavigationState
 * @brief Manages the state of the user interface navigation.
 *
 * This struct tracks the currently displayed screen, the total number of screens,
 * and user activity for features like automatic screen return.
 */
struct PLCNavigationState {
    uint8_t currentScreen;      ///< The index of the currently displayed screen.
    uint8_t maxScreens;         ///< The total number of screens available for navigation.
    unsigned long lastActivity; ///< Timestamp of the last user interaction.
    bool autoReturnEnabled;     ///< Flag to enable/disable automatic return to a default screen.

    /**
     * @brief Construct a new PLCNavigationState object with default values.
     */
    PLCNavigationState() {
        currentScreen = 0;
        maxScreens = 4;
        lastActivity = 0;
        autoReturnEnabled = true;
    }
};

// ============================================================================
// STAMP PLC CLASS
// ============================================================================

/**
 * @class StampPLC
 * @brief A thread-safe hardware abstraction layer for the M5Stack StampPLC.
 *
 * This class provides a high-level interface for managing the StampPLC's I/O
 * in a FreeRTOS environment. It includes features for thread-safe data access,
 * a simple navigation system for a display, and a dedicated FreeRTOS task
 * for handling hardware updates.
 */
class StampPLC {
private:
    // M5Stack StampPLC instance
    m5::M5_STAMPLC* stamPLC; ///< Pointer to the underlying M5_STAMPLC hardware instance.

    // FreeRTOS synchronization
    SemaphoreHandle_t xSemaphorePLC; ///< Semaphore for thread-safe access to PLC hardware.
    QueueHandle_t xQueuePLCData;     ///< Queue for passing PLC data between tasks.

    // Navigation state
    PLCNavigationState navigationState; ///< Holds the current state of the UI navigation.

    // Data cache
    PLCData currentData;  ///< Cache for the most recent PLC data.
    PLCData previousData; ///< Cache for the previous PLC data, for change detection.

    // Private methods
    /**
     * @brief Initializes the underlying hardware components of the StampPLC.
     * @return True if hardware initialization is successful, false otherwise.
     */
    bool initializeHardware();

    /**
     * @brief Reads and updates the state of all digital inputs.
     */
    void updateDigitalInputs();

    /**
     * @brief Reads and updates the values from analog sensors.
     */
    void updateAnalogSensors();

    /**
     * @brief Updates the physical relay outputs based on the current state.
     */
    void updateRelayOutputs();

    /**
     * @brief Manages the UI navigation logic, including screen changes.
     */
    void handleNavigation();

    /**
     * @brief Draws the main navigation screen.
     */
    void drawNavigationScreen();

    /**
     * @brief Draws the screen displaying digital input statuses.
     */
    void drawDigitalInputScreen();

    /**
     * @brief Draws the screen displaying relay output statuses.
     */
    void drawRelayOutputScreen();

    /**
     * @brief Draws the screen displaying analog sensor values.
     */
    void drawAnalogSensorScreen();

    /**
     * @brief Draws a configuration screen.
     */
    void drawConfigurationScreen();

public:
    /**
     * @brief Construct a new StampPLC object.
     */
    StampPLC();

    /**
     * @brief Destroy the StampPLC object and clean up resources.
     */
    ~StampPLC();

    /**
     * @brief Initializes the StampPLC, including hardware and FreeRTOS components.
     * @return True if initialization is successful, false otherwise.
     */
    bool begin();

    /**
     * @brief Shuts down the PLC and releases resources.
     */
    void shutdown();

    // Data access (thread-safe)
    /**
     * @brief Gets the state of a specific digital input channel.
     * @param channel The input channel (0-7).
     * @return The state of the input (true for HIGH, false for LOW).
     */
    bool getDigitalInput(uint8_t channel);

    /**
     * @brief Sets the state of a specific relay output channel.
     * @param channel The output channel (0-3).
     * @param state The desired state (true for ON, false for OFF).
     * @return True if the state was set successfully, false otherwise.
     */
    bool setRelayOutput(uint8_t channel, bool state);

    /**
     * @brief Gets the last measured voltage.
     * @return The voltage value.
     */
    float getVoltage();

    /**
     * @brief Gets the last measured current.
     * @return The current value.
     */
    float getCurrent();

    /**
     * @brief Gets the last measured temperature.
     * @return The temperature value.
     */
    float getTemperature();

    /**
     * @brief Gets a copy of the most recent PLC data.
     * @return A PLCData struct with the latest data.
     */
    PLCData getCurrentData();

    // Navigation system
    /**
     * @brief Navigates to the next screen in the sequence.
     */
    void navigateNext();

    /**
     * @brief Navigates to the previous screen in the sequence.
     */
    void navigatePrevious();

    /**
     * @brief Jumps directly to a specific screen.
     * @param screen The index of the screen to navigate to.
     */
    void navigateToScreen(uint8_t screen);

    /**
     * @brief Handles a button press event for navigation.
     * @param button The identifier of the button that was pressed.
     */
    void handleButtonPress(uint8_t button);

    /**
     * @brief Updates the navigation state, checking for timeouts.
     */
    void updateNavigation();

    /**
     * @brief Updates the internal state of the buttons.
     */
    void updateButtonStates();

    // Status
    /**
     * @brief Checks if the PLC is ready and operational.
     * @return True if ready, false otherwise.
     */
    bool isReady();

    /**
     * @brief Checks if the PLC has been initialized.
     * @return True if initialized, false otherwise.
     */
    bool isInitialized();

    /**
     * @brief Prints the current status of the PLC to the serial monitor.
     */
    void printStatus();

    // FreeRTOS integration
    /**
     * @brief The main function for the dedicated PLC FreeRTOS task.
     */
    void taskFunction();

    /**
     * @brief A static wrapper to launch the taskFunction for FreeRTOS.
     * @param pvParameters A pointer to the StampPLC instance.
     */
    static void taskWrapper(void* pvParameters);
};

#endif // STAMP_PLC_H