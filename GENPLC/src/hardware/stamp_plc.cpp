/**
 * @file stamp_plc.cpp
 * @brief Implementation of the thread-safe hardware abstraction layer for the M5Stack StampPLC.
 *
 * This file provides the implementation for managing the StampPLC's I/O,
 * navigation, and FreeRTOS integration.
 */

#include "stamp_plc.h"
#include <M5Unified.h>
#include <M5GFX.h>

// ============================================================================
// CONSTRUCTOR/DESTRUCTOR
// ============================================================================

/**
 * @brief Construct a new StampPLC::StampPLC object.
 * Initializes member variables and navigation state to default values.
 */
StampPLC::StampPLC() : stamPLC(nullptr), xSemaphorePLC(nullptr), xQueuePLCData(nullptr) {
    // Initialize navigation state
    navigationState.currentScreen = 0;
    navigationState.maxScreens = 4;
    navigationState.lastActivity = 0;
    navigationState.autoReturnEnabled = true;
}

/**
 * @brief Destroy the StampPLC::StampPLC object.
 * Calls the shutdown() method to release resources.
 */
StampPLC::~StampPLC() {
    shutdown();
}

// ============================================================================
// INITIALIZATION
// ============================================================================

/**
 * @brief Initializes the StampPLC system.
 * Creates FreeRTOS synchronization primitives (semaphore and queue) and
 * initializes the underlying hardware.
 * @return true if initialization is successful, false otherwise.
 */
bool StampPLC::begin() {
    Serial.println("StampPLC: Initializing hardware...");

    // Create FreeRTOS synchronization objects
    xSemaphorePLC = xSemaphoreCreateMutex();
    if (xSemaphorePLC == nullptr) {
        Serial.println("ERROR: Failed to create PLC semaphore!");
        return false;
    }

    xQueuePLCData = xQueueCreate(5, sizeof(PLCData));
    if (xQueuePLCData == nullptr) {
        Serial.println("ERROR: Failed to create PLC data queue!");
        vSemaphoreDelete(xSemaphorePLC);
        return false;
    }

    // Initialize M5Stack StampPLC
    if (!initializeHardware()) {
        Serial.println("ERROR: Hardware initialization failed!");
        return false;
    }

    Serial.println("StampPLC: Initialization complete");
    return true;
}

/**
 * @brief Shuts down the StampPLC system.
 * Deletes the FreeRTOS semaphore and queue to free up resources.
 */
void StampPLC::shutdown() {
    if (xSemaphorePLC != nullptr) {
        vSemaphoreDelete(xSemaphorePLC);
        xSemaphorePLC = nullptr;
    }

    if (xQueuePLCData != nullptr) {
        vQueueDelete(xQueuePLCData);
        xQueuePLCData = nullptr;
    }

    Serial.println("StampPLC: Shutdown complete");
}

/**
 * @brief Initializes the low-level hardware components.
 * Sets up the M5StamPLC instance and initializes the INA226 and LM75B sensors.
 * @return true if hardware is initialized successfully.
 */
bool StampPLC::initializeHardware() {
    // Initialize M5Stack StampPLC
    stamPLC = &M5StamPLC;

    // Call begin() but it will be safe since M5.begin() was already called
    stamPLC->begin();

    // Initialize INA226 current sensor
    if (!stamPLC->INA226.begin()) {
        Serial.println("WARNING: INA226 initialization failed");
    }

    // Initialize LM75B temperature sensor
    if (!stamPLC->LM75B.begin()) {
        Serial.println("WARNING: LM75B initialization failed");
    }

    Serial.println("StampPLC: Hardware initialized successfully");
    return true;
}

// ============================================================================
// DATA ACCESS (THREAD-SAFE)
// ============================================================================

/**
 * @brief Gets the state of a digital input in a thread-safe manner.
 * @param channel The input channel to read (0-7).
 * @return The state of the input (true/false), or false on error.
 */
bool StampPLC::getDigitalInput(uint8_t channel) {
    if (channel >= 8) return false;

    if (xSemaphoreTake(xSemaphorePLC, pdMS_TO_TICKS(100)) == pdTRUE) {
        bool value = currentData.digitalInputs[channel];
        xSemaphoreGive(xSemaphorePLC);
        return value;
    }
    return false;
}

