#include "catm_gnss_module.h"
#include "../logging/log_buffer.h"
#include "config/task_config.h"
#include "../../include/string_pool.h"
#include <stdlib.h>
#include <string.h>
extern EventGroupHandle_t xEventGroupSystemStatus;

CatMGNSSModule::CatMGNSSModule() {
    isInitialized = false;
    state = CatMGNSSState::INITIALIZING;
    serialModule = nullptr;
    lastError_.clear();
    lastProbeRx_ = -1;
    lastProbeTx_ = -1;
    
    // Initialize GNSS data
    gnssData.latitude = 0;
    gnssData.longitude = 0;
    gnssData.altitude = 0;
    gnssData.speed = 0;
    gnssData.course = 0;
    gnssData.hour = 0;
    gnssData.minute = 0;
    gnssData.second = 0;
    gnssData.day = 0;
    gnssData.month = 0;
    gnssData.year = 0;
    gnssData.satellites = 0;
    gnssData.isValid = false;
    gnssData.lastUpdate = 0;
    gnssData.hdop = 0;
    gnssData.pdop = 0;
    gnssData.vdop = 0;
    gnssData.hAcc = 0;
    gnssData.vAcc = 0;
    
    // Initialize cellular data
    cellularData.isConnected = false;
    cellularData.operatorName = "";
    cellularData.signalStrength = 0;
    cellularData.imei = "";
    cellularData.ipAddress = "";
    cellularData.apn = "";
    cellularData.isRegistered = false;
    cellularData.registrationState = 0;
    cellularData.lastUpdate = 0;
    cellularData.isValid = false;
    cellularData.hasIpAddress = false;
    cellularData.errorCount = 0;
    cellularData.lastDetachReason = "";
    cellularData.txBytes = 0;
    cellularData.rxBytes = 0;
    cellularData.txBps = 0;
    cellularData.rxBps = 0;

    lastTxBytesSample_ = 0;
    lastRxBytesSample_ = 0;
    lastStatsSampleMs_ = 0;
    networkTimeConfigured_ = false;
    networkTimeSynced_ = false;
    lastNetworkTimeSyncMs_ = 0;
    memset(&lastNetworkUtc_, 0, sizeof(lastNetworkUtc_));

    
    // Create mutex
    serialMutex = xSemaphoreCreateMutex();
    if (!serialMutex) {
        Serial.println("CatM+GNSS: Failed to create serial mutex");
    }
}

CatMGNSSModule::~CatMGNSSModule() {
    if (serialModule) {
        serialModule->end();
        delete serialModule;
    }
    
    if (serialMutex) {
        vSemaphoreDelete(serialMutex);
    }
}

bool CatMGNSSModule::begin() {
    Serial.println("CatM+GNSS: begin() called");
    Serial.flush();
    lastError_.clear();
    lastProbeRx_ = -1;
    lastProbeTx_ = -1;

    if (!serialMutex) {
        lastError_ = "Serial mutex not available";
        Serial.println("CatM+GNSS: Serial mutex not available");
        Serial.flush();
        return false;
    }
    Serial.println("CatM+GNSS: Serial mutex OK");
    Serial.flush();

    if (serialModule) {
        delete serialModule;
        serialModule = nullptr;
    }
    serialModule = new HardwareSerial(2); // Use UART2 for CatM+GNSS module (avoid conflicts)
    if (!serialModule) {
        lastError_ = "Failed to create serial instance";
        Serial.println("CatM+GNSS: Failed to create serial instance");
        Serial.flush();
        return false;
    }
    Serial.println("CatM+GNSS: HardwareSerial created");
    Serial.flush();

    const int rx = CATM_GNSS_RX_PIN;
    const int tx = CATM_GNSS_TX_PIN;
    lastProbeRx_ = rx;
    lastProbeTx_ = tx;

    Serial.printf("CatM+GNSS: Configuring Grove Port C (RX:%d TX:%d) for UART @ %d baud\n", rx, tx, CATM_GNSS_BAUD_RATE);
    Serial.flush();
    
    serialModule->end();
    delay(50); // Give serial time to close
    
    pinMode(rx, INPUT_PULLUP);
    pinMode(tx, OUTPUT);
    
    Serial.println("CatM+GNSS: Starting serial port...");
    Serial.flush();
    serialModule->begin(CATM_GNSS_BAUD_RATE, SERIAL_8N1, rx, tx);
    
    Serial.println("CatM+GNSS: Waiting for serial to stabilize...");
    Serial.flush();
    delay(300); // Increased delay for module to boot/respond
    
    // Flush any startup garbage
    uint32_t flushStart = millis();
    int flushed = 0;
    while (serialModule->available() && (millis() - flushStart < 500)) {
        serialModule->read();
        flushed++;
    }
    if (flushed > 0) {
        Serial.printf("CatM+GNSS: Flushed %d bytes from serial buffer\n", flushed);
    }
    Serial.flush();
    
    Serial.println("CatM+GNSS: Probing modem with AT...");
    Serial.flush();
    
    bool at_ok = false;
    {
        char quick[64] = {0};
        Serial.println("CatM+GNSS: Sending initial AT command...");
        Serial.flush();
        if (sendATCommand("AT", quick, sizeof(quick), 1000)) {
            Serial.printf("CatM+GNSS: AT response: [%s]\n", quick);
            Serial.flush();
            if (strstr(quick, POOL_STRING("OK")) != nullptr) {
                at_ok = true;
                lastError_.clear();
                Serial.println("CatM+GNSS: AT OK on Grove Port C (first attempt)");
                Serial.flush();
            } else {
                Serial.printf("CatM+GNSS: AT response missing 'OK', got: [%s]\n", quick);
                Serial.flush();
            }
        } else {
            Serial.println("CatM+GNSS: No response to AT command");
            Serial.flush();
        }
    }

    if (!at_ok) {
        Serial.println("CatM+GNSS: Trying alternative initialization sequence...");
        Serial.flush();
        char resp[128] = {0};
        Serial.println("CatM+GNSS: Sending ATE0...");
        Serial.flush();
        sendATCommand("ATE0", resp, sizeof(resp), 500);
        Serial.printf("CatM+GNSS: ATE0 response: [%s]\n", resp);
        Serial.flush();
        
        Serial.println("CatM+GNSS: Sending ATI...");
        Serial.flush();
        sendATCommand("ATI", resp, sizeof(resp), 1000);
        Serial.printf("CatM+GNSS: ATI response: [%s]\n", resp);
        Serial.flush();
        
        Serial.println("CatM+GNSS: Retrying AT...");
        Serial.flush();
        if (sendATCommand("AT", resp, sizeof(resp), 1000)) {
            Serial.printf("CatM+GNSS: AT retry response: [%s]\n", resp);
            Serial.flush();
            if (strstr(resp, POOL_STRING("OK")) != nullptr) {
                at_ok = true;
                lastError_.clear();
                Serial.println("CatM+GNSS: AT OK on Grove Port C (retry)");
                Serial.flush();
            }
        }
    }

    if (!at_ok) {
        lastError_ = "No AT response on Grove Port C (check unit power and Grove cable)";
        Serial.println("CatM+GNSS: Modem not detected (absent). Booting without CatM/GNSS.");
        Serial.printf("CatM+GNSS: Check: Unit powered? Cable on Grove Port C? Pins RX:%d TX:%d\n", rx, tx);
        Serial.flush();
        return false;
    }

    // Ensure SIM is ready before baseline config (some units need extra time after power-up)
    {
        uint32_t start = millis();
        String r;
        bool simReady = false;
        Serial.println("CatM+GNSS: Waiting for SIM ready (max 30s)...");
        Serial.flush();
        int attempt = 0;
        while (millis() - start < 30000UL) { // wait up to 30s for SIM ready
            yield(); // Feed watchdog during long wait
            attempt++;
            if (attempt % 10 == 0) { // Log every 5 seconds
                Serial.printf("CatM+GNSS: SIM check attempt %d (elapsed: %lu ms)\n", attempt, millis() - start);
                Serial.flush();
            }
            if (sendATCommand("AT+CPIN?", r, 2000)) {
                Serial.printf("CatM+GNSS: AT+CPIN? response: [%s]\n", r.c_str());
                Serial.flush();
                if (r.indexOf("+CPIN: READY") >= 0) { 
                    simReady = true; 
                    Serial.println("CatM+GNSS: SIM ready");
                    Serial.flush();
                    break; 
                } else if (r.indexOf("+CPIN: SIM PIN") >= 0) {
                    lastError_ = "SIM requires PIN unlock";
                    Serial.println("CatM+GNSS: ERROR - SIM requires PIN unlock");
                    Serial.flush();
                    return false;
                } else if (r.indexOf("+CPIN: SIM PUK") >= 0) {
                    lastError_ = "SIM PUK required (SIM may be locked)";
                    Serial.println("CatM+GNSS: ERROR - SIM PUK required");
                    Serial.flush();
                    return false;
                }
            } else {
                if (attempt % 10 == 0) {
                    Serial.println("CatM+GNSS: No response to AT+CPIN?");
                    Serial.flush();
                }
            }
            vTaskDelay(pdMS_TO_TICKS(500));
            yield(); // Feed watchdog again
        }
        if (!simReady) {
            lastError_ = "SIM not ready after 30s (AT+CPIN? timeout)";
            Serial.println("CatM+GNSS: SIM not ready after 30s");
            Serial.flush();
            return false;
        }
    }

    Serial.println("CatM+GNSS: Applying baseline configuration...");
    Serial.flush();
    if (!applyBaselineConfig()) {
        Serial.printf("CatM+GNSS: Baseline config failed: %s\n", lastError_.c_str());
        Serial.flush();
        return false;
    }
    Serial.println("CatM+GNSS: Baseline config OK");
    Serial.flush();

    Serial.println("CatM+GNSS: Configuring network time...");
    Serial.flush();
    if (!configureNetworkTime()) {
        Serial.printf("CatM+GNSS: Network time configuration failed: %s\n", lastError_.c_str());
        Serial.flush();
        // Don't fail initialization if network time config fails - it's not critical
        Serial.println("CatM+GNSS: Continuing despite network time config failure");
        Serial.flush();
    } else {
        Serial.println("CatM+GNSS: Network time configured");
        Serial.flush();
    }

    Serial.println("CatM+GNSS: Module initialized successfully");
    lastError_.clear();
    isInitialized = true;
    state = CatMGNSSState::READY;

    return true;
}

