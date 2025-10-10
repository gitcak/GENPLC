#include "mqtt_task.h"
#include "mqtt_commands.h"
#include "../catm_gnss/catm_gnss_module.h"
#include "../catm_gnss/catm_gnss_task.h"
#include "../settings/settings_store.h"
#include <ArduinoJson.h>
#include <cstring>
#include "../logging/log_buffer.h"
#include "config/task_config.h"

extern CatMGNSSModule* catmGnssModule;
extern EventGroupHandle_t xEventGroupSystemStatus;

// External shared status flags
extern volatile bool g_mqttUp;

static AppSettings s;
namespace {
constexpr uint32_t kStatsPublishIntervalMs = 60000;   // 60 seconds
constexpr uint32_t kTelemetryMinIntervalMs = 15000;  // 15 seconds minimum between telemetry bursts
}

extern "C" void vTaskMQTT(void* pvParameters) {
    (void)pvParameters;
    settingsLoad(s);

    while (!xEventGroupSystemStatus) {
        Serial.println("MQTT task waiting for event group...");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    EventBits_t bits = xEventGroupWaitBits(
        xEventGroupSystemStatus,
        EVENT_BIT_CELLULAR_READY,
        pdFALSE,
        pdTRUE,
        portMAX_DELAY
    );
    (void)bits;

    if (strlen(s.apn)) {
        catmGnssModule->setApnCredentials(String(s.apn), String(s.apnUser), String(s.apnPass));
    }

    uint32_t lastConnTry = 0;
    bool mqttUp = false;
    bool mqttConfigured = false;
    bool mqttSubscribed = false;
    String deviceId = String((uint32_t)ESP.getEfuseMac(), HEX);
    const char* telemetryTopic = "v1/devices/me/telemetry";
    const char* attributesTopic = "v1/devices/me/attributes";
    const char* rpcRequestTopic = "v1/devices/me/rpc/request/+";
    const char* rpcResponsePrefix = "v1/devices/me/rpc/response/";
    
    uint32_t pendingRebootTime = 0;
    bool pendingReboot = false;
    bool pendingOTAReboot = false;
    uint32_t lastStatsPublish = 0;
    uint32_t lastTelemetryPublish = 0;
    bool attributesPublished = false;

    for (;;) {
        if (!xEventGroupSystemStatus) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        bits = xEventGroupWaitBits(
            xEventGroupSystemStatus,
            EVENT_BIT_MQTT_DATA_READY | EVENT_BIT_MQTT_PUBLISH_REQ | EVENT_BIT_MQTT_RECONNECT_REQ | EVENT_BIT_ERROR_DETECTED,
            pdTRUE,
            pdFALSE,
            pdMS_TO_TICKS(1000)
        );

        if (!catmGnssModule || !catmGnssModule->isModuleInitialized()) {
            if (mqttUp) {
                mqttUp = false;
                g_mqttUp = false;
                attributesPublished = false;
                lastTelemetryPublish = 0;
                lastStatsPublish = 0;
                mqttSubscribed = false;
                if (xEventGroupSystemStatus) {
                    xEventGroupSetBits(xEventGroupSystemStatus, EVENT_BIT_STATUS_CHANGE);
                }
            }
            mqttConfigured = false;
            xEventGroupWaitBits(xEventGroupSystemStatus, EVENT_BIT_CELLULAR_READY, pdFALSE, pdTRUE, portMAX_DELAY);
            continue;
        }

        if (!catmGnssModule->isNetworkConnected()) {
            if (mqttUp) {
                mqttUp = false;
                g_mqttUp = false;
                attributesPublished = false;
                lastTelemetryPublish = 0;
                lastStatsPublish = 0;
                mqttSubscribed = false;
                if (xEventGroupSystemStatus) {
                    xEventGroupSetBits(xEventGroupSystemStatus, EVENT_BIT_STATUS_CHANGE);
                }
            }
            mqttConfigured = false;
            xEventGroupWaitBits(xEventGroupSystemStatus, EVENT_BIT_CELLULAR_READY, pdFALSE, pdTRUE, pdMS_TO_TICKS(30000));
            continue;
        }

        if (strlen(s.mqttHost) && !mqttConfigured) {
            CatMCommand cfg{};
            cfg.type = CatMCommandType::ConfigureMQTT;
            strncpy(cfg.data.config.host, s.mqttHost, sizeof(cfg.data.config.host) - 1);
            cfg.data.config.port = s.mqttPort ? s.mqttPort : 1883;
            strncpy(cfg.data.config.user, s.mqttUser, sizeof(cfg.data.config.user) - 1);
            strncpy(cfg.data.config.pass, s.mqttPass, sizeof(cfg.data.config.pass) - 1);

            bool cfgResult = false;
            if (catmSubmitCommand(cfg, pdMS_TO_TICKS(200), pdMS_TO_TICKS(5000), cfgResult) && cfgResult) {
                mqttConfigured = true;
            } else {
                log_add("MQTT config failed - will retry");
                vTaskDelay(pdMS_TO_TICKS(1000));
                continue;
            }
        }

        if (!mqttUp && strlen(s.mqttHost) && mqttConfigured && (millis() - lastConnTry > 5000)) {
            lastConnTry = millis();
            CatMCommand connect{};
            connect.type = CatMCommandType::ConnectMQTT;
            bool connectResult = false;
            bool submitted = catmSubmitCommand(connect, pdMS_TO_TICKS(200), pdMS_TO_TICKS(15000), connectResult);
            bool wasConnected = mqttUp;
            mqttUp = submitted && connectResult;
            logbuf_printf("MQTT %s host=%s:%u", mqttUp ? "connected" : "connect fail", s.mqttHost,
                          (unsigned)(s.mqttPort ? s.mqttPort : 1883));
            if (wasConnected != mqttUp) {
                g_mqttUp = mqttUp;
                if (xEventGroupSystemStatus) {
                    xEventGroupSetBits(xEventGroupSystemStatus, EVENT_BIT_STATUS_CHANGE);
                }
            }
            if (!mqttUp) {
                if (!submitted) {
                    log_add("MQTT connect command queue full");
                }
                mqttConfigured = false;
                attributesPublished = false;
                lastTelemetryPublish = 0;
                lastStatsPublish = 0;
                mqttSubscribed = false;
            }
        }

        // Subscribe to RPC request topic after successful connection
        if (mqttUp && !mqttSubscribed) {
            CatMCommand subscribe{};
            subscribe.type = CatMCommandType::SubscribeMQTT;
            strncpy(subscribe.data.subscribe.topic, rpcRequestTopic, sizeof(subscribe.data.subscribe.topic) - 1);
            subscribe.data.subscribe.qos = 1;
            
            bool subscribeResult = false;
            if (catmSubmitCommand(subscribe, pdMS_TO_TICKS(200), pdMS_TO_TICKS(5000), subscribeResult) && subscribeResult) {
                mqttSubscribed = true;
                logbuf_printf("MQTT subscribed to %s", rpcRequestTopic);
            } else {
                log_add("MQTT subscribe failed - will retry");
            }
        }

        // Check for incoming commands
        if (mqttUp && mqttSubscribed) {
            String incomingTopic;
            String incomingPayload;
            
            // Check for incoming message (non-blocking, 100ms timeout)
            if (catmGnssModule && catmGnssModule->mqttCheckIncoming(incomingTopic, incomingPayload, 100)) {
                logbuf_printf("MQTT command received on %s", incomingTopic.c_str());
                int lastSlash = incomingTopic.lastIndexOf('/');
                String requestId = lastSlash >= 0 ? incomingTopic.substring(lastSlash + 1) : "";
                if (requestId.isEmpty()) {
                    log_add("MQTT RPC request missing ID");
                    continue;
                }
                
                // Parse and handle command
                MQTTCommand cmd;
                if (parseCommand(incomingPayload, cmd)) {
                    cmd.topic = incomingTopic;
                    cmd.id = requestId;

                    MQTTCommandResponse response;
                    response.commandId = cmd.id;
                    response.commandName = commandTypeToString(cmd.type);
                    
                    // Execute command based on type
                    bool handled = false;
                    switch (cmd.type) {
                        case MQTTCommandType::GET_GPS:
                            handled = handleGetGPS(cmd, response);
                            break;
                        case MQTTCommandType::GET_STATS:
                            handled = handleGetStats(cmd, response);
                            break;
                        case MQTTCommandType::GET_LOGS:
                            handled = handleGetLogs(cmd, response);
                            break;
                        case MQTTCommandType::CONFIG_UPDATE:
                            handled = handleConfigUpdate(cmd, response);
                            break;
                        case MQTTCommandType::CONFIG_MQTT:
                            handled = handleConfigMQTT(cmd, response);
                            break;
                        case MQTTCommandType::OTA_UPDATE:
                            handled = handleOTAUpdate(cmd, response);
                            if (handled && response.success) {
                                pendingOTAReboot = true;
                                pendingRebootTime = millis() + 3000; // Reboot in 3s
                            }
                            break;
                        case MQTTCommandType::REBOOT:
                            handled = handleReboot(cmd, response);
                            if (handled && response.success) {
                                uint32_t delayMs = cmd.params["delay_ms"] | 5000;
                                pendingReboot = true;
                                pendingRebootTime = millis() + delayMs;
                            }
                            break;
                        case MQTTCommandType::SET_INTERVAL:
                            handled = handleSetInterval(cmd, response);
                            break;
                        default:
                            response.success = false;
                            response.status = "error";
                            response.errorMessage = "Unknown command type";
                            handled = true;
                            break;
                    }
                    
                    // Send response
                    if (handled) {
                        String responseJSON = buildResponseJSON(response);
                        
                        CatMCommand publishResponse{};
                        publishResponse.type = CatMCommandType::PublishMQTT;
                        String responseTopic = String(rpcResponsePrefix) + requestId;
                        strncpy(publishResponse.data.publish.topic, responseTopic.c_str(),
                               sizeof(publishResponse.data.publish.topic) - 1);
                        publishResponse.data.publish.qos = 0;
                        publishResponse.data.publish.retain = false;
                        publishResponse.data.publish.payloadLen = responseJSON.length();
                        memcpy(publishResponse.data.publish.payload, responseJSON.c_str(),
                              min((size_t)responseJSON.length() + 1, sizeof(publishResponse.data.publish.payload)));
                        
                        bool publishResult = false;
                        if (!catmSubmitCommand(publishResponse, pdMS_TO_TICKS(200), pdMS_TO_TICKS(5000), publishResult)) {
                            log_add("Failed to send command response");
                        } else {
                            logbuf_printf("Command response sent: %s", response.status.c_str());
                        }
                    }
                } else {
                    log_add("Failed to parse command JSON");
                }
            }
        }

        if (mqttUp) {
            uint32_t nowMs = millis();
            bool moduleReady = (catmGnssModule && catmGnssModule->isModuleInitialized());
            CellularData cell{};
            GNSSData gps{};
            if (moduleReady) {
                cell = catmGnssModule->getCellularData();
                gps = catmGnssModule->getGNSSData();
            }

            auto publishJson = [&](const char* topic, const String& jsonPayload, bool retain, const char* failureLog) -> bool {
                CatMCommand publish{};
                publish.type = CatMCommandType::PublishMQTT;
                strncpy(publish.data.publish.topic, topic, sizeof(publish.data.publish.topic) - 1);
                publish.data.publish.qos = 0;
                publish.data.publish.retain = retain;

                size_t payloadLen = jsonPayload.length();
                if (payloadLen >= sizeof(publish.data.publish.payload)) {
                    logbuf_printf("MQTT payload too large for %s (%u bytes)", topic, (unsigned)payloadLen);
                    return false;
                }

                publish.data.publish.payloadLen = payloadLen;
                memcpy(publish.data.publish.payload, jsonPayload.c_str(), payloadLen);
                publish.data.publish.payload[payloadLen] = '\0';

                bool publishResult = false;
                if (!catmSubmitCommand(publish, pdMS_TO_TICKS(200), pdMS_TO_TICKS(5000), publishResult) || !publishResult) {
                    log_add(failureLog);
                    return false;
                }
                return true;
            };

            if (!attributesPublished) {
                StaticJsonDocument<192> attrDoc;
                attrDoc["device_id"] = deviceId;
                attrDoc["firmware_version"] = "1.0.0";
                attrDoc["chip_model"] = ESP.getChipModel();
                attrDoc["chip_revision"] = ESP.getChipRevision();
                attrDoc["sdk_version"] = ESP.getSdkVersion();

                String attrJson;
                serializeJson(attrDoc, attrJson);

                if (!publishJson(attributesTopic, attrJson, false, "Attributes publish failed - will reconnect")) {
                    mqttUp = false;
                    g_mqttUp = false;
                    mqttConfigured = false;
                    mqttSubscribed = false;
                    attributesPublished = false;
                    lastTelemetryPublish = 0;
                    lastStatsPublish = 0;
                    if (xEventGroupSystemStatus) {
                        xEventGroupSetBits(xEventGroupSystemStatus, EVENT_BIT_STATUS_CHANGE);
                    }
                } else {
                    attributesPublished = true;
                }
            }

            bool telemetryRequested = (bits & (EVENT_BIT_MQTT_DATA_READY | EVENT_BIT_MQTT_PUBLISH_REQ)) != 0;
            if (telemetryRequested && (lastTelemetryPublish == 0 || (nowMs - lastTelemetryPublish) >= kTelemetryMinIntervalMs)) {
                StaticJsonDocument<256> telemetryDoc;
                telemetryDoc["uptime_ms"] = nowMs;
                telemetryDoc["cellular_connected"] = moduleReady && cell.isConnected;
                telemetryDoc["signal_strength"] = moduleReady ? cell.signalStrength : 0;
                if (moduleReady) {
                    telemetryDoc["lat"] = gps.latitude;
                    telemetryDoc["lon"] = gps.longitude;
                    telemetryDoc["alt"] = gps.altitude;
                    telemetryDoc["speed"] = gps.speed;
                    telemetryDoc["course"] = gps.course;
                    telemetryDoc["gps_valid"] = gps.isValid;
                    telemetryDoc["gps_satellites"] = gps.satellites;
                } else {
                    telemetryDoc["gps_valid"] = false;
                    telemetryDoc["gps_satellites"] = 0;
                }

                String telemetryJson;
                serializeJson(telemetryDoc, telemetryJson);

                if (publishJson(telemetryTopic, telemetryJson, false, "Telemetry publish failed - will reconnect")) {
                    lastTelemetryPublish = nowMs;
                } else {
                    mqttUp = false;
                    g_mqttUp = false;
                    mqttConfigured = false;
                    mqttSubscribed = false;
                    attributesPublished = false;
                    lastTelemetryPublish = 0;
                    lastStatsPublish = 0;
                    if (xEventGroupSystemStatus) {
                        xEventGroupSetBits(xEventGroupSystemStatus, EVENT_BIT_STATUS_CHANGE);
                    }
                }
            }

            if ((nowMs - lastStatsPublish) >= kStatsPublishIntervalMs) {
                StaticJsonDocument<256> statsDoc;
                statsDoc["uptime_ms"] = nowMs;
                statsDoc["free_heap"] = ESP.getFreeHeap();
                statsDoc["min_free_heap"] = ESP.getMinFreeHeap();
                statsDoc["cellular_connected"] = moduleReady && cell.isConnected;
                statsDoc["signal_strength"] = moduleReady ? cell.signalStrength : 0;
                if (moduleReady) {
                    statsDoc["operator"] = cell.operatorName.c_str();
                    statsDoc["tx_bytes"] = static_cast<uint64_t>(cell.txBytes);
                    statsDoc["rx_bytes"] = static_cast<uint64_t>(cell.rxBytes);
                    statsDoc["gps_valid"] = gps.isValid;
                    statsDoc["gps_satellites"] = gps.satellites;
                    statsDoc["gps_last_update"] = gps.lastUpdate;
                } else {
                    statsDoc["gps_valid"] = false;
                    statsDoc["gps_satellites"] = 0;
                }

                String statsJson;
                serializeJson(statsDoc, statsJson);

                if (publishJson(telemetryTopic, statsJson, false, "Stats publish failed - will reconnect")) {
                    lastStatsPublish = nowMs;
                } else {
                    mqttUp = false;
                    g_mqttUp = false;
                    mqttConfigured = false;
                    mqttSubscribed = false;
                    attributesPublished = false;
                    lastTelemetryPublish = 0;
                    lastStatsPublish = 0;
                    if (xEventGroupSystemStatus) {
                        xEventGroupSetBits(xEventGroupSystemStatus, EVENT_BIT_STATUS_CHANGE);
                    }
                }
            }
        }

        if (bits & EVENT_BIT_MQTT_RECONNECT_REQ) {
            if (mqttUp) {
                CatMCommand disconnect{};
                disconnect.type = CatMCommandType::DisconnectMQTT;
                bool discard = false;
                catmSubmitCommand(disconnect, pdMS_TO_TICKS(200), pdMS_TO_TICKS(5000), discard);
            }
            mqttUp = false;
            g_mqttUp = false;
            mqttConfigured = false;
            attributesPublished = false;
            lastTelemetryPublish = 0;
            lastStatsPublish = 0;
            mqttSubscribed = false;
            if (xEventGroupSystemStatus) {
                xEventGroupSetBits(xEventGroupSystemStatus, EVENT_BIT_STATUS_CHANGE);
            }
            lastConnTry = 0;
        }

        if (bits & EVENT_BIT_ERROR_DETECTED) {
            if (mqttUp) {
                CatMCommand disconnect{};
                disconnect.type = CatMCommandType::DisconnectMQTT;
                bool discard = false;
                catmSubmitCommand(disconnect, pdMS_TO_TICKS(200), pdMS_TO_TICKS(5000), discard);
            }
            mqttUp = false;
            g_mqttUp = false;
            mqttConfigured = false;
            attributesPublished = false;
            lastTelemetryPublish = 0;
            lastStatsPublish = 0;
            if (xEventGroupSystemStatus) {
                xEventGroupSetBits(xEventGroupSystemStatus, EVENT_BIT_STATUS_CHANGE);
            }
            log_add("MQTT task detected error - will reconnect");
            mqttSubscribed = false;
        }

        // Handle pending reboots
        if (pendingReboot && millis() >= pendingRebootTime) {
            log_add("Executing scheduled reboot");
            vTaskDelay(pdMS_TO_TICKS(1000)); // Give time for logs
            ESP.restart();
        }
        
        if (pendingOTAReboot && millis() >= pendingRebootTime) {
            log_add("Executing OTA reboot");
            vTaskDelay(pdMS_TO_TICKS(1000)); // Give time for logs
            ESP.restart();
        }
    }
}