/**
 * @brief Sets the state of a relay output in a thread-safe manner.
 * @param channel The relay channel to set (0-3).
 * @param state The desired state (true for ON, false for OFF).
 * @return true if the state was set successfully, false otherwise.
 */
bool StampPLC::setRelayOutput(uint8_t channel, bool state) {
    if (channel >= 4) return false;

    if (xSemaphoreTake(xSemaphorePLC, pdMS_TO_TICKS(100)) == pdTRUE) {
        // Set relay state
        stamPLC->writePlcRelay(channel, state);
        currentData.relayOutputs[channel] = state;
        xSemaphoreGive(xSemaphorePLC);
        return true;
    }
    return false;
}

/**
 * @brief Gets the last read voltage value in a thread-safe manner.
 * @return The voltage, or 0.0f on error.
 */
float StampPLC::getVoltage() {
    if (xSemaphoreTake(xSemaphorePLC, pdMS_TO_TICKS(100)) == pdTRUE) {
        float value = currentData.voltage;
        xSemaphoreGive(xSemaphorePLC);
        return value;
    }
    return 0.0f;
}

/**
 * @brief Gets the last read current value in a thread-safe manner.
 * @return The current in mA, or 0.0f on error.
 */
float StampPLC::getCurrent() {
    if (xSemaphoreTake(xSemaphorePLC, pdMS_TO_TICKS(100)) == pdTRUE) {
        float value = currentData.current;
        xSemaphoreGive(xSemaphorePLC);
        return value;
    }
    return 0.0f;
}

/**
 * @brief Gets the last read temperature value in a thread-safe manner.
 * @return The temperature in Celsius, or 0.0f on error.
 */
float StampPLC::getTemperature() {
    if (xSemaphoreTake(xSemaphorePLC, pdMS_TO_TICKS(100)) == pdTRUE) {
        float value = currentData.temperature;
        xSemaphoreGive(xSemaphorePLC);
        return value;
    }
    return 0.0f;
}

/**
 * @brief Gets a copy of the current PLC data snapshot in a thread-safe manner.
 * @return A PLCData struct. The struct will be zeroed out on error.
 */
PLCData StampPLC::getCurrentData() {
    PLCData data;
    if (xSemaphoreTake(xSemaphorePLC, pdMS_TO_TICKS(100)) == pdTRUE) {
        data = currentData;
        xSemaphoreGive(xSemaphorePLC);
    }
    return data;
}

// ============================================================================
// NAVIGATION SYSTEM
// ============================================================================

/**
 * @brief Navigates to the next screen.
 */
void StampPLC::navigateNext() {
    if (xSemaphoreTake(xSemaphorePLC, pdMS_TO_TICKS(100)) == pdTRUE) {
        navigationState.currentScreen = (navigationState.currentScreen + 1) % navigationState.maxScreens;
        navigationState.lastActivity = millis();
        xSemaphoreGive(xSemaphorePLC);
    }
}

/**
 * @brief Navigates to the previous screen.
 */
void StampPLC::navigatePrevious() {
    if (xSemaphoreTake(xSemaphorePLC, pdMS_TO_TICKS(100)) == pdTRUE) {
        navigationState.currentScreen = (navigationState.currentScreen - 1 + navigationState.maxScreens) % navigationState.maxScreens;
        navigationState.lastActivity = millis();
        xSemaphoreGive(xSemaphorePLC);
    }
}

/**
 * @brief Navigates to a specific screen index.
 * @param screen The screen index to navigate to.
 */
void StampPLC::navigateToScreen(uint8_t screen) {
    if (screen >= navigationState.maxScreens) return;

    if (xSemaphoreTake(xSemaphorePLC, pdMS_TO_TICKS(100)) == pdTRUE) {
        navigationState.currentScreen = screen;
        navigationState.lastActivity = millis();
        xSemaphoreGive(xSemaphorePLC);
    }
}