void CatMGNSSModule::shutdown() {
    if (!isInitialized) return;
    
    // Disable GNSS
    disableGNSS();
    
    // Disconnect network
    disconnectNetwork();
    
    // Power down module
    char response[128];
    resetNetworkStats();
    sendATCommand("AT+CPOWD=1", response, sizeof(response), 5000);

    networkTimeConfigured_ = false;
    networkTimeSynced_ = false;
    lastNetworkTimeSyncMs_ = 0;
    memset(&lastNetworkUtc_, 0, sizeof(lastNetworkUtc_));
    
    isInitialized = false;
    state = CatMGNSSState::INITIALIZING;
}

bool CatMGNSSModule::testAT() {
    char response[64];
    for (int i = 0; i < 3; i++) {
        if (sendATCommand("AT", response, sizeof(response), 1000) && strstr(response, POOL_STRING("OK")) != nullptr) {
            return true;
        }
        delay(500);
    }
    return false;
}

bool CatMGNSSModule::sendATCommand(const String& command, String& response, uint32_t timeout) {
    // Enhanced null checks
    if (!serialModule) {
        Serial.println("CatM+GNSS: Serial module is null");
        return false;
    }
    if (!serialMutex) {
        Serial.println("CatM+GNSS: Serial mutex is null");
        return false;
    }
    
    // Validate command string
    if (command.length() == 0) {
        Serial.println("CatM+GNSS: Empty command");
        return false;
    }
    
    // Try to take mutex
    if (xSemaphoreTake(serialMutex, pdMS_TO_TICKS(3000)) != pdTRUE) {
        Serial.println("CatM+GNSS: Failed to take mutex");
        return false;
    }
    
    // Clear any pending data with bounds checking
    while (serialModule->available()) {
        serialModule->read();
    }
    
    // Send command
    Serial.println("CatM+GNSS: >>> " + command); // Debug output
    serialModule->println(command);
    
    // Wait for response
    bool result = waitForResponse(response, timeout);
    
    // Release mutex
    xSemaphoreGive(serialMutex);
    
    return result;
}

bool CatMGNSSModule::sendATCommand(const char* command, char* response, size_t responseSize, uint32_t timeout) {
    if (!serialModule || !command || !response || responseSize == 0) return false;
    if (!serialMutex) return false;
    
    // Try to take mutex
    if (xSemaphoreTake(serialMutex, pdMS_TO_TICKS(3000)) != pdTRUE) {
        Serial.println("CatM+GNSS: Failed to take mutex");
        return false;
    }
    
    // Clear any pending data
    while (serialModule->available()) {
        serialModule->read();
    }
    
    // Send command
    Serial.printf("CatM+GNSS: >>> %s\n", command); // Debug output
    serialModule->println(command);
    
    // Wait for response
    bool result = waitForResponse(response, responseSize, timeout);
    
    // Release mutex
    xSemaphoreGive(serialMutex);
    
    return result;
}

bool CatMGNSSModule::applyBaselineConfig() {
    char resp[256];
    if (!sendATCommand("AT+CMEE=2", resp, sizeof(resp), 1000) || strstr(resp, POOL_STRING("OK")) == nullptr) {
        lastError_ = "Failed to configure modem (AT+CMEE)";
        return false;
    }
    if (!sendATCommand("AT+CFUN=1", resp, sizeof(resp), 5000) || strstr(resp, POOL_STRING("OK")) == nullptr) {
        lastError_ = "Failed to set modem to full functionality";
        return false;
    }
    // Improve boot reliability: keep UART awake and disable power-saving until attached
    sendATCommand("AT+CSCLK=0", resp, sizeof(resp), 1000);   // UART clock always on
    sendATCommand("AT+CPSMS=0", resp, sizeof(resp), 1000);   // disable PSM
    sendATCommand("AT+CEDRXS=0", resp, sizeof(resp), 1000);  // disable eDRX
    sendATCommand("AT+IFC=0,0", resp, sizeof(resp), 1000);   // no HW flow control on UART
    // Preferred RAT and operator selection (best effort)
    sendATCommand("AT+CMNB=1", resp, sizeof(resp), 2000);
    sendATCommand("AT+CNMP=38", resp, sizeof(resp), 2000);
    sendATCommand("AT+COPS=0", resp, sizeof(resp), 5000);
    
    // Soracom-centric network time configuration
    if (!sendATCommand("AT+CLTS=1", resp, sizeof(resp), 1000) || strstr(resp, POOL_STRING("OK")) == nullptr) {
        lastError_ = "Failed to enable network time latch (AT+CLTS=1)";
        return false;
    }
    if (!sendATCommand("AT+CNTPCID=1", resp, sizeof(resp), 1000) || strstr(resp, POOL_STRING("OK")) == nullptr) {
        lastError_ = "Failed to bind CNTP to PDP context 1 (AT+CNTPCID=1)";
        return false;
    }
    if (!sendATCommand("AT+CNTP=\"ntp.soracom.io\",0", resp, sizeof(resp), 1000) || strstr(resp, POOL_STRING("OK")) == nullptr) {
        lastError_ = "Failed to configure Soracom NTP server (AT+CNTP)";
        return false;
    }
    
    return true;
}

bool CatMGNSSModule::waitForResponse(String& response, uint32_t timeout) {
    response = "";
    uint32_t start = millis();
    bool receivedAnyData = false;
    
    while (millis() - start < timeout) {
        if (serialModule->available()) {
            receivedAnyData = true;
            char c = serialModule->read();
            response += c;
            
            // Limit response size to prevent memory issues
            if (response.length() > 512) {
                Serial.println("CatM+GNSS: WARNING - Response too long, truncating");
                Serial.flush();
                response = response.substring(0, 512);
            }
            
            // Check if we have a complete response
            if (response.indexOf("\r\nOK\r\n") >= 0 ||
                response.indexOf("\nOK\r\n") >= 0 ||
                response.endsWith("OK\r\n") ||
                response.indexOf("\r\nERROR\r\n") >= 0 || 
                response.indexOf("+CME ERROR") > 0) {
                Serial.printf("CatM+GNSS: <<< [%lu ms] %s\n", millis() - start, response.c_str());
                Serial.flush();
                return true;
            }
        }
        yield(); // Feed watchdog and allow other tasks
        delay(10);
    }
    
    // Timeout - log what we got (if anything)
    if (receivedAnyData) {
        Serial.printf("CatM+GNSS: Timeout after %lu ms, partial response: [%s]\n", timeout, response.c_str());
    } else {
        Serial.printf("CatM+GNSS: Timeout after %lu ms, no data received\n", timeout);
    }
    Serial.flush();
    return false;
}

bool CatMGNSSModule::waitForResponse(char* response, size_t responseSize, uint32_t timeout) {
    if (!response || responseSize == 0) return false;
    
    memset(response, 0, responseSize);
    uint32_t start = millis();
    size_t pos = 0;
    bool receivedAnyData = false;
    
    while (millis() - start < timeout) {
        if (serialModule->available()) {
            receivedAnyData = true;
            char c = serialModule->read();
            
            // Prevent buffer overflow
            if (pos < responseSize - 1) {
                response[pos++] = c;
                response[pos] = '\0';
                
                // Check if we have a complete response
                if (strstr(response, "\r\nOK\r\n") != nullptr ||
                    strstr(response, "\nOK\r\n") != nullptr ||
                    strstr(response, "OK\r\n") != nullptr ||
                    strstr(response, "\r\nERROR\r\n") != nullptr || 
                    strstr(response, "+CME ERROR") != nullptr) {
                    Serial.printf("CatM+GNSS: <<< [%lu ms] %s\n", millis() - start, response);
                    Serial.flush();
                    return true;
                }
            }
        }
        yield(); // Feed watchdog and allow other tasks
        delay(10);
    }
    
    // Timeout - log what we got (if anything)
    if (receivedAnyData) {
        Serial.printf("CatM+GNSS: Timeout after %lu ms, partial response: [%s]\n", timeout, response);
    } else {
        Serial.printf("CatM+GNSS: Timeout after %lu ms, no data received\n", timeout);
    }
    Serial.flush();
    return false;
}

// GNSS Functions
bool CatMGNSSModule::enableGNSS() {
    if (!isInitialized) return false;
    
    char response[128];
    // Power on GNSS
    if (!sendATCommand("AT+CGNSPWR=1", response, sizeof(response), 2000) || strstr(response, POOL_STRING("OK")) == nullptr) {
        Serial.println("CatM+GNSS: Failed to power on GNSS");
        return false;
    }
    
    // Configure GNSS output format (optional; some firmware may not support this)
    if (!sendATCommand("AT+CGNSSEQ=\"RMC\"", response, sizeof(response), 1000)) {
        Serial.println("CatM+GNSS: GNSS sequence config not supported; continuing");
    }
    
    Serial.println("CatM+GNSS: GNSS enabled successfully");
    return true;
}

bool CatMGNSSModule::disableGNSS() {
    if (!isInitialized) return false;
    
    char response[128];
    if (!sendATCommand("AT+CGNSPWR=0", response, sizeof(response), 2000) || strstr(response, POOL_STRING("OK")) == nullptr) {
        Serial.println("CatM+GNSS: Failed to power off GNSS");
        return false;
    }
    
    Serial.println("CatM+GNSS: GNSS disabled successfully");
    return true;
}

