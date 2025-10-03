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

// Forward declarations
struct PLCData;
struct PLCNavigationState;

// ============================================================================
// PLC DATA STRUCTURES
// ============================================================================

struct PLCData {
    // Digital inputs (8 channels)
    bool digitalInputs[8];
    
    // Relay outputs (4 channels)
    bool relayOutputs[4];
    
    // Analog sensor values
    float voltage;
    float current;
    float temperature;
    
    // Timestamp
    unsigned long timestamp;
    
    // Constructor
    PLCData() {
        memset(digitalInputs, 0, sizeof(digitalInputs));
        memset(relayOutputs, 0, sizeof(relayOutputs));
        voltage = 0.0f;
        current = 0.0f;
        temperature = 0.0f;
        timestamp = 0;
    }
};

struct PLCNavigationState {
    uint8_t currentScreen;
    uint8_t maxScreens;
    unsigned long lastActivity;
    bool autoReturnEnabled;
    
    // Constructor
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

class StampPLC {
private:
    // M5Stack StampPLC instance
    m5::M5_STAMPLC* stamPLC;
    
    // FreeRTOS synchronization
    SemaphoreHandle_t xSemaphorePLC;
    QueueHandle_t xQueuePLCData;
    
    // Navigation state
    PLCNavigationState navigationState;
    
    // Data cache
    PLCData currentData;
    PLCData previousData;
    
    // Private methods
    bool initializeHardware();
    void updateDigitalInputs();
    void updateAnalogSensors();
    void updateRelayOutputs();
    void handleNavigation();
    void drawNavigationScreen();
    void drawDigitalInputScreen();
    void drawRelayOutputScreen();
    void drawAnalogSensorScreen();
    void drawConfigurationScreen();

public:
    // Constructor/Destructor
    StampPLC();
    ~StampPLC();
    
    // Initialization
    bool begin();
    void shutdown();
    
    // Data access (thread-safe)
    bool getDigitalInput(uint8_t channel);
    bool setRelayOutput(uint8_t channel, bool state);
    float getVoltage();
    float getCurrent();
    float getTemperature();
    PLCData getCurrentData();
    
    // Navigation system
    void navigateNext();
    void navigatePrevious();
    void navigateToScreen(uint8_t screen);
    void handleButtonPress(uint8_t button);
    void updateNavigation();
    
    // Button synchronization
    void updateButtonStates();
    
    // Status
    bool isReady();
    bool isInitialized();
    void printStatus();
    
    // FreeRTOS integration
    void taskFunction();
    static void taskWrapper(void* pvParameters);
};

#endif // STAMP_PLC_H
