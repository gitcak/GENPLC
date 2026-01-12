/*
 * GENPLC Recovery Cellular Test
 * 
 * Minimal standalone test to isolate and diagnose cellular connectivity issues
 * without the complexity of the full system
 * 
 * Created: November 2025
 * Purpose: Emergency debugging of PDP activation failures
 */

#include <Arduino.h>
#include <M5StamPLC.h>
#include <HardwareSerial.h>

// UART Configuration for SIM7080G
#define CATM_SERIAL Serial1
#define CATM_RX_PIN 5  // Grove Port C G5 (Yellow)
#define CATM_TX_PIN 4  // Grove Port C G4 (White)
#define CATM_BAUD 115200
#define CATM_PWR_PIN 2  // Power control pin (if available)

// Test configuration
#define TEST_TIMEOUT_MS 30000
#define RETRY_DELAY_MS 5000
#define MAX_RETRIES 3

// Global variables
bool moduleInitialized = false;
unsigned long testStartTime = 0;
int retryCount = 0;

// APN configurations to test
const char* apnList[] = {
    "soracom.io",
    "iot.soracom.io", 
    "du.soracom.io",
    "wholesale",
    "phone"
};
const int apnCount = sizeof(apnList) / sizeof(apnList[0]);
int currentApnIndex = 0;

void setup() {
    Serial.begin(115200);
    delay(2000);
    
    Serial.println("\n=== GENPLC Recovery Cellular Test ===");
    Serial.println("Purpose: Isolate cellular connectivity issues");
    Serial.println("Target: SIM7080G module on Grove Port C");
    
    // Initialize M5StamPLC (minimal)
    M5StamPLC.begin();
    delay(1000);
    
    // Initialize CatM UART
    CATM_SERIAL.begin(CATM_BAUD, SERIAL_8N1, CATM_RX_PIN, CATM_TX_PIN);
    delay(1000);
    
    Serial.println("Hardware initialized");
    Serial.printf("CatM UART: RX=%d, TX=%d, Baud=%d\n", CATM_RX_PIN, CATM_TX_PIN, CATM_BAUD);
    
    testStartTime = millis();
    
    // Test basic module communication
    if (testModuleCommunication()) {
        moduleInitialized = true;
        Serial.println("Module communication established");
        runComprehensiveTest();
    } else {
        Serial.println("FAILED: Module communication failed");
        Serial.println("Check:");
        Serial.println("1. SIM7080G module properly connected to Grove Port C");
        Serial.println("2. Power supply to module");
        Serial.println("3. UART wiring (G4=TX, G5=RX)");
    }
}

void loop() {
    // If module failed to initialize, try recovery
    if (!moduleInitialized) {
        static unsigned long lastRetry = 0;
        if (millis() - lastRetry > RETRY_DELAY_MS) {
            lastRetry = millis();
            retryCount++;
            
            Serial.printf("\nRetry attempt %d/%d\n", retryCount, MAX_RETRIES);
            
            if (testModuleCommunication()) {
                moduleInitialized = true;
                Serial.println("Module recovered!");
                runComprehensiveTest();
            } else if (retryCount >= MAX_RETRIES) {
                Serial.println("Max retries reached. System halted.");
                while (true) {
                    delay(10000);
                    Serial.println("System halted - check hardware connections");
                }
            }
        }
    } else {
        // Module working - periodic status check
        static unsigned long lastStatus = 0;
        if (millis() - lastStatus > 30000) { // Every 30 seconds
            lastStatus = millis();
            checkConnectionStatus();
        }
    }
    
    delay(1000);
}

