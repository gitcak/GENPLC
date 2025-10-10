#include "mqtt_commands.h"
#include "../catm_gnss/catm_gnss_module.h"
#include "../storage/sd_card_module.h"
#include "../settings/settings_store.h"
#include "../logging/log_buffer.h"
#include <Update.h>
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <Preferences.h>

extern CatMGNSSModule* catmGnssModule;
extern SDCardModule* sdModule;

// Parse incoming command JSON
bool parseCommand(const String& payload, MQTTCommand& cmd) {
    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, payload);
    
    if (error) {
        Serial.printf("Failed to parse command JSON: %s\n", error.c_str());
        return false;
    }
    
    // Extract command type (support both legacy 'cmd' and ThingsBoard 'method')
    const char* cmdStr = doc["cmd"];
    if (!cmdStr) {
        cmdStr = doc["method"];
    }
    if (!cmdStr) {
        Serial.println("Command missing 'cmd'/'method' field");
        return false;
    }
    
    cmd.type = stringToCommandType(String(cmdStr));
    cmd.id = doc["id"] | "";
    cmd.receivedAt = millis();
    
    // Copy parameters if present
    if (doc.containsKey("params")) {
        cmd.params = doc["params"];
    }
    
    Serial.printf("Parsed command: type=%s, id=%s\n", 
                 cmdStr, cmd.id.c_str());
    
    return true;
}

// Convert string to command type
MQTTCommandType stringToCommandType(const String& cmdStr) {
    if (cmdStr == "get_gps") return MQTTCommandType::GET_GPS;
    if (cmdStr == "get_stats") return MQTTCommandType::GET_STATS;
    if (cmdStr == "get_logs") return MQTTCommandType::GET_LOGS;
    if (cmdStr == "config_update") return MQTTCommandType::CONFIG_UPDATE;
    if (cmdStr == "config_mqtt") return MQTTCommandType::CONFIG_MQTT;
    if (cmdStr == "ota_update") return MQTTCommandType::OTA_UPDATE;
    if (cmdStr == "reboot") return MQTTCommandType::REBOOT;
    if (cmdStr == "set_interval") return MQTTCommandType::SET_INTERVAL;
    return MQTTCommandType::UNKNOWN;
}

// Convert command type to string
String commandTypeToString(MQTTCommandType type) {
    switch (type) {
        case MQTTCommandType::GET_GPS: return "get_gps";
        case MQTTCommandType::GET_STATS: return "get_stats";
        case MQTTCommandType::GET_LOGS: return "get_logs";
        case MQTTCommandType::CONFIG_UPDATE: return "config_update";
        case MQTTCommandType::CONFIG_MQTT: return "config_mqtt";
        case MQTTCommandType::OTA_UPDATE: return "ota_update";
        case MQTTCommandType::REBOOT: return "reboot";
        case MQTTCommandType::SET_INTERVAL: return "set_interval";
        default: return "unknown";
    }
}

// Build JSON response
String buildResponseJSON(const MQTTCommandResponse& response) {
    StaticJsonDocument<512> doc;
    doc["method"] = response.commandName;
    doc["cmd"] = response.commandName;
    doc["id"] = response.commandId;
    doc["status"] = response.status;
    
    if (response.success) {
        doc["data"] = response.data;
    } else {
        doc["error"] = response.errorMessage;
    }
    
    String output;
    serializeJson(doc, output);
    return output;
}

// Handler: GET_GPS - Return current GPS position
bool handleGetGPS(const MQTTCommand& cmd, MQTTCommandResponse& response) {
    if (!catmGnssModule) {
        response.success = false;
        response.errorMessage = "GPS module not available";
        return false;
    }
    
    GNSSData gps = catmGnssModule->getGNSSData();
    
    response.data["timestamp"] = millis();
    response.data["valid"] = gps.isValid;
    response.data["latitude"] = gps.latitude;
    response.data["longitude"] = gps.longitude;
    response.data["altitude"] = gps.altitude;
    response.data["speed"] = gps.speed;
    response.data["course"] = gps.course;
    response.data["satellites"] = gps.satellites;
    response.data["hdop"] = gps.hdop;
    
    response.success = true;
    response.status = "success";
    
    Serial.printf("GET_GPS: lat=%.6f, lon=%.6f, valid=%d\n",
                 gps.latitude, gps.longitude, gps.isValid);
    
    return true;
}