/**
 * @brief Handles button presses for navigation.
 * @note This function is disabled to prevent conflicts with the main button handling system.
 * @param button The button identifier.
 */
void StampPLC::handleButtonPress(uint8_t button) {
    // DISABLED: Button handling to prevent conflicts with main button system
    // Buttons are now handled exclusively by the main M5Stack task
    return;
}

/**
 * @brief Updates the navigation system.
 * @note This function is disabled to prevent this module from controlling the display.
 */
void StampPLC::updateNavigation() {
    // DISABLED: Navigation system to prevent display conflicts
    // This prevents StampPLC from taking over the display
    return;
}

/**
 * @brief Updates the button states by calling the underlying library's update method.
 */
void StampPLC::updateButtonStates() {
    if (stamPLC != nullptr) {
        stamPLC->update();
    }
}

// ============================================================================
// DISPLAY FUNCTIONS
// ============================================================================

/**
 * @brief Draws the navigation screen content.
 * @note Simplified to print to Serial to avoid complex display logic and potential stack overflows.
 */
void StampPLC::drawNavigationScreen() {
    // This will be implemented with M5GFX
    // SIMPLIFIED - use basic Serial.print to prevent stack overflow
    Serial.print("StampPLC: Displaying screen ");
    Serial.print(navigationState.currentScreen + 1);
    Serial.print("/");
    Serial.println(navigationState.maxScreens);
}

/**
 * @brief Draws the digital input screen content.
 * @note Simplified to print to Serial.
 */
void StampPLC::drawDigitalInputScreen() {
    Serial.println("StampPLC: Digital Input Screen");
    Serial.println("DI0 DI1 DI2 DI3 DI4 DI5 DI6 DI7");
    // SIMPLIFIED - use basic Serial.print to prevent stack overflow
    for (int i = 0; i < 8; i++) {
        Serial.print(" ");
        Serial.print(currentData.digitalInputs[i] ? "1" : "0");
        Serial.print(" ");
    }
    Serial.println();
}

/**
 * @brief Draws the relay output screen content.
 * @note Simplified to print to Serial.
 */
void StampPLC::drawRelayOutputScreen() {
    Serial.println("StampPLC: Relay Output Screen");
    Serial.println("RO0 RO1 RO2 RO3");
    // SIMPLIFIED - use basic Serial.print to prevent stack overflow
    for (int i = 0; i < 4; i++) {
        Serial.print(" ");
        Serial.print(currentData.relayOutputs[i] ? "1" : "0");
        Serial.print(" ");
    }
    Serial.println();
}

/**
 * @brief Draws the analog sensor screen content.
 * @note Simplified to print to Serial.
 */
void StampPLC::drawAnalogSensorScreen() {
    Serial.println("StampPLC: Analog Sensor Screen");
    // SIMPLIFIED - use basic Serial.print to prevent stack overflow
    Serial.print("Voltage: ");
    Serial.print(currentData.voltage, 2);
    Serial.println("V");
    Serial.print("Current: ");
    Serial.print(currentData.current, 2);
    Serial.println("mA");
    Serial.print("Temperature: ");
    Serial.print(currentData.temperature, 1);
    Serial.println("°C");
}

/**
 * @brief Draws the configuration screen content.
 * @note Simplified to print to Serial.
 */
void StampPLC::drawConfigurationScreen() {
    Serial.println("StampPLC: Configuration Screen");
    // SIMPLIFIED - use basic Serial.print to prevent stack overflow
    Serial.print("Status: ");
    Serial.println(isReady() ? "Ready" : "Not Ready");
    Serial.print("FreeRTOS Heap: ");
    Serial.print(ESP.getFreeHeap());
    Serial.println(" bytes");
    Serial.print("Uptime: ");
    Serial.print(millis());
    Serial.println(" ms");
}

// ============================================================================
// STATUS
// ============================================================================

/**
 * @brief Checks if the PLC is fully ready for operation.
 * @return true if hardware and FreeRTOS components are initialized.
 */
bool StampPLC::isReady() {
    return (stamPLC != nullptr && xSemaphorePLC != nullptr && xQueuePLCData != nullptr);
}

