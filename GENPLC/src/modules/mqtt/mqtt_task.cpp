#include "mqtt_task.h"
#include "modules/catm_gnss/catm_gnss_module.h"
#include "modules/settings/settings_store.h"
#include <ArduinoJson.h>
#include "modules/logging/log_buffer.h"
#include "config/task_config.h"

extern CatMGNSSModule* catmGnssModule;
extern EventGroupHandle_t xEventGroupSystemStatus;

// External shared status flags
extern volatile bool g_mqttUp;

static AppSettings s;

extern "C" void vTaskMQTT(void* pvParameters) {
    (void)pvParameters;
    settingsLoad(s);
    
    // Wait for module initialization event (non-blocking)
    // Wait for event group to be available
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
    
    // Configure APN from settings if present
    if (strlen(s.apn)) {
        catmGnssModule->setApnCredentials(String(s.apn), String(s.apnUser), String(s.apnPass));
    }

    uint32_t lastPub = 0;
    uint32_t lastConnTry = 0;
    bool mqttUp = false;

    for (;;) {
        // Wait for data ready, publish request, or reconnect request
        if (!xEventGroupSystemStatus) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        bits = xEventGroupWaitBits(
            xEventGroupSystemStatus,
            EVENT_BIT_MQTT_DATA_READY | EVENT_BIT_MQTT_PUBLISH_REQ | EVENT_BIT_MQTT_RECONNECT_REQ | EVENT_BIT_ERROR_DETECTED,
            pdFALSE,
            pdFALSE,
            pdMS_TO_TICKS(1000)  // 1s timeout for periodic checks
        );
        
        // Handle module disconnect
        if (!catmGnssModule || !catmGnssModule->isModuleInitialized()) {
            if (mqttUp) {
                mqttUp = false;
                g_mqttUp = false;
                if (xEventGroupSystemStatus) {
                    xEventGroupSetBits(xEventGroupSystemStatus, EVENT_BIT_STATUS_CHANGE);
                }
            }
            // Wait for module to return
            if (!xEventGroupSystemStatus) {
                vTaskDelay(pdMS_TO_TICKS(1000));
                continue;
            }
            xEventGroupWaitBits(xEventGroupSystemStatus, EVENT_BIT_CELLULAR_READY, 
                pdFALSE, pdTRUE, portMAX_DELAY);
            continue;
        }
        
        // Handle network disconnect
        if (!catmGnssModule->isNetworkConnected()) {
            if (mqttUp) {
                mqttUp = false;
                g_mqttUp = false;
                if (xEventGroupSystemStatus) {
                    xEventGroupSetBits(xEventGroupSystemStatus, EVENT_BIT_STATUS_CHANGE);
                }
            }
            // Wait for network connection
            if (!xEventGroupSystemStatus) {
                vTaskDelay(pdMS_TO_TICKS(1000));
                continue;
            }
            xEventGroupWaitBits(xEventGroupSystemStatus, EVENT_BIT_CELLULAR_READY, 
                pdFALSE, pdTRUE, pdMS_TO_TICKS(30000));
            continue;
        }

        // Configure + connect MQTT if not up
        if (!mqttUp && millis() - lastConnTry > 5000 && strlen(s.mqttHost)) {
            lastConnTry = millis();
            catmGnssModule->mqttConfig(String(s.mqttHost), s.mqttPort ? s.mqttPort : 1883, String(s.mqttUser), String(s.mqttPass), "");
            bool wasConnected = mqttUp;
            mqttUp = catmGnssModule->mqttConnect(15000);
            logbuf_printf("MQTT %s host=%s:%u", mqttUp ? "connected" : "connect fail", s.mqttHost, (unsigned)(s.mqttPort ? s.mqttPort : 1883));
            
            // Update shared flag and notify UI if status changed
            if (wasConnected != mqttUp) {
                g_mqttUp = mqttUp;
                if (xEventGroupSystemStatus) {
                    xEventGroupSetBits(xEventGroupSystemStatus, EVENT_BIT_STATUS_CHANGE);
                }
            }
        }

        // Handle data publishing
        if (bits & (EVENT_BIT_MQTT_DATA_READY | EVENT_BIT_MQTT_PUBLISH_REQ)) {
            if (mqttUp) {
                GNSSData g = catmGnssModule->getGNSSData();
                StaticJsonDocument<256> d;
                d["t"] = millis();
                d["valid"] = g.isValid;
                d["lat"] = g.latitude; d["lon"] = g.longitude;
                d["alt"] = g.altitude; d["spd"] = g.speed; d["sat"] = g.satellites;
                String payload; serializeJson(d, payload);
                if (!catmGnssModule->mqttPublish("stamplc/gnss", payload, 0, false)) {
                    mqttUp = false; // force reconnect
                    g_mqttUp = false;
                    if (xEventGroupSystemStatus) {
                        xEventGroupSetBits(xEventGroupSystemStatus, EVENT_BIT_STATUS_CHANGE);
                    }
                    log_add("MQTT publish failed - will reconnect");
                }
            }
        }
        
        // Handle reconnect request
        if (bits & EVENT_BIT_MQTT_RECONNECT_REQ) {
            if (mqttUp) {
                mqttUp = false;
                g_mqttUp = false;
                if (xEventGroupSystemStatus) {
                    xEventGroupSetBits(xEventGroupSystemStatus, EVENT_BIT_STATUS_CHANGE);
                }
            }
            lastConnTry = 0; // Allow immediate reconnect attempt
        }
        
        // Handle error events
        if (bits & EVENT_BIT_ERROR_DETECTED) {
            if (mqttUp) {
                mqttUp = false;
                g_mqttUp = false;
                if (xEventGroupSystemStatus) {
                    xEventGroupSetBits(xEventGroupSystemStatus, EVENT_BIT_STATUS_CHANGE);
                }
            }
            log_add("MQTT task detected error - will reconnect");
        }
    }
}


