#include "M5_SIM7080G.h"

M5_SIM7080G::M5_SIM7080G() {
    _serial = nullptr;
    _rxPin = -1;
    _txPin = -1;
    _initialized = false;
}

void M5_SIM7080G::Init(HardwareSerial* serial, int rxPin, int txPin) {
    _serial = serial;
    _rxPin = rxPin;
    _txPin = txPin;
    
    if (_serial) {
        _serial->begin(115200, SERIAL_8N1, rxPin, txPin);
        delay(1000); // Allow modem to initialize
        _initialized = true;
    }
}

void M5_SIM7080G::sendMsg(const String& command) {
    if (!_initialized || !_serial) return;
    
    _serial->print(command);
    _serial->flush();
}

String M5_SIM7080G::waitMsg(unsigned long timeoutMs) {
    if (!_initialized || !_serial) return "";
    
    String response = "";
    unsigned long startTime = millis();
    
    while (millis() - startTime < timeoutMs) {
        if (_serial->available()) {
            char c = _serial->read();
            response += c;
            
            // Check for complete response (OK or ERROR)
            if (response.indexOf("OK") != -1 || response.indexOf("ERROR") != -1) {
                break;
            }
        }
        
        // Feed watchdog every 100ms during long waits
        if ((millis() - startTime) % 100 == 0) {
            feedWatchdog();
        }
        
        delay(10); // Small delay to prevent CPU spinning
    }
    
    return response;
}

void M5_SIM7080G::feedWatchdog() {
    // Feed the ESP32 task watchdog to prevent resets during long AT operations
    esp_task_wdt_reset();
}

bool M5_SIM7080G::isReady() {
    if (!_initialized || !_serial) return false;
    
    sendMsg("AT\r\n");
    String response = waitMsg(2000);
    return response.indexOf("OK") != -1;
}

int M5_SIM7080G::getSignalQuality() {
    if (!_initialized || !_serial) return 0;
    
    sendMsg("AT+CSQ\r\n");
    String response = waitMsg(2000);
    
    int idx = response.indexOf("+CSQ: ");
    if (idx != -1) {
        int val = response.substring(idx + 6, response.indexOf(",", idx)).toInt();
        if (val != 99) return val; // 99 means not known/not detectable
    }
    
    return 0;
}

String M5_SIM7080G::getIpAddress(const String& response) {
    int start = response.indexOf("+CNACT:");
    if (start == -1) return "";
    
    String buf = response.substring(start + 13);
    int term = buf.indexOf("\r");
    if (term == -1) term = buf.indexOf("\n");
    if (term == -1) return buf;
    
    return buf.substring(0, term);
}

bool M5_SIM7080G::isIpActive(const String& response) {
    int start = response.indexOf("+CNACT:");
    if (start == -1) return false;
    
    // Check if the status is "1" (active)
    return response.substring(start + 10, start + 11) == "1";
}