/**
 * @brief Checks if the low-level hardware has been initialized.
 * @return true if the hardware pointer is not null.
 */
bool StampPLC::isInitialized() {
    return (stamPLC != nullptr);
}

/**
 * @brief Prints a summary of the PLC's status to the Serial monitor.
 */
void StampPLC::printStatus() {
    Serial.println("=== StampPLC Status ===");
    // SIMPLIFIED - use basic Serial.print to prevent stack overflow
    Serial.print("Hardware: ");
    Serial.println((stamPLC != nullptr) ? "Initialized" : "Not Initialized");
    Serial.print("Semaphore: ");
    Serial.println((xSemaphorePLC != nullptr) ? "Created" : "Not Created");
    Serial.print("Queue: ");
    Serial.println((xQueuePLCData != nullptr) ? "Created" : "Not Created");
    Serial.print("Current Screen: ");
    Serial.print(navigationState.currentScreen + 1);
    Serial.print("/");
    Serial.println(navigationState.maxScreens);
    Serial.print("Last Activity: ");
    Serial.print(millis() - navigationState.lastActivity);
    Serial.println(" ms ago");
}

// ============================================================================
// FREERTOS INTEGRATION
// ============================================================================

/**
 * @brief The main function for the dedicated PLC FreeRTOS task.
 * This function runs in an infinite loop, periodically updating sensor data.
 */
void StampPLC::taskFunction() {
    Serial.println("StampPLC: Task started");

    for (;;) {
        // Update sensor data
        updateDigitalInputs();
        updateAnalogSensors();
        updateRelayOutputs();

        // Update navigation
        updateNavigation();

        // Send data to queue if there are consumers
        if (uxQueueMessagesWaiting(xQueuePLCData) < 4) {
            xQueueSend(xQueuePLCData, &currentData, 0);
        }

        // Task delay
        vTaskDelay(pdMS_TO_TICKS(100)); // 10Hz update rate
    }
}

/**
 * @brief Static wrapper function to launch the FreeRTOS task.
 * @param pvParameters A void pointer to the StampPLC instance.
 */
void StampPLC::taskWrapper(void* pvParameters) {
    StampPLC* plc = static_cast<StampPLC*>(pvParameters);
    if (plc != nullptr) {
        plc->taskFunction();
    }
    vTaskDelete(nullptr);
}

// ============================================================================
// PRIVATE UPDATE METHODS
// ============================================================================

/**
 * @brief Updates the state of the digital inputs from the hardware.
 */
void StampPLC::updateDigitalInputs() {
    if (xSemaphoreTake(xSemaphorePLC, pdMS_TO_TICKS(10)) == pdTRUE) {
        // Read digital inputs from M5Stack StampPLC
        for (int i = 0; i < 8; i++) {
            currentData.digitalInputs[i] = stamPLC->readPlcInput(i);
        }
        currentData.timestamp = millis();
        xSemaphoreGive(xSemaphorePLC);
    }
}

/**
 * @brief Updates the analog sensor values from the hardware.
 */
void StampPLC::updateAnalogSensors() {
    if (xSemaphoreTake(xSemaphorePLC, pdMS_TO_TICKS(10)) == pdTRUE) {
        // Read INA226 current sensor
        if (stamPLC->INA226.begin()) {
            currentData.voltage = stamPLC->INA226.getBusVoltage();
            currentData.current = stamPLC->INA226.getShuntCurrent();
        }

        // Read LM75B temperature sensor
        if (stamPLC->LM75B.begin()) {
            currentData.temperature = stamPLC->getTemp();
        }

        xSemaphoreGive(xSemaphorePLC);
    }
}

/**
 * @brief Updates the relay outputs.
 * @note This is a placeholder for consistency, as relays are set on demand.
 */
void StampPLC::updateRelayOutputs() {
    if (xSemaphoreTake(xSemaphorePLC, pdMS_TO_TICKS(10)) == pdTRUE) {
        // Update relay states (they're already set via setRelayOutput)
        // This is just for consistency
        xSemaphoreGive(xSemaphorePLC);
    }
}