bool testModuleCommunication() {
    Serial.println("\n--- Testing Module Communication ---");
    
    // Try multiple AT commands with timeout
    for (int i = 0; i < 3; i++) {
        Serial.printf("Attempt %d: ", i + 1);
        
        // Clear buffers
        while (CATM_SERIAL.available()) CATM_SERIAL.read();
        while (Serial.available()) Serial.read();
        
        // Send AT command
        CATM_SERIAL.println("AT");
        
        // Wait for response with timeout
        unsigned long start = millis();
        String response = "";
        while (millis() - start < 2000) {
            if (CATM_SERIAL.available()) {
                char c = CATM_SERIAL.read();
                response += c;
                if (response.indexOf("OK") >= 0) {
                    Serial.println("OK");
                    return true;
                }
            }
            delay(10);
        }
        
        Serial.printf("TIMEOUT (response: '%s')\n", response.substring(0, 50).c_str());
        delay(1000);
    }
    
    return false;
}

void runComprehensiveTest() {
    Serial.println("\n=== Running Comprehensive Cellular Test ===");
    
    // Test sequence
    testBasicInfo();
    testSIMStatus();
    testNetworkRegistration();
    testSignalQuality();
    testAPNConfiguration();
    testPDPActivation();
    
    Serial.println("\n=== Test Summary ===");
    Serial.println("Review the results above for specific failure points");
}

void testBasicInfo() {
    Serial.println("\n--- Module Information ---");
    sendATCommand("ATI", "Module identification");
    sendATCommand("AT+CGMM", "Module model");
    sendATCommand("AT+CGMR", "Module revision");
    sendATCommand("AT+CFUN?", "Functionality mode");
}

void testSIMStatus() {
    Serial.println("\n--- SIM Status ---");
    sendATCommand("AT+CPIN?", "SIM status");
    sendATCommand("AT+ICCID", "SIM ICCID");
    sendATCommand("AT+CIMI", "SIM IMSI");
}

void testNetworkRegistration() {
    Serial.println("\n--- Network Registration ---");
    sendATCommand("AT+COPS?", "Operator selection");
    sendATCommand("AT+CEREG?", "EPS registration status");
    sendATCommand("AT+CGATT?", "GPRS attachment status");
}

void testSignalQuality() {
    Serial.println("\n--- Signal Quality ---");
    sendATCommand("AT+CSQ", "Signal quality");
    sendATCommand("AT+CESQ", "Extended signal quality");
}

void testAPNConfiguration() {
    Serial.println("\n--- APN Configuration Test ---");
    
    for (int i = 0; i < apnCount; i++) {
        currentApnIndex = i;
        Serial.printf("\nTesting APN %d/%d: %s\n", i + 1, apnCount, apnList[i]);
        
        String cmd = String("AT+CNCFG=0,1,\"") + apnList[i] + "\"";
        if (sendATCommand(cmd.c_str(), "Configure APN")) {
            Serial.println("APN configuration successful");
            // Continue to PDP test with this APN
            testPDPActivationSingleAPN();
            
            // Small delay between APN tests
            delay(2000);
        } else {
            Serial.println("APN configuration failed");
        }
    }
}

void testPDPActivation() {
    Serial.println("\n--- PDP Context Activation (Default APN) ---");
    
    // Use default soracom.io APN
    sendATCommand("AT+CNCFG=0,1,\"soracom.io\"", "Configure default APN");
    testPDPActivationSingleAPN();
}

void testPDPActivationSingleAPN() {
    Serial.println("\n--- PDP Activation Test ---");
    
    // Check current PDP status first
    sendATCommand("AT+CNACT?", "Current PDP status");
    
    // Activate PDP context
    Serial.println("Activating PDP context...");
    String activateCmd = "AT+CNACT=0,1";
    
    // Clear buffers
    while (CATM_SERIAL.available()) CATM_SERIAL.read();
    
    CATM_SERIAL.println(activateCmd);
    
    // Wait longer for PDP activation
    unsigned long start = millis();
    String response = "";
    bool gotOK = false;
    
    while (millis() - start < 30000) { // 30 second timeout
        if (CATM_SERIAL.available()) {
            char c = CATM_SERIAL.read();
            response += c;
            Serial.print(c); // Echo response
            
            if (response.indexOf("OK") >= 0) {
                gotOK = true;
                Serial.println("\nPDP activation command accepted");
                break;
            }
        }
        delay(100);
    }
    
    if (!gotOK) {
        Serial.println("\nPDP activation command timeout or failed");
        return;
    }
    
    // Wait a bit and check if IP was assigned
    delay(2000);
    sendATCommand("AT+CNACT?", "PDP status after activation");
    
    // Try alternative check method
    sendATCommand("AT+CGPADDR=0", "Get PDP address (alternative)");
    
    // Test connectivity with HTTP if we have an IP
    testConnectivity();
}

