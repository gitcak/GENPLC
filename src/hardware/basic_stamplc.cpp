#include "basic_stamplc.h"

#include <cstring>
#include <M5StamPLC.h>

extern m5::M5_STAMPLC M5StamPLC;

BasicStampPLC::BasicStampPLC() {
    stamPLC = nullptr;
    isInitialized = false;
    ioMutex = nullptr;
    memset(digitalInputs, 0, sizeof(digitalInputs));
    memset(analogInputs, 0, sizeof(analogInputs));
    memset(relayOutputs, 0, sizeof(relayOutputs));
}

bool BasicStampPLC::begin(m5::M5_STAMPLC* device) {
    if (isInitialized) {
        return true;
    }

    stamPLC = device ? device : &M5StamPLC;
    if (!stamPLC) {
        Serial.println("StampPLC: No M5StamPLC instance available");
        return false;
    }

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

    if (!ioMutex) {
        ioMutex = xSemaphoreCreateMutex();
        if (!ioMutex) {
            Serial.println("StampPLC: Failed to create IO mutex");
        }
    }
    return true;
}

void BasicStampPLC::update() {
    if (!isInitialized || !stamPLC) return;
    if (!ioMutex) return;
    if (xSemaphoreTake(ioMutex, portMAX_DELAY) != pdTRUE) {
        return;
    }

    // Ensure underlying StamPLC subsystem (incl. buttons via IO expander) is updated
    stamPLC->update();

    // Read digital inputs
    for (int i = 0; i < 8; i++) {
        digitalInputs[i] = stamPLC->readPlcInput(i);
    }

    // Read analog inputs (simplified for now)
    for (int i = 0; i < 4; i++) {
        analogInputs[i] = stamPLC->readPlcInput(i) ? 1023 : 0;
    }

    xSemaphoreGive(ioMutex);
}

bool BasicStampPLC::getDigitalInput(uint8_t channel) {
    if (channel >= 8) return false;
    return digitalInputs[channel];
}

uint16_t BasicStampPLC::getAnalogInput(uint8_t channel) {
    if (channel >= 4) return 0;
    return analogInputs[channel];
}

bool BasicStampPLC::setRelayOutput(uint8_t channel, bool state) {
    if (channel >= 2) return false;
    
    // Set relay state
    relayOutputs[channel] = state;
    
    // Update physical relay
    if (ioMutex && xSemaphoreTake(ioMutex, portMAX_DELAY) == pdTRUE) {
        if (stamPLC) {
            stamPLC->writePlcRelay(channel, state);
        }
        xSemaphoreGive(ioMutex);
    } else {
        if (stamPLC) {
            stamPLC->writePlcRelay(channel, state);
        }
    }
    
    return true;
}

bool BasicStampPLC::getRelayOutput(uint8_t channel) {
    if (channel >= 2) return false;
    return relayOutputs[channel];
}

void BasicStampPLC::printStatus() {
    if (!isInitialized) {
        Serial.println("StampPLC: Not initialized");
        return;
    }
    if (!stamPLC) {
        Serial.println("StampPLC: M5StamPLC instance unavailable");
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