// Handler: GET_STATS - Return device statistics
bool handleGetStats(const MQTTCommand& cmd, MQTTCommandResponse& response) {
    response.data["uptime_ms"] = millis();
    response.data["free_heap"] = ESP.getFreeHeap();
    response.data["min_free_heap"] = ESP.getMinFreeHeap();
    response.data["heap_size"] = ESP.getHeapSize();
    
    if (catmGnssModule && catmGnssModule->isModuleInitialized()) {
        CellularData cell = catmGnssModule->getCellularData();
        GNSSData gps = catmGnssModule->getGNSSData();
        
        response.data["cellular_connected"] = cell.isConnected;
        response.data["signal_strength"] = cell.signalStrength;
        response.data["operator"] = cell.operatorName.c_str();
        response.data["imei"] = cell.imei.c_str();
        response.data["ip_address"] = cell.ipAddress.c_str();
        response.data["tx_bytes"] = (unsigned long long)cell.txBytes;
        response.data["rx_bytes"] = (unsigned long long)cell.rxBytes;
        response.data["tx_bps"] = cell.txBps;
        response.data["rx_bps"] = cell.rxBps;
        
        response.data["gps_fix"] = gps.isValid;
        response.data["gps_satellites"] = gps.satellites;
        response.data["gps_last_update"] = gps.lastUpdate;
    }
    
    // Firmware version (you may want to define this elsewhere)
    response.data["firmware_version"] = "1.0.0";
    response.data["chip_model"] = ESP.getChipModel();
    response.data["chip_revision"] = ESP.getChipRevision();
    
    response.success = true;
    response.status = "success";
    
    Serial.println("GET_STATS: Device statistics collected");
    
    return true;
}

// Handler: GET_LOGS - Return recent logs
bool handleGetLogs(const MQTTCommand& cmd, MQTTCommandResponse& response) {
    int lines = cmd.params["lines"] | 50;
    const char* logType = cmd.params["type"] | "all";

    // Get logs from log buffer using correct API
    size_t totalLogs = log_count();
    int actualLines = min(lines, (int)totalLogs);

    if (actualLines == 0) {
        response.data["logs"] = "No logs available";
        response.data["count"] = 0;
        response.data["type"] = logType;
        response.success = true;
        response.status = "success";
        return true;
    }

    // Build logs string from most recent lines
    String logs;
    char lineBuffer[LOG_BUFFER_LINE_LEN];

    for (int i = actualLines - 1; i >= 0; i--) {
        if (log_get_line(i, lineBuffer, sizeof(lineBuffer))) {
            if (logs.length() > 0) {
                logs += "\n";
            }
            logs += lineBuffer;
        }
    }

    response.data["logs"] = logs;
    response.data["count"] = actualLines;
    response.data["type"] = logType;

    response.success = true;
    response.status = "success";

    Serial.printf("GET_LOGS: Returning %d lines\n", actualLines);

    return true;
}

