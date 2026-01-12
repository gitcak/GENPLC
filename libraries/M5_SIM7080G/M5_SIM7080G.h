#ifndef M5_SIM7080G_H
#define M5_SIM7080G_H

#include <Arduino.h>
#include <HardwareSerial.h>
#include "esp_task_wdt.h"

class M5_SIM7080G {
public:
    M5_SIM7080G();
    
    // Initialize the modem with UART pins
    void Init(HardwareSerial* serial, int rxPin, int txPin);
    
    // Send AT command to modem
    void sendMsg(const String& command);
    
    // Wait for response with watchdog feeding
    String waitMsg(unsigned long timeoutMs = 5000);
    
    // Check if modem is responding
    bool isReady();
    
    // Get signal quality
    int getSignalQuality();
    
    // Get IP address from CNACT response
    String getIpAddress(const String& response);
    
    // Check if IP is active
    bool isIpActive(const String& response);

private:
    HardwareSerial* _serial;
    int _rxPin;
    int _txPin;
    bool _initialized;
    
    // Helper to feed watchdog during long operations
    void feedWatchdog();
};

#endif // M5_SIM7080G_H