bool CatMGNSSModule::updateGNSSData() {
    if (!isInitialized) return false;
    
    char response[512];
    if (!sendATCommand("AT+CGNSINF", response, sizeof(response), 2000)) {
        Serial.println("CatM+GNSS: Failed to get GNSS information");
        return false;
    }
    
    // Parse GNSS data
    return parseGNSSData(response);
}

bool CatMGNSSModule::parseGNSSData(const String& data) {
    // Check if we have a valid response
    int startIdx = data.indexOf("+CGNSINF: ");
    if (startIdx < 0) {
        Serial.println("CatM+GNSS: Invalid GNSS data format");
        return false;
    }
    
    // Extract the data part
    startIdx += 10; // Skip "+CGNSINF: "
    String gnssStr = data.substring(startIdx, data.indexOf("\r\n", startIdx));
    
    // Split by commas
    int commaPos[15];
    int commaCount = 0;
    
    for (int i = 0; i < gnssStr.length() && commaCount < 15; i++) {
        if (gnssStr.charAt(i) == ',') {
            commaPos[commaCount++] = i;
        }
    }
    
    if (commaCount < 14) {
        Serial.println("CatM+GNSS: Not enough data fields");
        return false;
    }
    
    // GNSS run status
    int gnssRunStatus = gnssStr.substring(0, commaPos[0]).toInt();
    if (gnssRunStatus != 1) {
        Serial.println("CatM+GNSS: GNSS is not running");
        gnssData.isValid = false;
        return false;
    }
    
    // GNSS fix status
    int gnssFixStatus = gnssStr.substring(commaPos[0] + 1, commaPos[1]).toInt();
    gnssData.isValid = (gnssFixStatus == 1);
    
    if (!gnssData.isValid) {
        Serial.println("CatM+GNSS: No valid GNSS fix");
        return false;
    }
    
    // UTC time
    String utcTime = gnssStr.substring(commaPos[1] + 1, commaPos[2]);
    if (utcTime.length() >= 14) {
        gnssData.year = utcTime.substring(0, 4).toInt();
        gnssData.month = utcTime.substring(4, 6).toInt();
        gnssData.day = utcTime.substring(6, 8).toInt();
        gnssData.hour = utcTime.substring(8, 10).toInt();
        gnssData.minute = utcTime.substring(10, 12).toInt();
        gnssData.second = utcTime.substring(12, 14).toInt();
    }
    
    // Latitude
    gnssData.latitude = gnssStr.substring(commaPos[2] + 1, commaPos[3]).toFloat();
    
    // Longitude
    gnssData.longitude = gnssStr.substring(commaPos[3] + 1, commaPos[4]).toFloat();
    
    // Altitude
    gnssData.altitude = gnssStr.substring(commaPos[4] + 1, commaPos[5]).toFloat();
    
    // Speed
    gnssData.speed = gnssStr.substring(commaPos[5] + 1, commaPos[6]).toFloat();
    
    // Course
    gnssData.course = gnssStr.substring(commaPos[6] + 1, commaPos[7]).toFloat();
    
    // Satellites in view and accuracies (SIM7080 CGNSINF fields vary; parse defensively)
    String satsStr = gnssStr.substring(commaPos[13] + 1, commaPos[14]);
    gnssData.satellites = satsStr.toInt();
    // Try to parse DOP/accuracy fields if present beyond known indices
    // Some firmware: fields [10]=hdop, [11]=pdop, [12]=vdop, [14]=speed accuracy, [15]=course accuracy
    // We'll clamp indices checks
    auto safeField = [&](int idxA, int idxB)->String{
        if (idxA >=0 && idxB >=0 && idxB > idxA && idxB <= (int)gnssStr.length()) return gnssStr.substring(idxA, idxB);
        return String();
    };
    // attempt HDOP/PDOP/VDOP around [10..12]
    if (commaCount > 12) {
        String hd = safeField(commaPos[9] + 1, commaPos[10]);
        String pd = safeField(commaPos[10] + 1, commaPos[11]);
        String vd = safeField(commaPos[11] + 1, commaPos[12]);
        gnssData.hdop = hd.length() ? hd.toFloat() : 0;
        gnssData.pdop = pd.length() ? pd.toFloat() : 0;
        gnssData.vdop = vd.length() ? vd.toFloat() : 0;
    }
    
    // Update timestamp
    gnssData.lastUpdate = millis();
    
    Serial.printf("CatM+GNSS: Valid fix - Lat: %.6f, Lon: %.6f, Alt: %.1f, Sats: %d\n",
                 gnssData.latitude, gnssData.longitude, gnssData.altitude, gnssData.satellites);
    
    return true;
}

bool CatMGNSSModule::parseGNSSData(const char* data) {
    if (!data) return false;
    
    // Check if we have a valid response
    const char* startPtr = strstr(data, "+CGNSINF: ");
    if (!startPtr) {
        Serial.println("CatM+GNSS: Invalid GNSS data format");
        return false;
    }
    
    // Extract the data part
    startPtr += 10; // Skip "+CGNSINF: "
    const char* endPtr = strstr(startPtr, "\r\n");
    if (!endPtr) {
        Serial.println("CatM+GNSS: Invalid GNSS data format");
        return false;
    }
    
    // Copy data to local buffer for parsing
    size_t dataLen = endPtr - startPtr;
    if (dataLen >= 512) {
        Serial.println("CatM+GNSS: GNSS data too long");
        return false;
    }
    
    char gnssStr[512];
    strncpy(gnssStr, startPtr, dataLen);
    gnssStr[dataLen] = '\0';
    
    // Split by commas
    int commaPos[15];
    int commaCount = 0;
    
    for (int i = 0; gnssStr[i] != '\0' && commaCount < 15; i++) {
        if (gnssStr[i] == ',') {
            commaPos[commaCount++] = i;
        }
    }
    
    if (commaCount < 14) {
        Serial.println("CatM+GNSS: Not enough data fields");
        return false;
    }
    
    // GNSS run status
    char runStatusStr[16];
    strncpy(runStatusStr, gnssStr, commaPos[0]);
    runStatusStr[commaPos[0]] = '\0';
    int gnssRunStatus = atoi(runStatusStr);
    
    if (gnssRunStatus != 1) {
        Serial.println("CatM+GNSS: GNSS is not running");
        gnssData.isValid = false;
        return false;
    }
    
    // GNSS fix status
    char fixStatusStr[16];
    strncpy(fixStatusStr, gnssStr + commaPos[0] + 1, commaPos[1] - commaPos[0] - 1);
    fixStatusStr[commaPos[1] - commaPos[0] - 1] = '\0';
    int gnssFixStatus = atoi(fixStatusStr);
    gnssData.isValid = (gnssFixStatus == 1);
    
    if (!gnssData.isValid) {
        Serial.println("CatM+GNSS: No valid GNSS fix");
        return false;
    }
    
    // UTC time
    char utcTimeStr[16];
    strncpy(utcTimeStr, gnssStr + commaPos[1] + 1, commaPos[2] - commaPos[1] - 1);
    utcTimeStr[commaPos[2] - commaPos[1] - 1] = '\0';
    
    if (strlen(utcTimeStr) >= 14) {
        char yearStr[5], monthStr[3], dayStr[3], hourStr[3], minuteStr[3], secondStr[3];
        strncpy(yearStr, utcTimeStr, 4); yearStr[4] = '\0';
        strncpy(monthStr, utcTimeStr + 4, 2); monthStr[2] = '\0';
        strncpy(dayStr, utcTimeStr + 6, 2); dayStr[2] = '\0';
        strncpy(hourStr, utcTimeStr + 8, 2); hourStr[2] = '\0';
        strncpy(minuteStr, utcTimeStr + 10, 2); minuteStr[2] = '\0';
        strncpy(secondStr, utcTimeStr + 12, 2); secondStr[2] = '\0';
        
        gnssData.year = atoi(yearStr);
        gnssData.month = atoi(monthStr);
        gnssData.day = atoi(dayStr);
        gnssData.hour = atoi(hourStr);
        gnssData.minute = atoi(minuteStr);
        gnssData.second = atoi(secondStr);
    }
    
    // Latitude
    char latStr[32];
    strncpy(latStr, gnssStr + commaPos[2] + 1, commaPos[3] - commaPos[2] - 1);
    latStr[commaPos[3] - commaPos[2] - 1] = '\0';
    gnssData.latitude = atof(latStr);
    
    // Longitude
    char lonStr[32];
    strncpy(lonStr, gnssStr + commaPos[3] + 1, commaPos[4] - commaPos[3] - 1);
    lonStr[commaPos[4] - commaPos[3] - 1] = '\0';
    gnssData.longitude = atof(lonStr);
    
    // Altitude
    char altStr[32];
    strncpy(altStr, gnssStr + commaPos[4] + 1, commaPos[5] - commaPos[4] - 1);
    altStr[commaPos[5] - commaPos[4] - 1] = '\0';
    gnssData.altitude = atof(altStr);
    
    // Speed
    char speedStr[32];
    strncpy(speedStr, gnssStr + commaPos[5] + 1, commaPos[6] - commaPos[5] - 1);
    speedStr[commaPos[6] - commaPos[5] - 1] = '\0';
    gnssData.speed = atof(speedStr);
    
    // Course
    char courseStr[32];
    strncpy(courseStr, gnssStr + commaPos[6] + 1, commaPos[7] - commaPos[6] - 1);
    courseStr[commaPos[7] - commaPos[6] - 1] = '\0';
    gnssData.course = atof(courseStr);
    
    // Satellites in view
    char satsStr[16];
    strncpy(satsStr, gnssStr + commaPos[13] + 1, commaPos[14] - commaPos[13] - 1);
    satsStr[commaPos[14] - commaPos[13] - 1] = '\0';
    gnssData.satellites = atoi(satsStr);
    
    // Try to parse DOP/accuracy fields if present beyond known indices
    if (commaCount > 12) {
        char hdStr[16], pdStr[16], vdStr[16];
        
        if (commaPos[9] + 1 < commaPos[10]) {
            strncpy(hdStr, gnssStr + commaPos[9] + 1, commaPos[10] - commaPos[9] - 1);
            hdStr[commaPos[10] - commaPos[9] - 1] = '\0';
            gnssData.hdop = atof(hdStr);
        }
        
        if (commaPos[10] + 1 < commaPos[11]) {
            strncpy(pdStr, gnssStr + commaPos[10] + 1, commaPos[11] - commaPos[10] - 1);
            pdStr[commaPos[11] - commaPos[10] - 1] = '\0';
            gnssData.pdop = atof(pdStr);
        }
        
        if (commaPos[11] + 1 < commaPos[12]) {
            strncpy(vdStr, gnssStr + commaPos[11] + 1, commaPos[12] - commaPos[11] - 1);
            vdStr[commaPos[12] - commaPos[11] - 1] = '\0';
            gnssData.vdop = atof(vdStr);
        }
    }
    
    // Update timestamp
    gnssData.lastUpdate = millis();
    
    Serial.printf("CatM+GNSS: Valid fix - Lat: %.6f, Lon: %.6f, Alt: %.1f, Sats: %d\n",
                 gnssData.latitude, gnssData.longitude, gnssData.altitude, gnssData.satellites);
    
    return true;
}