// Handler: CONFIG_MQTT - Update MQTT broker configuration
bool handleConfigMQTT(const MQTTCommand& cmd, MQTTCommandResponse& response) {
    Preferences prefs;
    if (!prefs.begin("stamplc", false)) {
        response.success = false;
        response.errorMessage = "Failed to open preferences";
        response.status = "error";
        return false;
    }
    
    JsonArray updated = response.data.createNestedArray("updated");
    
    // Update MQTT host
    if (cmd.params.containsKey("host")) {
        String host = cmd.params["host"].as<String>();
        prefs.putString("mHost", host);
        updated.add("host");
        Serial.printf("CONFIG_MQTT: Host set to %s\n", host.c_str());
    }
    
    // Update MQTT port
    if (cmd.params.containsKey("port")) {
        uint16_t port = cmd.params["port"].as<uint16_t>();
        prefs.putUInt("mPort", port);
        updated.add("port");
        Serial.printf("CONFIG_MQTT: Port set to %u\n", port);
    }
    
    // Update MQTT user (ThingsBoard access token)
    if (cmd.params.containsKey("user")) {
        String user = cmd.params["user"].as<String>();
        prefs.putString("mUser", user);
        updated.add("user");
        Serial.printf("CONFIG_MQTT: User set to %s\n", user.c_str());
    }
    
    // Update MQTT password
    if (cmd.params.containsKey("pass")) {
        String pass = cmd.params["pass"].as<String>();
        prefs.putString("mPass", pass);
        updated.add("pass");
        Serial.println("CONFIG_MQTT: Password updated");
    }
    
    // Update APN
    if (cmd.params.containsKey("apn")) {
        String apn = cmd.params["apn"].as<String>();
        prefs.putString("apn", apn);
        updated.add("apn");
        Serial.printf("CONFIG_MQTT: APN set to %s\n", apn.c_str());
    }
    
    // Update APN user
    if (cmd.params.containsKey("apnU")) {
        String apnUser = cmd.params["apnU"].as<String>();
        prefs.putString("apnU", apnUser);
        updated.add("apnU");
        Serial.println("CONFIG_MQTT: APN user updated");
    }
    
    // Update APN password
    if (cmd.params.containsKey("apnP")) {
        String apnPass = cmd.params["apnP"].as<String>();
        prefs.putString("apnP", apnPass);
        updated.add("apnP");
        Serial.println("CONFIG_MQTT: APN password updated");
    }
    
    prefs.end();
    
    if (updated.size() == 0) {
        response.success = false;
        response.errorMessage = "No parameters provided";
        response.status = "error";
        return false;
    }
    
    response.data["message"] = "Configuration saved - reboot required to apply";
    response.data["count"] = updated.size();
    response.success = true;
    response.status = "success";
    
    Serial.printf("CONFIG_MQTT: Saved %d settings to NVS\n", updated.size());
    
    return true;
}

// Handler: CONFIG_UPDATE - Update device configuration
bool handleConfigUpdate(const MQTTCommand& cmd, MQTTCommandResponse& response) {
    AppSettings settings{};
    if (!settingsLoad(settings)) {
        response.success = false;
        response.errorMessage = "Failed to load current settings";
        return false;
    }
    
    JsonArray updated = response.data.createNestedArray("updated");
    bool needsRestart = false;
    
    // Update GPS interval
    if (cmd.params.containsKey("gps_interval")) {
        uint32_t interval = cmd.params["gps_interval"];
        // Store in settings or global variable
        updated.add("gps_interval");
        Serial.printf("Updated GPS interval: %u ms\n", interval);
    }
    
    // Update MQTT host
    if (cmd.params.containsKey("mqtt_host")) {
        const char* host = cmd.params["mqtt_host"];
        strncpy(settings.mqttHost, host, sizeof(settings.mqttHost) - 1);
        updated.add("mqtt_host");
        needsRestart = true;
    }
    
    // Update MQTT port
    if (cmd.params.containsKey("mqtt_port")) {
        settings.mqttPort = cmd.params["mqtt_port"];
        updated.add("mqtt_port");
        needsRestart = true;
    }
    
    // Update APN
    if (cmd.params.containsKey("apn")) {
        const char* apn = cmd.params["apn"];
        strncpy(settings.apn, apn, sizeof(settings.apn) - 1);
        updated.add("apn");
        needsRestart = true;
    }
    
    // Save settings
    if (updated.size() > 0) {
        if (!settingsSave(settings)) {
            response.success = false;
            response.errorMessage = "Failed to save settings";
            return false;
        }
    }
    
    response.data["restart_required"] = needsRestart;
    response.success = true;
    response.status = "success";
    
    Serial.printf("CONFIG_UPDATE: Updated %d settings\n", updated.size());
    
    return true;
}

