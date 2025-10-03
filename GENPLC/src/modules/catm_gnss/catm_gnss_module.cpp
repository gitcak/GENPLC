#include "catm_gnss_module.h"
#include "modules/logging/log_buffer.h"

CatMGNSSModule::CatMGNSSModule() {
    isInitialized = false;
    state = CatMGNSSState::INITIALIZING;
    serialModule = nullptr;
    
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
    // Check if mutex was created successfully
    if (!serialMutex) {
        Serial.println("CatM+GNSS: Serial mutex not available");
        return false;
    }
    
    // Initialize serial
    serialModule = new HardwareSerial(2); // Use UART2 for CatM+GNSS module (avoid conflicts)
    if (!serialModule) {
        Serial.println("CatM+GNSS: Failed to create serial instance");
        return false;
    }
    
    // Try known UART pin pairs (Port C and Port A) with both RX/TX permutations
    const int pinPairs[][2] = {
        {CATM_GNSS_RX_PIN, CATM_GNSS_TX_PIN}, // Port C expected
        {CATM_GNSS_TX_PIN, CATM_GNSS_RX_PIN}, // Port C swapped
        {1, 2},  // Port A expected (host RX=1 white, TX=2 yellow)
        {2, 1}   // Port A swapped
    };

    // Fail-fast probe window to avoid blocking boot when module is absent
    // Total budget ~3000ms across all permutations
    const uint32_t kProbeBudgetMs = 3000;
    uint32_t probeStart = millis();

    bool at_ok = false;
    for (int i = 0; i < 4 && !at_ok; ++i) {
        if (millis() - probeStart > kProbeBudgetMs) break; // out of time; fail fast
        int rx = pinPairs[i][0];
        int tx = pinPairs[i][1];
        Serial.printf("CatM+GNSS: Trying UART2 on RX:%d TX:%d @%d\n", rx, tx, CATM_GNSS_BAUD_RATE);
        serialModule->end();
        serialModule->begin(CATM_GNSS_BAUD_RATE, SERIAL_8N1, rx, tx);
        // Short settle; avoid long blocking
        delay(150);
        while (serialModule->available()) { serialModule->read(); }
        Serial.println("CatM+GNSS: Probing modem with AT...");
        // Quick single-shot AT probe (300ms)
        {
            String quick;
            if (sendATCommand("AT", quick, 300) && quick.indexOf("OK") >= 0) {
                at_ok = true;
                Serial.printf("CatM+GNSS: AT OK on RX:%d TX:%d\n", rx, tx);
                break;
            }
        }
        if (millis() - probeStart > kProbeBudgetMs) break; // budget spent

        // One lightweight attempt with echo off and identify
        String resp;
        sendATCommand("ATE0", resp, 300);
        sendATCommand("ATI", resp, 300);
        // Retry quick AT once more (300ms)
        if (sendATCommand("AT", resp, 300) && resp.indexOf("OK") >= 0) {
            at_ok = true;
            Serial.printf("CatM+GNSS: AT OK on RX:%d TX:%d\n", rx, tx);
            break;
        }
    }

    if (!at_ok) {
        Serial.println("CatM+GNSS: Modem not detected (absent). Booting without CatM/GNSS.");
        return false;
    }
    
    // Basic modem setup for SIM7080
    String resp;
    sendATCommand("AT+CMEE=2", resp, 1000); // verbose errors
    sendATCommand("AT+CFUN=1", resp, 5000); // full functionality
    sendATCommand("AT+CMNB=1", resp, 2000); // Cat-M only
    sendATCommand("AT+CNMP=38", resp, 2000); // LTE Cat-M1 mode
    sendATCommand("AT+COPS=0", resp, 5000);  // automatic operator selection
    
    Serial.println("CatM+GNSS: Module initialized successfully");
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
    String response;
    sendATCommand("AT+CPOWD=1", response, 5000);
    
    isInitialized = false;
    state = CatMGNSSState::INITIALIZING;
}

bool CatMGNSSModule::testAT() {
    String response;
    for (int i = 0; i < 3; i++) {
        if (sendATCommand("AT", response, 1000) && response.indexOf("OK") >= 0) {
            return true;
        }
        delay(500);
    }
    return false;
}

bool CatMGNSSModule::sendATCommand(const String& command, String& response, uint32_t timeout) {
    if (!serialModule) return false;
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
    Serial.println("CatM+GNSS: >>> " + command); // Debug output
    serialModule->println(command);
    
    // Wait for response
    bool result = waitForResponse(response, timeout);
    
    // Release mutex
    xSemaphoreGive(serialMutex);
    
    return result;
}

bool CatMGNSSModule::waitForResponse(String& response, uint32_t timeout) {
    response = "";
    uint32_t start = millis();
    
    while (millis() - start < timeout) {
        if (serialModule->available()) {
            char c = serialModule->read();
            response += c;
            
            // Check if we have a complete response
            if (response.indexOf("\r\nOK\r\n") >= 0 ||
                response.indexOf("\nOK\r\n") >= 0 ||
                response.endsWith("OK\r\n") ||
                response.indexOf("\r\nERROR\r\n") >= 0 || 
                response.indexOf("+CME ERROR") > 0) {
                Serial.println("CatM+GNSS: <<< " + response); // Debug output
                return true;
            }
        }
        delay(10);
    }
    
    // Timeout
    Serial.println("CatM+GNSS: <<< TIMEOUT: " + response); // Debug output
    return false;
}

