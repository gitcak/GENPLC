#include "catm_gnss_module.h"
#include "modules/storage/storage_task.h"
#include <ArduinoJson.h>
#include "config/task_config.h"
#include "catm_gnss_task.h"
#include "../../include/debug_system.h"
#include "modules/settings/settings_store.h"
#include "modules/logging/log_buffer.h"

extern EventGroupHandle_t xEventGroupSystemStatus;
extern QueueHandle_t g_storageQ;
extern volatile bool g_cellularUp;

// Function to trigger immediate GNSS update
void requestGNSSUpdate() {
    if (xEventGroupSystemStatus) {
        xEventGroupSetBits(xEventGroupSystemStatus, EVENT_BIT_GNSS_UPDATE_REQ);
    }
}

// FreeRTOS task function for CatM+GNSS module
void vTaskCatMGNSS(void* pvParameters) {

    CatMGNSSModule* module = (CatMGNSSModule*)pvParameters;



    if (!module) {

        LOG_ERROR("Invalid module pointer");

        vTaskDelete(NULL);

        return;

    }



    LOG_INFO("CATM_GNSS_TASK starting...");



    bool gnssEnabled = false;

    uint32_t lastGnssPausedLog = 0;



    if (module->disableGNSS()) {

        Serial.println("[CATM_GNSS_TASK] GNSS powered down to prioritize cellular attach");

    }



    // Load settings for APN credentials (refresh periodically to pick up changes)

    AppSettings settings{};

    bool haveSettings = settingsLoad(settings);

    if (haveSettings) {

        module->setApnCredentials(String(settings.apn), String(settings.apnUser), String(settings.apnPass));

        log_add("BOOT: CatM APN loaded");

    }



    uint32_t lastConnectAttempt = 0;

    uint32_t lastSettingsRefresh = millis();



    for (;;) {

        if (!xEventGroupSystemStatus) {

            vTaskDelay(pdMS_TO_TICKS(1000));

            continue;

        }

        EventBits_t bits = xEventGroupWaitBits(

            xEventGroupSystemStatus,

            EVENT_BIT_GNSS_UPDATE_REQ | EVENT_BIT_ERROR_DETECTED,

            pdFALSE,

            pdFALSE,

            pdMS_TO_TICKS(10000)

        );



        if (bits & EVENT_BIT_ERROR_DETECTED) {

            Serial.println("[CATM_GNSS_TASK] Error detected - waiting for recovery");

            if (!xEventGroupSystemStatus) {

                vTaskDelay(pdMS_TO_TICKS(1000));

                continue;

            }

            xEventGroupWaitBits(xEventGroupSystemStatus, EVENT_BIT_CELLULAR_READY, pdFALSE, pdTRUE, portMAX_DELAY);

            continue;

        }



        const uint32_t now = millis();



        if (now - lastSettingsRefresh > 60000) {

            lastSettingsRefresh = now;

            if (settingsLoad(settings)) {

                haveSettings = true;

                module->setApnCredentials(String(settings.apn), String(settings.apnUser), String(settings.apnPass));

            }

        }



        bool wasConnected = g_cellularUp;

        bool isConnected = module->isNetworkConnected();



        if (!isConnected) {

            if (gnssEnabled) {

                Serial.println("[CATM_GNSS_TASK] Disabling GNSS before network attach");

                if (module->disableGNSS()) {

                    gnssEnabled = false;

                    lastGnssPausedLog = now;

                }

            }



            if (now - lastConnectAttempt > 15000) {

                lastConnectAttempt = now;

                if (haveSettings && settings.apn[0] != '\0') {

                    Serial.println("[CATM_GNSS_TASK] Attempting network attach with stored APN");

                    if (g_storageQ) {

                        LogRecord rec{};

                        rec.type = LogRecord::CELL;

                        snprintf(rec.line, sizeof(rec.line), "Attempting attach APN=%s", settings.apn);

                        xQueueSend(g_storageQ, &rec, 0);

                    }

                    bool attached = module->connectNetwork(String(settings.apn), String(settings.apnUser), String(settings.apnPass));

                    isConnected = attached;

                    if (!attached) {

                        LOG_WARN("APN attach failed: %s", settings.apn);

                        log_add("ERROR: CatM APN attach failed");

                    } else {

                        if (xEventGroupSystemStatus) {

                            xEventGroupSetBits(xEventGroupSystemStatus, EVENT_BIT_CELLULAR_READY | EVENT_BIT_STATUS_CHANGE);

                        }

                        log_add("BOOT: CatM network attached");

                        Serial.println("[CATM_GNSS_TASK] Cellular attach succeeded");

                        if (!gnssEnabled) {

                            if (module->enableGNSS()) {

                                gnssEnabled = true;

                                Serial.println("[CATM_GNSS_TASK] GNSS resumed after attach");

                            } else {

                                Serial.println("[CATM_GNSS_TASK] Failed to re-enable GNSS after attach");

                            }

                        }

                    }

                } else {

                    Serial.println("[CATM_GNSS_TASK] APN not configured; skipping network attach attempt");

                }

            }

        }



        if (isConnected && !gnssEnabled) {

            Serial.println("[CATM_GNSS_TASK] Enabling GNSS after confirming cellular link");

            if (module->enableGNSS()) {

                gnssEnabled = true;

                Serial.println("[CATM_GNSS_TASK] GNSS enabled successfully");

            } else {

                Serial.println("[CATM_GNSS_TASK] Failed to enable GNSS");

            }

        }



        if (isConnected && xEventGroupSystemStatus) {

            xEventGroupSetBits(xEventGroupSystemStatus, EVENT_BIT_CELLULAR_READY);

        }



        if (gnssEnabled) {

            if (module->updateGNSSData()) {

                GNSSData data = module->getGNSSData();

                Serial.printf("[CATM_GNSS_TASK] GNSS: Valid fix, Satellites: %d\n", data.satellites);



                if (xEventGroupSystemStatus) {

                    xEventGroupSetBits(xEventGroupSystemStatus, EVENT_BIT_MQTT_DATA_READY);

                }



                if (g_storageQ) {

                    StaticJsonDocument<256> doc;

                    doc["t"] = millis();

                    doc["lat"] = data.latitude;

                    doc["lon"] = data.longitude;

                    doc["alt"] = data.altitude;

                    doc["spd"] = data.speed;

                    doc["sat"] = data.satellites;

                    doc["valid"] = data.isValid;

                    char buf[256];

                    size_t n = serializeJson(doc, buf, sizeof(buf));

                    LogRecord rec{};

                    rec.type = LogRecord::GNSS;

                    memcpy(rec.line, buf, min(n + 1, sizeof(rec.line)));

                    xQueueSend(g_storageQ, &rec, 0);

                }

            } else {

                Serial.println("[CATM_GNSS_TASK] GNSS: No valid fix");

            }

        } else if (now - lastGnssPausedLog > 5000) {

            Serial.println("[CATM_GNSS_TASK] GNSS paused while cellular attach in progress");

            lastGnssPausedLog = now;

        }



        if (!isConnected) {

            Serial.println("[CATM_GNSS_TASK] Cellular: Not connected");



            if (g_storageQ) {

                StaticJsonDocument<96> doc;

                doc["t"] = millis();

                doc["conn"] = false;

                char buf[96];

                size_t n = serializeJson(doc, buf, sizeof(buf));

                LogRecord rec{};

                rec.type = LogRecord::CELL;

                memcpy(rec.line, buf, min(n + 1, sizeof(rec.line)));

                xQueueSend(g_storageQ, &rec, 0);

            }

        } else {

            int8_t signal = module->getSignalStrength();

            String operator_name = module->getOperatorName();

            Serial.printf("[CATM_GNSS_TASK] Cellular: Connected to %s, Signal: %d dBm\n",

                         operator_name.c_str(), signal);



            if (g_storageQ) {

                StaticJsonDocument<192> doc;

                doc["t"] = millis();

                doc["op"] = operator_name;

                doc["rssi"] = signal;

                doc["conn"] = true;

                char buf[192];

                size_t n = serializeJson(doc, buf, sizeof(buf));

                LogRecord rec{};

                rec.type = LogRecord::CELL;

                memcpy(rec.line, buf, min(n + 1, sizeof(rec.line)));

                xQueueSend(g_storageQ, &rec, 0);

            }

        }



        if (wasConnected != isConnected) {

            g_cellularUp = isConnected;

            if (!isConnected) {
                CellularData diag = module->getCellularData();
                if (diag.lastDetachReason.length()) {
                    LOG_WARN("Cellular detached: %s", diag.lastDetachReason.c_str());
                }
            }

            if (xEventGroupSystemStatus) {

                xEventGroupSetBits(xEventGroupSystemStatus, EVENT_BIT_STATUS_CHANGE);

            }

        }

    }

}