void testConnectivity() {
    Serial.println("\n--- Connectivity Test ---");
    
    // Check if we have an IP
    CATM_SERIAL.println("AT+CNACT?");
    delay(1000);
    
    String response = "";
    while (CATM_SERIAL.available()) {
        char c = CATM_SERIAL.read();
        response += c;
    }
    
    if (response.indexOf("0.0.0.0") >= 0) {
        Serial.println("No IP address assigned - skipping connectivity test");
        return;
    }
    
    Serial.println("IP address detected - testing HTTP connectivity...");
    
    // Try a simple HTTP request
    sendATCommand("AT+HTTPINIT", "Initialize HTTP service");
    sendATCommand("AT+HTTPPARA=\"CID\",1", "Set HTTP context");
    sendATCommand("AT+HTTPPARA=\"URL\",\"http://httpbin.org/ip\"", "Set HTTP URL");
    sendATCommand("AT+HTTPACTION=0", "HTTP GET request");
    
    delay(5000); // Wait for HTTP response
    
    sendATCommand("AT+HTTPREAD", "Read HTTP response");
    sendATCommand("AT+HTTPTERM", "Terminate HTTP service");
}

void checkConnectionStatus() {
    Serial.println("\n--- Periodic Status Check ---");
    Serial.printf("Uptime: %lu seconds\n", millis() / 1000);
    
    sendATCommand("AT+CSQ", "Signal quality");
    sendATCommand("AT+CEREG?", "Network registration");
    sendATCommand("AT+CNACT?", "PDP status");
}

bool sendATCommand(const char* command, const char* description) {
    Serial.printf("AT Command: %s\n", description ? description : command);
    
    // Clear buffers
    while (CATM_SERIAL.available()) CATM_SERIAL.read();
    
    // Send command
    CATM_SERIAL.println(command);
    
    // Wait for response
    unsigned long start = millis();
    String response = "";
    
    while (millis() - start < 5000) { // 5 second timeout
        if (CATM_SERIAL.available()) {
            char c = CATM_SERIAL.read();
            response += c;
            Serial.print(c); // Echo response
            
            if (response.indexOf("OK") >= 0) {
                Serial.println(); // New line after OK
                return true;
            }
            if (response.indexOf("ERROR") >= 0) {
                Serial.println(); // New line after ERROR
                return false;
            }
        }
        delay(10);
    }
    
    Serial.printf("\nTIMEOUT - Response: %s\n", response.substring(0, 100).c_str());
    return false;
}

void printHelp() {
    Serial.println("\n=== Recovery Test Help ===");
    Serial.println("This test isolates cellular connectivity issues:");
    Serial.println("1. Tests basic AT command communication");
    Serial.println("2. Verifies SIM status and network registration");
    Serial.println("3. Tests multiple APN configurations");
    Serial.println("4. Attempts PDP context activation");
    Serial.println("5. Tests HTTP connectivity if IP assigned");
    Serial.println("");
    Serial.println("Common failure points:");
    Serial.println("- No response to AT: Check power/wiring");
    Serial.println("- SIM ERROR: Check SIM insertion/carrier");
    Serial.println("- No network: Check coverage/antenna");
    Serial.println("- PDP fails: Check APN/carrier settings");
}