// Handler: OTA_UPDATE - Perform firmware update
bool handleOTAUpdate(const MQTTCommand& cmd, MQTTCommandResponse& response) {
    const char* url = cmd.params["url"];
    const char* version = cmd.params["version"] | "unknown";
    const char* md5 = cmd.params["md5"] | "";
    
    if (!url || strlen(url) == 0) {
        response.success = false;
        response.errorMessage = "Missing firmware URL";
        return false;
    }
    
    Serial.printf("OTA_UPDATE: Starting update from %s\n", url);
    
    // Disable GPS during OTA
    if (catmGnssModule) {
        catmGnssModule->disableGNSS();
    }
    
    HTTPClient http;
    http.begin(url);
    
    int httpCode = http.GET();
    if (httpCode != HTTP_CODE_OK) {
        response.success = false;
        response.errorMessage = "Failed to download firmware: HTTP " + String(httpCode);
        http.end();
        
        // Re-enable GPS
        if (catmGnssModule) {
            catmGnssModule->enableGNSS();
        }
        return false;
    }
    
    int contentLength = http.getSize();
    if (contentLength <= 0) {
        response.success = false;
        response.errorMessage = "Invalid firmware size";
        http.end();
        
        if (catmGnssModule) {
            catmGnssModule->enableGNSS();
        }
        return false;
    }
    
    // Begin OTA update
    if (!Update.begin(contentLength)) {
        response.success = false;
        response.errorMessage = "OTA begin failed: " + String(Update.errorString());
        http.end();
        
        if (catmGnssModule) {
            catmGnssModule->enableGNSS();
        }
        return false;
    }
    
    // Set MD5 if provided
    if (strlen(md5) > 0) {
        Update.setMD5(md5);
    }
    
    // Download and flash firmware
    WiFiClient* stream = http.getStreamPtr();
    size_t written = Update.writeStream(*stream);
    
    if (written != contentLength) {
        response.success = false;
        response.errorMessage = "OTA write incomplete";
        Update.abort();
        http.end();
        
        if (catmGnssModule) {
            catmGnssModule->enableGNSS();
        }
        return false;
    }
    
    if (!Update.end()) {
        response.success = false;
        response.errorMessage = "OTA end failed: " + String(Update.errorString());
        http.end();
        
        if (catmGnssModule) {
            catmGnssModule->enableGNSS();
        }
        return false;
    }
    
    http.end();
    
    // Verify update
    if (!Update.isFinished()) {
        response.success = false;
        response.errorMessage = "OTA update not finished";
        
        if (catmGnssModule) {
            catmGnssModule->enableGNSS();
        }
        return false;
    }
    
    response.data["version"] = version;
    response.data["size"] = contentLength;
    response.data["message"] = "Firmware updated successfully, rebooting...";
    response.success = true;
    response.status = "success";
    
    Serial.println("OTA_UPDATE: Success! Rebooting in 3 seconds...");
    
    // Note: Device will reboot after response is sent
    
    return true;
}

// Handler: REBOOT - Restart device
bool handleReboot(const MQTTCommand& cmd, MQTTCommandResponse& response) {
    uint32_t delayMs = cmd.params["delay_ms"] | 5000;
    
    response.data["message"] = "Device will reboot in " + String(delayMs) + " ms";
    response.data["delay_ms"] = delayMs;
    response.success = true;
    response.status = "success";
    
    Serial.printf("REBOOT: Scheduled reboot in %u ms\n", delayMs);
    
    // Note: Reboot will happen after response is sent
    
    return true;
}

// Handler: SET_INTERVAL - Change reporting intervals
bool handleSetInterval(const MQTTCommand& cmd, MQTTCommandResponse& response) {
    const char* intervalType = cmd.params["type"];
    uint32_t valueMs = cmd.params["value_ms"];
    
    if (!intervalType) {
        response.success = false;
        response.errorMessage = "Missing interval type";
        return false;
    }
    
    // Store previous value (example - you may need global variables)
    uint32_t previousValue = 30000; // Default
    
    // Update the interval based on type
    if (strcmp(intervalType, "gps_publish") == 0) {
        // Update GPS publish interval
        // You'll need to implement this based on your architecture
        Serial.printf("SET_INTERVAL: GPS publish interval = %u ms\n", valueMs);
    } else if (strcmp(intervalType, "stats_publish") == 0) {
        // Update stats publish interval
        Serial.printf("SET_INTERVAL: Stats publish interval = %u ms\n", valueMs);
    } else {
        response.success = false;
        response.errorMessage = "Unknown interval type: " + String(intervalType);
        return false;
    }
    
    response.data["type"] = intervalType;
    response.data["previous_value"] = previousValue;
    response.data["new_value"] = valueMs;
    response.success = true;
    response.status = "success";
    
    return true;
}