GNSSData CatMGNSSModule::getGNSSData() {
    return gnssData;
}

bool CatMGNSSModule::hasValidFix() {
    return gnssData.isValid;
}

uint8_t CatMGNSSModule::getSatellites() {
    return gnssData.satellites;
}

// Cellular Functions
void CatMGNSSModule::setApnCredentials(const String& apn, const String& user, const String& pass) {
    apn_ = apn; apnUser_ = user; apnPass_ = pass;
    cellularData.apn = apn;
}

bool CatMGNSSModule::ensureRegistered(uint32_t maxWaitMs) {
    String response; uint32_t start=millis();
    sendATCommand("AT+CEREG=2", response, 2000);
    while (millis()-start < maxWaitMs) {
        if (sendATCommand("AT+CEREG?", response, 2000)) {
            int idx = response.indexOf("+CEREG:");
            if (idx >= 0) {
                int comma = response.indexOf(',', idx);
                if (comma > 0) {
                    int end = response.indexOf(',', comma + 1);
                    String stateStr = (end > comma) ? response.substring(comma + 1, end) : response.substring(comma + 1);
                    stateStr.trim();
                    uint8_t regState = (uint8_t)stateStr.toInt();
                    updateRegistrationState(regState);
                    if (regState == 1 || regState == 5) {
                        cellularData.lastUpdate = millis();
                        return true;
                    }
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
    return false;
}



void CatMGNSSModule::updateRegistrationState(uint8_t state) {
    cellularData.registrationState = state;
    cellularData.isRegistered = (state == 1 || state == 5);
}

bool CatMGNSSModule::parseCNACTResponse(const String& resp, bool& anyActive, String& ipOut) {
    anyActive = false;
    ipOut = String();
    int pos = 0;
    while (true) {
        int idx = resp.indexOf("+CNACT:", pos);
        if (idx < 0) break;
        int lineEnd = resp.indexOf('\n', idx);
        String line = (lineEnd > idx) ? resp.substring(idx, lineEnd) : resp.substring(idx);
        pos = (lineEnd > 0) ? lineEnd + 1 : idx + 7;
        int firstComma = line.indexOf(',');
        if (firstComma < 0 || firstComma + 1 >= line.length()) continue;
        int secondComma = line.indexOf(',', firstComma + 1);
        if (secondComma < 0 || secondComma + 1 >= line.length()) continue;
        char stateChar = line.charAt(firstComma + 1);
        if (stateChar == '1') {
            anyActive = true;
            int quote1 = line.indexOf('"', secondComma);
            int quote2 = (quote1 >= 0) ? line.indexOf('"', quote1 + 1) : -1;
            if (quote1 >= 0 && quote2 > quote1) {
                String ip = line.substring(quote1 + 1, quote2);
                ip.trim();
                ipOut = ip;
            }
            break;
        }
    }
    return anyActive;
}

void CatMGNSSModule::refreshDetachReason(const String& resp) {
    int idx = resp.indexOf("+CEER:");
    if (idx >= 0) {
        int lineEnd = resp.indexOf('\n', idx);
        String line = (lineEnd > idx) ? resp.substring(idx + 7, lineEnd) : resp.substring(idx + 7);
        line.trim();
        if (line.isEmpty()) {
            line = "No detail";
        }
        cellularData.lastDetachReason = line;
    }
}

bool CatMGNSSModule::configureAPN() {
    String response; bool ok=false;
    if (apn_.length()) {
        String cmd1 = String("AT+CNCFG=0,1,\"") + apn_ + "\"";
        ok = sendATCommand(cmd1, response, 3000) && response.indexOf("OK")>=0;
        if (!ok) {
            String cmd2 = String("AT+CNCFG=0,\"") + apn_ + "\"";
            ok = sendATCommand(cmd2, response, 3000) && response.indexOf("OK")>=0;
        }
        // Optional user/pass for some networks
        if (apnUser_.length()) {
            String userCmd = String("AT+CNCFG=0,3,\"") + apnUser_ + "\"";
            sendATCommand(userCmd, response, 3000);
        }
        if (apnPass_.length()) {
            String passCmd = String("AT+CNCFG=0,4,\"") + apnPass_ + "\"";
            ok = sendATCommand(passCmd, response, 3000) && response.indexOf("OK") >= 0;
        }
    }
    return ok;
}

bool CatMGNSSModule::activatePDP(uint32_t timeoutMs) {
    String response;
    uint32_t start = millis();
    const int MAX_RETRIES = 3;
    
    for (int attempt = 1; attempt <= MAX_RETRIES; attempt++) {
        Serial.printf("CatM+GNSS: PDP activation attempt %d/%d\n", attempt, MAX_RETRIES);
        
        // Check if PDP is already active before attempting activation
        if (sendATCommand("AT+CNACT?", response, 3000)) {
            bool anyActive = false;
            String ip;
            if (parseCNACTResponse(response, anyActive, ip) && anyActive && ip.length() > 0 && ip != "0.0.0.0") {
                Serial.printf("CatM+GNSS: PDP already active with IP: %s\n", ip.c_str());
                cellularData.hasIpAddress = true;
                cellularData.ipAddress = ip;
                return true;
            }
        }
        
        // Attempt PDP activation
        if (sendATCommand("AT+CNACT=0,1", response, 15000)) {
            if (response.indexOf("OK") >= 0) {
                Serial.println("CatM+GNSS: PDP activation command accepted");
                
                // Wait a moment for IP assignment
                vTaskDelay(pdMS_TO_TICKS(2000));
                
                // Verify IP assignment
                if (sendATCommand("AT+CNACT?", response, 5000)) {
                    bool anyActive = false;
                    String ip;
                    if (parseCNACTResponse(response, anyActive, ip) && anyActive && ip.length() > 0 && ip != "0.0.0.0") {
                        Serial.printf("CatM+GNSS: PDP activated successfully with IP: %s\n", ip.c_str());
                        cellularData.hasIpAddress = true;
                        cellularData.ipAddress = ip;
                        return true;
                    } else {
                        Serial.printf("CatM+GNSS: PDP activation OK but no valid IP assigned: %s\n", ip.c_str());
                    }
                }
            } else {
                Serial.printf("CatM+GNSS: PDP activation failed on attempt %d\n", attempt);
                // Get detailed error information
                if (sendATCommand("AT+CEER", response, 3000)) {
                    Serial.printf("CatM+GNSS: PDP error details: %s\n", response.c_str());
                }
            }
        }
        
        // Wait before retry with exponential backoff
        if (attempt < MAX_RETRIES) {
            uint32_t backoffMs = 2000 * (1 << (attempt - 1)); // 2s, 4s, 8s
            Serial.printf("CatM+GNSS: Waiting %ums before retry...\n", backoffMs);
            vTaskDelay(pdMS_TO_TICKS(backoffMs));
        }
    }
    
    Serial.println("CatM+GNSS: PDP activation failed after all retries");
    return false;
}

bool CatMGNSSModule::isGnssPowered(bool& powered) {
    String response;
    if (sendATCommand("AT+CGNSPWR?", response, 3000)) {
        if (response.indexOf("+CGNSPWR: 1") >= 0) {
            powered = true;
            return true;
        } else if (response.indexOf("+CGNSPWR: 0") >= 0) {
            powered = false;
            return true;
        }
    }
    powered = false;
    return false;
}

bool CatMGNSSModule::connectNetwork(const String& apn) {
    if (!isInitialized) return false;

    bool success = false;
    bool gnssWasOn = false;
    bool gnssSuspendAttempted = false;
    bool gnssSuspendSucceeded = false;

    String response;
    resetNetworkStats();

    if (isGnssPowered(gnssWasOn) && gnssWasOn) {
        Serial.println("CatM+GNSS: Suspending GNSS for network attach");
        gnssSuspendAttempted = true;
        gnssSuspendSucceeded = disableGNSS();
        if (!gnssSuspendSucceeded) {
            Serial.println("CatM+GNSS: Failed to disable GNSS prior to attach");
        } else {
            delay(800);
        }
    }

    do {
        if (!sendATCommand("AT+CPIN?", response, 2000) || response.indexOf("+CPIN: READY") < 0) {
            Serial.println("CatM+GNSS: SIM not ready");
            break;
        }

        sendATCommand("AT+CFUN=1", response, 5000);
        sendATCommand("AT+CMNB=1", response, 2000);
        sendATCommand("AT+CNMP=38", response, 2000);
        sendATCommand("AT+CGATT=1", response, 5000);
        sendATCommand("AT+COPS=0", response, 5000);

        if (!ensureRegistered(120000)) {
            Serial.println("CatM+GNSS: Not registered to network (CEREG) yet");
        }

        apn_ = apn;
        cellularData.apn = apn;
        if (!configureAPN()) {
            Serial.println("CatM+GNSS: Failed to set APN (CNCFG)");
            break;
        }

        if (!activatePDP(90000)) {
            Serial.println("CatM+GNSS: PDP activation failed on CNMP=38; trying auto RAT (CNMP=2)");
            sendATCommand("AT+CNMP=2", response, 2000);
            ensureRegistered(60000);
            if (!activatePDP(90000)) {
                Serial.println("CatM+GNSS: Failed to activate PDP (CNACT) after fallback");
                sendATCommand("AT+CNACT?", response, 3000);
                sendATCommand("AT+CEREG?", response, 3000);
                sendATCommand("AT+CGATT?", response, 3000);
                sendATCommand("AT+COPS?", response, 3000);
                sendATCommand("AT+CSQ", response, 2000);
                sendATCommand("AT+CEER", response, 3000);
                refreshDetachReason(response);
                break;
            }
        }

        sendATCommand("AT+CNACT?", response, 3000);
        bool anyActive = false;
        String ip;
        parseCNACTResponse(response, anyActive, ip);
        if (anyActive) {
            cellularData.hasIpAddress = ip.length() > 0;
            cellularData.ipAddress = ip;
        } else {
            cellularData.hasIpAddress = false;
            cellularData.ipAddress = "";
        }

        getOperatorName();
        getSignalStrength();

        cellularData.isConnected = true;
        cellularData.lastUpdate = millis();
        cellularData.lastDetachReason = "";
        resetNetworkStats();
        updateNetworkStats();

        getOperatorName();
        getSignalStrength();

        Serial.println("CatM+GNSS: Connected to network");
        success = true;
    } while (false);

    if (gnssSuspendAttempted && gnssWasOn) {
        if (gnssSuspendSucceeded) {
            delay(250);
            if (!enableGNSS()) {
                Serial.println("CatM+GNSS: Failed to resume GNSS after network attach");
            } else {
                Serial.println("CatM+GNSS: GNSS resumed after network attach");
            }
        } else {
            Serial.println("CatM+GNSS: GNSS was not disabled; skipping resume");
        }
    }

    if (!success) {
        cellularData.isConnected = false;
        cellularData.hasIpAddress = false;
        cellularData.ipAddress = "";
        resetNetworkStats();
    }

    return success;
}
bool CatMGNSSModule::connectNetwork(const String& apn, const String& user, const String& pass) {
    setApnCredentials(apn, user, pass);
    return connectNetwork(apn);
}

bool CatMGNSSModule::disconnectNetwork() {
    if (!isInitialized) return false;
    
    String response;
    // Deactivate PDP with SIM7080G CNACT command for context 0
    if (!sendATCommand("AT+CNACT=0,0", response, 10000) || response.indexOf("OK") < 0) {
        Serial.println("CatM+GNSS: Failed to deactivate PDP context");
        return false;
    }
    
    cellularData.isConnected = false;

    if (xEventGroupSystemStatus) {
        xEventGroupClearBits(xEventGroupSystemStatus, EVENT_BIT_CELLULAR_READY);
    }

    Serial.println("CatM+GNSS: Disconnected from network");
    return true;
}

bool CatMGNSSModule::isNetworkConnected() {
    if (!isInitialized) return false;

    String response;
    bool wasConnected = cellularData.isConnected;

    if (sendATCommand("AT+CNACT?", response, 2000)) {
        bool anyActive = false;
        String ip;
        parseCNACTResponse(response, anyActive, ip);
        if (anyActive) {
            cellularData.isConnected = true;
            cellularData.hasIpAddress = ip.length() > 0;
            cellularData.ipAddress = ip;
            cellularData.lastUpdate = millis();
            updateNetworkStats();
            return true;
        } else {
            cellularData.hasIpAddress = false;
            cellularData.ipAddress = "";
        }
    }

    if (sendATCommand("AT+CEREG?", response, 2000)) {
        int idx = response.indexOf("+CEREG:");
        if (idx >= 0) {
            int comma = response.indexOf(',', idx);
            if (comma > 0) {
                int end = response.indexOf(',', comma + 1);
                String stateStr = (end > comma) ? response.substring(comma + 1, end) : response.substring(comma + 1);
                stateStr.trim();
                uint8_t regState = (uint8_t)stateStr.toInt();
                updateRegistrationState(regState);
                if (regState == 1 || regState == 5) {
                    cellularData.isConnected = true;
                    cellularData.lastUpdate = millis();
                    updateNetworkStats();
                    return true;
                }
            }
        }
    }

    cellularData.isConnected = false;
    resetNetworkStats();
    if (wasConnected) {
        String diag;
        if (sendATCommand("AT+CEER", diag, 2000)) {
            refreshDetachReason(diag);
        }
        cellularData.errorCount++;
    }
    return false;
}


int8_t CatMGNSSModule::getSignalStrength() {
    if (!isInitialized) return -100;
    
    String response;
    if (!sendATCommand("AT+CSQ", response) || response.indexOf("+CSQ:") < 0) {
        return -100;
    }
    
    // Parse response
    int start = response.indexOf("+CSQ: ") + 6;
    int end = response.indexOf(",", start);
    if (start < 6 || end < 0) {
        return -100;
    }
    
    int csq = response.substring(start, end).toInt();
    
    // Convert CSQ to dBm
    int8_t rssi;
    if (csq == 99) {
        rssi = -100; // Unknown
    } else {
        rssi = -113 + (2 * csq); // Convert to dBm
    }
    
    // Update data
    cellularData.signalStrength = rssi;
    
    return rssi;
}

String CatMGNSSModule::getOperatorName() {
    if (!isInitialized) return "";
    
    String response;
    if (!sendATCommand("AT+COPS?", response) || response.indexOf("+COPS:") < 0) {
        return "";
    }
    
    // Parse response
    int start = response.indexOf("\",\"") + 3;
    int end = response.indexOf("\"", start);
    if (start < 3 || end < 0) {
        return "";
    }
    
    String operatorName = response.substring(start, end);
    
    // Update data
    cellularData.operatorName = operatorName;
    
    return operatorName;
}

String CatMGNSSModule::getIMEI() {
    if (!isInitialized) return "";
    
    String response;
    if (!sendATCommand("AT+GSN", response)) {
        return "";
    }
    
    // Parse response - IMEI is usually on its own line
    int start = response.indexOf("\r\n") + 2;
    int end = response.indexOf("\r\n", start);
    if (start < 2 || end < 0) {
        return "";
    }
    
    String imei = response.substring(start, end);
    
    // Update data
    cellularData.imei = imei;
    
    return imei;
}

bool CatMGNSSModule::softReset() {
    if (!serialModule || !serialMutex) {
        return false;
    }

    Serial.println("CatM+GNSS: Initiating soft reset (AT+CRESET)");
    char response[64];
    // We don't require success; some firmware resets before responding
    sendATCommand("AT+CRESET", response, sizeof(response), 2000);

    uint32_t start = millis();
    String line;
    bool readySeen = false;

    while (millis() - start < 20000) {
        while (serialModule->available()) {
            char c = serialModule->read();
            if (c == '\r') {
                continue;
            }
            if (c == '\n') {
                line.trim();
                if (line.length()) {
                    Serial.printf("CatM+GNSS: [URC] %s\n", line.c_str());
                    String upper = line;
                    upper.toUpperCase();
                    if (upper.indexOf("READY") >= 0 || upper.indexOf("APP RDY") >= 0 || upper.indexOf("PB DONE") >= 0) {
                        readySeen = true;
                        break;
                    }
                }
                line.clear();
            } else {
                line += c;
            }
        }
        if (readySeen) {
            break;
        }
        delay(50);
    }

    if (!readySeen) {
        Serial.println("CatM+GNSS: Soft reset timeout waiting for READY");
        lastError_ = "Modem did not become ready after reset";
        return false;
    }

    delay(200);
    // Flush any lingering bytes
    while (serialModule->available()) {
        serialModule->read();
    }

    bool atOk = false;
    for (int attempt = 0; attempt < 8 && !atOk; ++attempt) {
        if (testAT()) {
            atOk = true;
            break;
        }
        delay(500);
    }

    if (!atOk) {
        Serial.println("CatM+GNSS: AT not responding after reset");
        lastError_ = "AT timeout after reset";
        return false;
    }

    if (!applyBaselineConfig()) {
        Serial.printf("CatM+GNSS: Baseline config failed after reset: %s\n", lastError_.c_str());
        return false;
    }

    if (!configureNetworkTime()) {
        Serial.printf("CatM+GNSS: Network time config failed after reset: %s\n", lastError_.c_str());
        return false;
    }

    resetNetworkStats();
    state = CatMGNSSState::READY;
    lastError_.clear();
    Serial.println("CatM+GNSS: Soft reset complete");
    return true;
}

CellularData CatMGNSSModule::getCellularData() {
    return cellularData;
}
CellStatus CatMGNSSModule::getCellStatus() {
    return CellStatus::fromCellularData(cellularData, apn_, cellularData.imei);
}


// Data Transmission

bool CatMGNSSModule::parseNetDevStatus(const String& resp, uint64_t& txBytes, uint64_t& rxBytes, uint32_t& txBps, uint32_t& rxBps) {
    txBytes = 0;
    rxBytes = 0;
    txBps = 0;
    rxBps = 0;

    int idx = resp.indexOf("+NETDEVSTATUS");
    if (idx < 0) {
        return false;
    }

    int lineEnd = resp.indexOf('\n', idx);
    String line = (lineEnd > idx) ? resp.substring(idx, lineEnd) : resp.substring(idx);
    int colon = line.indexOf(':');
    if (colon >= 0) {
        line = line.substring(colon + 1);
    }
    line.replace("\r", "");
    line.trim();
    if (!line.length()) {
        return false;
    }

    uint64_t values[8];
    size_t count = 0;
    int start = 0;
    while (start < line.length() && count < 8) {
        int comma = line.indexOf(',', start);
        String token = (comma >= 0) ? line.substring(start, comma) : line.substring(start);
        token.trim();
        if (token.length()) {
            const char* raw = token.c_str();
            char* end = nullptr;
            unsigned long long value = strtoull(raw, &end, 10);
            if (end != raw) {
                values[count++] = value;
            }
        }
        if (comma < 0) {
            break;
        }
        start = comma + 1;
    }

    if (count >= 6) {
        txBytes = values[count - 4];
        rxBytes = values[count - 3];
        txBps = static_cast<uint32_t>(values[count - 2] > 0xFFFFFFFFULL ? 0xFFFFFFFFULL : values[count - 2]);
        rxBps = static_cast<uint32_t>(values[count - 1] > 0xFFFFFFFFULL ? 0xFFFFFFFFULL : values[count - 1]);
        return true;
    }

    if (count >= 4) {
        txBytes = values[count - 2];
        rxBytes = values[count - 1];
        return true;
    }

    return false;
}

bool CatMGNSSModule::updateNetworkStats() {
    if (!isInitialized || !cellularData.isConnected) {
        return false;
    }

    String response;
    if (!sendATCommand("AT+NETDEVSTATUS=0", response, 2000)) {
        return false;
    }

    uint64_t txBytes = 0;
    uint64_t rxBytes = 0;
    uint32_t txBps = 0;
    uint32_t rxBps = 0;
    if (!parseNetDevStatus(response, txBytes, rxBytes, txBps, rxBps)) {
        return false;
    }

    uint32_t now = millis();
    if (lastStatsSampleMs_ != 0 && (txBps == 0 && rxBps == 0)) {
        uint32_t deltaMs = now - lastStatsSampleMs_;
        if (deltaMs > 0) {
            uint64_t txDelta = (txBytes >= lastTxBytesSample_) ? (txBytes - lastTxBytesSample_) : 0;
            uint64_t rxDelta = (rxBytes >= lastRxBytesSample_) ? (rxBytes - lastRxBytesSample_) : 0;
            txBps = static_cast<uint32_t>((txDelta * 1000ULL) / deltaMs);
            rxBps = static_cast<uint32_t>((rxDelta * 1000ULL) / deltaMs);
        }
    }

    cellularData.txBytes = txBytes;
    cellularData.rxBytes = rxBytes;
    cellularData.txBps = txBps;
    cellularData.rxBps = rxBps;
    cellularData.lastUpdate = now;

    lastTxBytesSample_ = txBytes;
    lastRxBytesSample_ = rxBytes;
    lastStatsSampleMs_ = now;
    return true;
}

void CatMGNSSModule::resetNetworkStats() {
    cellularData.txBytes = 0;
    cellularData.rxBytes = 0;
    cellularData.txBps = 0;
    cellularData.rxBps = 0;
    lastTxBytesSample_ = 0;
    lastRxBytesSample_ = 0;
    lastStatsSampleMs_ = 0;
}

bool CatMGNSSModule::sendSMS(const String& number, const String& message) {
    if (!isInitialized) return false;
    
    String response;
    
    // Set SMS text mode
    if (!sendATCommand("AT+CMGF=1", response) || response.indexOf("OK") < 0) {
        Serial.println("CatM+GNSS: Failed to set SMS text mode");
        return false;
    }
    
    // Send SMS command
    String smsCmd = "AT+CMGS=\"" + number + "\"";
    if (!sendATCommand(smsCmd, response, 1000) || response.indexOf(">") < 0) {
        Serial.println("CatM+GNSS: Failed to start SMS");
        return false;
    }
    
    // Take mutex for direct write
    if (!serialMutex || xSemaphoreTake(serialMutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        Serial.println("CatM+GNSS: Failed to take mutex for SMS");
        return false;
    }
    
    // Send message content
    serialModule->print(message);
    serialModule->write(26); // Ctrl+Z to end message
    
    // Wait for response
    bool result = waitForResponse(response, 10000);
    
    // Release mutex
    xSemaphoreGive(serialMutex);
    
    if (!result || response.indexOf("+CMGS:") < 0) {
        Serial.println("CatM+GNSS: Failed to send SMS");
        return false;
    }
    
    Serial.println("CatM+GNSS: SMS sent successfully");
    return true;
}

bool CatMGNSSModule::sendHTTP(const String& url, const String& data, String& response) {
    if (!isInitialized || !cellularData.isConnected) return false;
    
    String atResponse;
    
    // Initialize HTTP service
    if (!sendATCommand("AT+HTTPINIT", atResponse) || atResponse.indexOf("OK") < 0) {
        Serial.println("CatM+GNSS: Failed to initialize HTTP");
        return false;
    }
    
    // Set HTTP parameters - URL
    String urlCmd = "AT+HTTPPARA=\"URL\",\"" + url + "\"";
    if (!sendATCommand(urlCmd, atResponse) || atResponse.indexOf("OK") < 0) {
        Serial.println("CatM+GNSS: Failed to set URL");
        sendATCommand("AT+HTTPTERM", atResponse); // Terminate HTTP service
        return false;
    }

    // Determine if this is GET or POST based on data presence
    bool isGetRequest = (data.length() == 0);

    // Set HTTP parameters - Content Type (needed for both GET and POST)
    if (!sendATCommand("AT+HTTPPARA=\"CONTENT\",\"application/json\"", atResponse) || atResponse.indexOf("OK") < 0) {
        Serial.println("CatM+GNSS: Failed to set content type");
        sendATCommand("AT+HTTPTERM", atResponse); // Terminate HTTP service
        return false;
    }

    if (!isGetRequest) {
        // Set HTTP data (only for POST)
        String dataCmd = "AT+HTTPDATA=" + String(data.length()) + ",10000";
        if (!sendATCommand(dataCmd, atResponse, 5000) || atResponse.indexOf("DOWNLOAD") < 0) {
            Serial.println("CatM+GNSS: Failed to start HTTP data");
            sendATCommand("AT+HTTPTERM", atResponse); // Terminate HTTP service
            return false;
        }
        
        // Take mutex for direct write
        if (!serialMutex || xSemaphoreTake(serialMutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
            Serial.println("CatM+GNSS: Failed to take mutex for HTTP data");
            sendATCommand("AT+HTTPTERM", atResponse); // Terminate HTTP service
            return false;
        }
        
        // Send data
        serialModule->print(data);
        delay(500);
        
        // Wait for response
        bool result = waitForResponse(atResponse, 5000);
        
        // Release mutex
        xSemaphoreGive(serialMutex);
        
        if (!result || atResponse.indexOf("OK") < 0) {
            Serial.println("CatM+GNSS: Failed to send HTTP data");
            sendATCommand("AT+HTTPTERM", atResponse); // Terminate HTTP service
            return false;
        }
    }
    
    // Send HTTP request (GET=0, POST=1)
    String actionCmd = isGetRequest ? "AT+HTTPACTION=0" : "AT+HTTPACTION=1";
    String expectedResponse = isGetRequest ? "+HTTPACTION: 0," : "+HTTPACTION: 1,";
    
    if (!sendATCommand(actionCmd, atResponse, 30000) || atResponse.indexOf(expectedResponse) < 0) {
        Serial.printf("CatM+GNSS: Failed to send HTTP %s\n", isGetRequest ? "GET" : "POST");
        sendATCommand("AT+HTTPTERM", atResponse); // Terminate HTTP service
        return false;
    }
    
    // Get HTTP response
    if (!sendATCommand("AT+HTTPREAD", atResponse, 10000) || atResponse.indexOf("+HTTPREAD:") < 0) {
        Serial.println("CatM+GNSS: Failed to read HTTP response");
        sendATCommand("AT+HTTPTERM", atResponse); // Terminate HTTP service
        return false;
    }
    
    // Parse response
    int start = atResponse.indexOf("+HTTPREAD:") + 10;
    int end = atResponse.indexOf("\r\n\r\nOK", start);
    if (start < 10 || end < 0) {
        response = "";
    } else {
        // Find the actual content start (skip the content length line)
        start = atResponse.indexOf("\r\n", start) + 2;
        response = atResponse.substring(start, end);
    }
    
    // Terminate HTTP service
    sendATCommand("AT+HTTPTERM", atResponse);
    
    Serial.printf("CatM+GNSS: HTTP %s request sent successfully\n", isGetRequest ? "GET" : "POST");
    return true;
}

bool CatMGNSSModule::sendJSON(const String& url, JsonDocument& json, String& response) {
    // Serialize JSON
    String jsonStr;
    serializeJson(json, jsonStr);
    
    // Send HTTP request
    return sendHTTP(url, jsonStr, response);
}

void CatMGNSSModule::printStatus() {
    Serial.println("=== CatM+GNSS Module Status ===");
    Serial.printf("Initialized: %s\n", isInitialized ? "YES" : "NO");
    Serial.printf("State: %d\n", (int)state);
    
    Serial.println("--- GNSS Status ---");
    Serial.printf("Valid Fix: %s\n", gnssData.isValid ? "YES" : "NO");
    Serial.printf("Satellites: %d\n", gnssData.satellites);
    if (gnssData.isValid) {
        Serial.printf("Position: %.6f, %.6f\n", gnssData.latitude, gnssData.longitude);
        Serial.printf("Altitude: %.1f m\n", gnssData.altitude);
        Serial.printf("Speed: %.1f km/h\n", gnssData.speed);
    }
    
    Serial.println("--- Cellular Status ---");
    Serial.printf("Connected: %s\n", cellularData.isConnected ? "YES" : "NO");
    Serial.printf("Operator: %s\n", cellularData.operatorName.c_str());
    Serial.printf("Signal: %d dBm\n", cellularData.signalStrength);
    Serial.printf("IMEI: %s\n", cellularData.imei.c_str());
    Serial.printf("Error Count: %d\n", cellularData.errorCount);
    
    Serial.println("============================");
}

bool CatMGNSSModule::getNetworkTimeZoneQuarters(int& tzQuarters) {
    tzQuarters = 0;
    if (!isInitialized) return false;
    String r;
    if (!sendATCommand("AT+CCLK?", r, 2000)) return false;
    int q1 = r.indexOf('"');
    int q2 = (q1 >= 0) ? r.indexOf('"', q1 + 1) : -1;
    if (q1 < 0 || q2 < 0) return false;
    String ts = r.substring(q1 + 1, q2); // yy/MM/dd,hh:mm:ss±tz
    int plus = ts.lastIndexOf('+');
    int minus = ts.lastIndexOf('-');
    int sgnPos = max(plus, minus);
    if (sgnPos < 0 || sgnPos + 1 >= ts.length()) return false;
    int sign = (ts.charAt(sgnPos) == '+') ? 1 : -1;
    // Extract digits after sign
    String tzstr = ts.substring(sgnPos + 1);
    // Some modules return hours (e.g. 08); others quarter-hours (e.g. 32)
    int val = tzstr.toInt();
    if (tzstr.length() <= 2 && val <= 12) {
        tzQuarters = sign * (val * 4);
    } else {
        tzQuarters = sign * val; // assume already in quarter-hours
    }
    return true;
}

bool CatMGNSSModule::getNetworkTime(struct tm& timeInfo) {
    if (!isInitialized) return false;
    
    String response;
    if (!sendATCommand("AT+CCLK?", response, 2000)) return false;
    
    // Parse response: +CCLK: "yy/MM/dd,hh:mm:ss±tz"
    int q1 = response.indexOf('"');
    int q2 = (q1 >= 0) ? response.indexOf('"', q1 + 1) : -1;
    if (q1 < 0 || q2 < 0) return false;
    
    String timeStr = response.substring(q1 + 1, q2);
    
    // Parse date and time
    int commaPos = timeStr.indexOf(',');
    if (commaPos < 0) return false;
    
    String dateStr = timeStr.substring(0, commaPos); // yy/MM/dd
    String timePart = timeStr.substring(commaPos + 1); // hh:mm:ss±tz
    
    // Parse date
    int slash1 = dateStr.indexOf('/');
    int slash2 = dateStr.indexOf('/', slash1 + 1);
    if (slash1 < 0 || slash2 < 0) return false;
    
    int year = 2000 + dateStr.substring(0, slash1).toInt();
    int month = dateStr.substring(slash1 + 1, slash2).toInt();
    int day = dateStr.substring(slash2 + 1).toInt();
    
    // Parse time
    int colon1 = timePart.indexOf(':');
    int colon2 = timePart.indexOf(':', colon1 + 1);
    int tzPos = timePart.lastIndexOf('+');
    if (tzPos < 0) tzPos = timePart.lastIndexOf('-');
    if (colon1 < 0 || colon2 < 0 || tzPos < 0) return false;
    
    int hour = timePart.substring(0, colon1).toInt();
    int minute = timePart.substring(colon1 + 1, colon2).toInt();
    int second = timePart.substring(colon2 + 1, tzPos).toInt();
    
    // Validate time
    if (year < 2020 || month < 1 || month > 12 || day < 1 || day > 31 ||
        hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 59) {
        return false;
    }
    
    // Fill tm structure
    timeInfo.tm_year = year - 1900;  // tm_year is years since 1900
    timeInfo.tm_mon = month - 1;     // tm_mon is 0-11
    timeInfo.tm_mday = day;
    timeInfo.tm_hour = hour;
    timeInfo.tm_min = minute;
    timeInfo.tm_sec = second;
    timeInfo.tm_isdst = 0; // Network time is typically UTC
    
    // Calculate day of week
    int m = month;
    int y = year;
    if (m < 3) {
        m += 12;
        y -= 1;
    }
    int dayOfWeek = (day + (13 * (m + 1)) / 5 + y + y / 4 - y / 100 + y / 400) % 7;
    timeInfo.tm_wday = (dayOfWeek + 5) % 7; // Adjust to 0=Sunday
    
    return true;
}

bool CatMGNSSModule::configureNetworkTime() {
    if (!serialModule || !serialMutex) {
        lastError_ = "Serial interface not ready";
        networkTimeConfigured_ = false;
        return false;
    }

    Serial.println("CatM+GNSS: Configuring network time settings...");
    char resp[256];
    const int MAX_RETRIES = 3;

    // Make sure we reset flags before attempting configuration
    networkTimeConfigured_ = false;
    networkTimeSynced_ = false;
    lastNetworkTimeSyncMs_ = 0;
    memset(&lastNetworkUtc_, 0, sizeof(lastNetworkUtc_));

    // AT+CLTS=1: Enable network time latch
    for (int attempt = 1; attempt <= MAX_RETRIES; attempt++) {
        if (sendATCommand("AT+CLTS=1", resp, sizeof(resp), 1000) && strstr(resp, POOL_STRING("OK")) != nullptr) {
            Serial.println("CatM+GNSS: Network time latch enabled (AT+CLTS=1)");
            break;
        }
        if (attempt == MAX_RETRIES) {
            lastError_ = "Failed to enable network time latch (AT+CLTS=1) after retries";
            Serial.printf("CatM+GNSS: ERROR - %s\n", lastError_.c_str());
            return false;
        }
        Serial.printf("CatM+GNSS: AT+CLTS=1 attempt %d failed, retrying...\n", attempt);
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    // AT+CNTPCID=1: Bind CNTP to PDP context 1
    for (int attempt = 1; attempt <= MAX_RETRIES; attempt++) {
        if (sendATCommand("AT+CNTPCID=1", resp, sizeof(resp), 1000) && strstr(resp, POOL_STRING("OK")) != nullptr) {
            Serial.println("CatM+GNSS: CNTP bound to PDP context 1 (AT+CNTPCID=1)");
            break;
        }
        if (attempt == MAX_RETRIES) {
            lastError_ = "Failed to bind CNTP to PDP context 1 (AT+CNTPCID=1) after retries";
            Serial.printf("CatM+GNSS: ERROR - %s\n", lastError_.c_str());
            return false;
        }
        Serial.printf("CatM+GNSS: AT+CNTPCID=1 attempt %d failed, retrying...\n", attempt);
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    // AT+CNTP: Configure Soracom NTP server
    for (int attempt = 1; attempt <= MAX_RETRIES; attempt++) {
        if (sendATCommand("AT+CNTP=\"ntp.soracom.io\",0", resp, sizeof(resp), 1000) && strstr(resp, POOL_STRING("OK")) != nullptr) {
            Serial.println("CatM+GNSS: Soracom NTP server configured (AT+CNTP)");
            networkTimeConfigured_ = true;
            lastError_.clear();
            Serial.println("CatM+GNSS: Network time configuration complete");
            return true;
        }
        if (attempt == MAX_RETRIES) {
            lastError_ = "Failed to configure Soracom NTP server (AT+CNTP) after retries";
            Serial.printf("CatM+GNSS: ERROR - %s\n", lastError_.c_str());
            return false;
        }
        Serial.printf("CatM+GNSS: AT+CNTP attempt %d failed, retrying...\n", attempt);
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    // Should not reach here
    return false;
}

bool CatMGNSSModule::syncNetworkTime(struct tm& utcOut, uint32_t timeoutMs) {
    if (!isInitialized) {
        lastError_ = "Module not initialized";
        networkTimeSynced_ = false;
        return false;
    }
    if (!networkTimeConfigured_) {
        Serial.println("CatM+GNSS: Network time not configured, applying configuration...");
        if (!configureNetworkTime()) {
            Serial.printf("CatM+GNSS: Failed to configure network time: %s\n", lastError_.c_str());
            networkTimeSynced_ = false;
            return false;
        }
    }
    
    Serial.printf("CatM+GNSS: Starting NTP sync (timeout %u ms)...\n", timeoutMs);
    
    // Clear UART buffer
    if (xSemaphoreTake(serialMutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        lastError_ = "Failed to acquire serial mutex";
        networkTimeSynced_ = false;
        return false;
    }
    
    while (serialModule->available()) {
        serialModule->read();
    }
    
    // Send AT+CNTPSTART
    Serial.println("CatM+GNSS: >>> AT+CNTPSTART");
    serialModule->println("AT+CNTPSTART");
    
    // Wait for URC response: +CNTP: <result>,"<time>"
    char response[256] = {0};
    uint32_t start = millis();
    bool foundCNTP = false;
    int responseLen = 0;
    
    while (millis() - start < timeoutMs) {
        if (serialModule->available()) {
            char c = serialModule->read();
            if (responseLen < sizeof(response) - 1) {
                response[responseLen++] = c;
                response[responseLen] = '\0';
            }
            
            // Check if we have a complete line
            if (c == '\n') {
                // Trim whitespace
                while (responseLen > 0 && (response[responseLen-1] == '\r' || response[responseLen-1] == '\n' || response[responseLen-1] == ' ')) {
                    response[--responseLen] = '\0';
                }
                
                Serial.printf("CatM+GNSS: <<< %s\n", response);
                
                // Look for +CNTP: response
                if (strncmp(response, "+CNTP:", 6) == 0) {
                    foundCNTP = true;
                    break;
                }
                
                responseLen = 0;
                response[0] = '\0';
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    
    xSemaphoreGive(serialMutex);
    
    if (!foundCNTP) {
        lastError_ = "NTP sync timeout - no +CNTP response received";
        Serial.printf("CatM+GNSS: ERROR - %s\n", lastError_.c_str());
        networkTimeSynced_ = false;
        return false;
    }
    
    // Parse +CNTP: <result>,"<time>"
    // Expected format: +CNTP: 1,"2025/10/08,14:23:45"
    char* colonPos = strchr(response, ':');
    if (!colonPos) {
        lastError_ = "Invalid +CNTP response format (no colon)";
        return false;
    }
    
    char* payload = colonPos + 1;
    // Trim leading whitespace
    while (*payload == ' ' || *payload == '\t') payload++;
    
    // Extract result code
    char* commaPos = strchr(payload, ',');
    if (!commaPos) {
        lastError_ = "Invalid +CNTP response format (no comma)";
        return false;
    }
    
    int resultCode = atoi(payload);
    
    // Handle error codes
    if (resultCode == 2) {
        lastError_ = "NTP sync failed - network error (result code 2)";
        Serial.printf("CatM+GNSS: ERROR - %s\n", lastError_.c_str());
        networkTimeSynced_ = false;
        return false;
    } else if (resultCode == 3) {
        lastError_ = "NTP sync failed - DNS resolution error (result code 3)";
        Serial.printf("CatM+GNSS: ERROR - %s\n", lastError_.c_str());
        networkTimeSynced_ = false;
        return false;
    } else if (resultCode == 4) {
        lastError_ = "NTP sync failed - timeout (result code 4)";
        Serial.printf("CatM+GNSS: ERROR - %s\n", lastError_.c_str());
        networkTimeSynced_ = false;
        return false;
    } else if (resultCode != 1) {
        lastError_ = "NTP sync failed - unknown result code " + String(resultCode);
        Serial.printf("CatM+GNSS: ERROR - %s\n", lastError_.c_str());
        networkTimeSynced_ = false;
        return false;
    }
    
    // Extract timestamp from quotes
    char* q1 = strchr(payload, '"');
    if (!q1) {
        lastError_ = "Invalid +CNTP response format (missing quotes)";
        return false;
    }
    char* q2 = strchr(q1 + 1, '"');
    if (!q2) {
        lastError_ = "Invalid +CNTP response format (missing closing quote)";
        return false;
    }
    
    // Extract time string between quotes
    char timeStr[32];
    int timeLen = q2 - q1 - 1;
    if (timeLen >= sizeof(timeStr)) {
        lastError_ = "Invalid +CNTP timestamp format (too long)";
        return false;
    }
    strncpy(timeStr, q1 + 1, timeLen);
    timeStr[timeLen] = '\0';
    
    // Format: YYYY/MM/DD,HH:MM:SS
    char* commaTime = strchr(timeStr, ',');
    if (!commaTime) {
        lastError_ = "Invalid +CNTP timestamp format (no comma in time)";
        return false;
    }
    
    char* dateStr = timeStr; // YYYY/MM/DD
    char* clockStr = commaTime + 1; // HH:MM:SS
    *commaTime = '\0'; // Split the string
    
    // Parse date: YYYY/MM/DD
    char* slash1 = strchr(dateStr, '/');
    if (!slash1) {
        lastError_ = "Invalid +CNTP date format (no first slash)";
        return false;
    }
    char* slash2 = strchr(slash1 + 1, '/');
    if (!slash2) {
        lastError_ = "Invalid +CNTP date format (no second slash)";
        return false;
    }
    
    int year = atoi(dateStr);
    int month = atoi(slash1 + 1);
    int day = atoi(slash2 + 1);
    
    // Parse time: HH:MM:SS
    char* colon1 = strchr(clockStr, ':');
    if (!colon1) {
        lastError_ = "Invalid +CNTP time format (no first colon)";
        return false;
    }
    char* colon2 = strchr(colon1 + 1, ':');
    if (!colon2) {
        lastError_ = "Invalid +CNTP time format (no second colon)";
        return false;
    }
    
    int hour = atoi(clockStr);
    int minute = atoi(colon1 + 1);
    int second = atoi(colon2 + 1);
    
    // Validate ranges
    if (year < 2020 || month < 1 || month > 12 || day < 1 || day > 31 ||
        hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 59) {
        lastError_ = "Invalid +CNTP timestamp values";
        networkTimeSynced_ = false;
        return false;
    }
    
    // Fill tm structure
    utcOut.tm_year = year - 1900;  // tm_year is years since 1900
    utcOut.tm_mon = month - 1;     // tm_mon is 0-11
    utcOut.tm_mday = day;
    utcOut.tm_hour = hour;
    utcOut.tm_min = minute;
    utcOut.tm_sec = second;
    utcOut.tm_isdst = 0; // UTC has no DST
    
    // Calculate day of week (Zeller's congruence)
    int m = month;
    int y = year;
    if (m < 3) {
        m += 12;
        y -= 1;
    }
    int dayOfWeek = (day + (13 * (m + 1)) / 5 + y + y / 4 - y / 100 + y / 400) % 7;
    utcOut.tm_wday = (dayOfWeek + 5) % 7; // Adjust to 0=Sunday
    
    Serial.printf("CatM+GNSS: NTP sync success - %04d/%02d/%02d %02d:%02d:%02d UTC\n",
                  year, month, day, hour, minute, second);

    networkTimeSynced_ = true;
    lastNetworkTimeSyncMs_ = millis();
    lastNetworkUtc_ = utcOut;
    lastError_.clear();
    
    return true;
}

// FreeRTOS Task Function
void CatMGNSSModule::taskFunction(void* pvParameters) {
    CatMGNSSModule* module = (CatMGNSSModule*)pvParameters;
    
    if (!module) {
        Serial.println("CatM+GNSS: Invalid task parameters");
        vTaskDelete(NULL);
        return;
    }
    
    Serial.println("CatM+GNSS: Task started");
    
    // Ensure GNSS powered initially
    if (!module->enableGNSS()) {
        Serial.println("CatM+GNSS: Failed to enable GNSS");
    }
    
    for (;;) {
        // Update GNSS data; if GNSS isn't running, attempt to re-enable periodically
        bool gnssOk = module->updateGNSSData();
        static uint32_t lastGnssPowerCheck = 0;
        if (!gnssOk && millis() - lastGnssPowerCheck > 5000) {
            lastGnssPowerCheck = millis();
            char r[128];
            if (module->sendATCommand("AT+CGNSPWR?", r, sizeof(r), 1500)) {
                char* idx = strstr(r, "+CGNSPWR:");
                if (idx) {
                    // Expect format: +CGNSPWR: 0 or 1
                    char* eol = strchr(idx, '\n');
                    if (eol) *eol = '\0';
                    bool powered = strstr(idx, ": 1") != nullptr || strstr(idx, ":1") != nullptr;
                    if (!powered) {
                        Serial.println("[CATM_GNSS_TASK] GNSS power off, enabling...");
                        module->enableGNSS();
                    }
                }
            }
        }
        
        // Check cellular status
        static uint32_t lastPdpCheck = 0;
        static uint32_t backoffMs = 5000; // start with 5s
        static bool wasConnected = false;
        bool nowConnected = module->isNetworkConnected();
        
        if (nowConnected) {
            module->getSignalStrength();
            backoffMs = 5000; // reset backoff on good state
            
            // Network just came up - sync time once per attach
            if (!wasConnected) {
                Serial.println("[CATM_GNSS_TASK] Network connected, syncing NTP time...");
                struct tm utcTime;
                if (module->syncNetworkTime(utcTime, 65000)) {
                    module->cellularData.lastUpdate = millis();
                    Serial.printf("[CATM_GNSS_TASK] NTP sync success, UTC %04d-%02d-%02d %02d:%02d:%02d\n",
                                  utcTime.tm_year + 1900,
                                  utcTime.tm_mon + 1,
                                  utcTime.tm_mday,
                                  utcTime.tm_hour,
                                  utcTime.tm_min,
                                  utcTime.tm_sec);
                } else {
                    Serial.printf("[CATM_GNSS_TASK] NTP sync failed: %s\n", module->getLastError().c_str());
                }
            }
        } else {
            // Periodically attempt PDP ensure/reattach using stored APN
            if (millis() - lastPdpCheck > backoffMs) {
                lastPdpCheck = millis();
                if (module->getApn().length()) {
                    Serial.println("[CATM_GNSS_TASK] PDP not active, attempting reattach...");
                    // Ensure registration first (20s), then APN config, then PDP activation (30s)
                    if (!module->ensureRegistered(20000)) {
                        Serial.println("[CATM_GNSS_TASK] Registration not ready, will retry");
                    } else if (!module->configureAPN()) {
                        Serial.println("[CATM_GNSS_TASK] APN configure failed");
                    } else if (!module->activatePDP(30000)) {
                        Serial.println("[CATM_GNSS_TASK] PDP activation failed");
                    } else {
                        Serial.println("[CATM_GNSS_TASK] PDP reattached");
                        backoffMs = 5000; // success, reset backoff
                    }
                    // Exponential backoff capped at 2 minutes
                    backoffMs = min<uint32_t>(backoffMs * 2, 120000);
                }
            }
        }
        
        // Update connection state tracking
        wasConnected = nowConnected;
        
        // Print status every 10 seconds
        static uint32_t lastStatusTime = 0;
        if (millis() - lastStatusTime > 10000) {
            module->printStatus();
            CellularData cd = module->getCellularData();
            GNSSData gd = module->getGNSSData();
            logbuf_printf("CELL %s RSSI=%d op=%s | GNSS %s sats=%u",
                       cd.isConnected ? "UP" : "DOWN", (int)cd.signalStrength, cd.operatorName.c_str(),
                       gd.isValid ? "FIX" : "NOFIX", (unsigned)gd.satellites);
            lastStatusTime = millis();
        }
        
        // Sleep for a short time
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