// GNSS Functions
bool CatMGNSSModule::enableGNSS() {
    if (!isInitialized) return false;
    
    String response;
    // Power on GNSS
    if (!sendATCommand("AT+CGNSPWR=1", response, 2000) || response.indexOf("OK") < 0) {
        Serial.println("CatM+GNSS: Failed to power on GNSS");
        return false;
    }
    
    // Configure GNSS output format (optional; some firmware may not support this)
    if (!sendATCommand("AT+CGNSSEQ=\"RMC\"", response)) {
        Serial.println("CatM+GNSS: GNSS sequence config not supported; continuing");
    }
    
    Serial.println("CatM+GNSS: GNSS enabled successfully");
    return true;
}

bool CatMGNSSModule::disableGNSS() {
    if (!isInitialized) return false;
    
    String response;
    if (!sendATCommand("AT+CGNSPWR=0", response) || response.indexOf("OK") < 0) {
        Serial.println("CatM+GNSS: Failed to power off GNSS");
        return false;
    }
    
    Serial.println("CatM+GNSS: GNSS disabled successfully");
    return true;
}

bool CatMGNSSModule::updateGNSSData() {
    if (!isInitialized) return false;
    
    String response;
    if (!sendATCommand("AT+CGNSINF", response, 2000)) {
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
    // Try CNACT with retries
    while (millis() - start < timeoutMs) {
        if (sendATCommand("AT+CNACT=0,1", response, 15000)) {
            if (response.indexOf("OK") >= 0) {
                return true;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
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
    if (!sendATCommand("AT+CGACT=0,1", response) || response.indexOf("OK") < 0) {
        Serial.println("CatM+GNSS: Failed to deactivate PDP context");
        return false;
    }
    
    cellularData.isConnected = false;
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
                    return true;
                }
            }
        }
    }

    cellularData.isConnected = false;
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

CellularData CatMGNSSModule::getCellularData() {
    return cellularData;
}

// Data Transmission
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
    
    // Set HTTP parameters - Content Type
    if (!sendATCommand("AT+HTTPPARA=\"CONTENT\",\"application/json\"", atResponse) || atResponse.indexOf("OK") < 0) {
        Serial.println("CatM+GNSS: Failed to set content type");
        sendATCommand("AT+HTTPTERM", atResponse); // Terminate HTTP service
        return false;
    }
    
    // Set HTTP data
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
    
    // Send HTTP POST request
    if (!sendATCommand("AT+HTTPACTION=1", atResponse, 30000) || atResponse.indexOf("+HTTPACTION: 1,") < 0) {
        Serial.println("CatM+GNSS: Failed to send HTTP POST");
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
    
    Serial.println("CatM+GNSS: HTTP request sent successfully");
    return true;
}

bool CatMGNSSModule::sendJSON(const String& url, JsonDocument& json, String& response) {
    // Serialize JSON
    String jsonStr;
    serializeJson(json, jsonStr);
    
    // Send HTTP request
    return sendHTTP(url, jsonStr, response);
}

// ============================= MQTT (AT+SM*) =============================
bool CatMGNSSModule::mqttConfig(const String& host, uint16_t port, const String& user, const String& pass, const String& clientId) {
    if (!isInitialized) return false;
    String r;
    // Set MQTT version and clean session where supported
    sendATCommand("AT+SMCONF=\"URL\",\"" + host + "," + String(port) + "\"", r, 2000);
    if (user.length()) sendATCommand("AT+SMCONF=\"USERNAME\",\"" + user + "\"", r, 2000);
    if (pass.length()) sendATCommand("AT+SMCONF=\"PASSWORD\",\"" + pass + "\"", r, 2000);
    String cid = clientId.length() ? clientId : String("stamplc-") + String((uint32_t)ESP.getEfuseMac(), HEX);
    sendATCommand("AT+SMCONF=\"CLIENTID\",\"" + cid + "\"", r, 2000);
    mqttConfigured_ = true;
    return true;
}

bool CatMGNSSModule::mqttConnect(uint32_t timeoutMs) {
    if (!isInitialized || !mqttConfigured_) return false;
    String r;
    return sendATCommand("AT+SMCONN", r, timeoutMs) && r.indexOf("OK") >= 0;
}

bool CatMGNSSModule::mqttPublish(const String& topic, const String& payload, int qos, bool retain) {
    if (!isInitialized) return false;
    String r;
    // AT+SMPUB="topic",len,qos,retain
    String cmd = String("AT+SMPUB=\"") + topic + "\"," + String(payload.length()) + "," + String(qos) + "," + String(retain ? 1 : 0);
    if (!sendATCommand(cmd, r, 5000) || r.indexOf(">") < 0) return false;
    // Direct write
    if (!serialMutex || xSemaphoreTake(serialMutex, pdMS_TO_TICKS(1000)) != pdTRUE) return false;
    serialModule->print(payload);
    bool ok = waitForResponse(r, 10000) && r.indexOf("OK") >= 0;
    xSemaphoreGive(serialMutex);
    return ok;
}

bool CatMGNSSModule::mqttDisconnect() {
    if (!isInitialized) return false;
    String r; return sendATCommand("AT+SMDISC", r, 5000) && r.indexOf("OK") >= 0;
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
            String r;
            if (module->sendATCommand("AT+CGNSPWR?", r, 1500)) {
                int idx = r.indexOf("+CGNSPWR:");
                if (idx >= 0) {
                    // Expect format: +CGNSPWR: 0 or 1
                    int eol = r.indexOf('\n', idx);
                    String line = (eol > idx) ? r.substring(idx, eol) : r.substring(idx);
                    bool powered = line.indexOf(": 1") >= 0 || line.endsWith(":1");
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
        if (module->isNetworkConnected()) {
            module->getSignalStrength();
            backoffMs = 5000; // reset backoff on good state
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
