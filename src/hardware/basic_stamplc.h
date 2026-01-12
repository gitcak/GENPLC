#ifndef BASIC_STAMPLC_H
#define BASIC_STAMPLC_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <utils/ina226/ina226.h>
#include <M5StamPLC.h>

class BasicStampPLC {
private:
    m5::M5_STAMPLC* stamPLC;
    bool isInitialized;
    SemaphoreHandle_t ioMutex;
    
    // State variables
    bool digitalInputs[8];
    uint16_t analogInputs[4];
    bool relayOutputs[2];

public:
    BasicStampPLC();
    
    // Initialization
    bool begin(m5::M5_STAMPLC* device = nullptr);
    
    // Update state
    void update();
    
    // Digital inputs (8 channels)
    bool getDigitalInput(uint8_t channel);
    
    // Analog inputs (4 channels)
    uint16_t getAnalogInput(uint8_t channel);
    
    // Relay outputs (2 channels)
    bool setRelayOutput(uint8_t channel, bool state);
    bool getRelayOutput(uint8_t channel);
    
    // Status
    void printStatus();
    bool isReady() { return isInitialized; }
    
    // Button access
    m5::M5_STAMPLC* getStamPLC() { return stamPLC; }
};

#endif // BASIC_STAMPLC_H
