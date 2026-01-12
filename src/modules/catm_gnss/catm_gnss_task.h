#ifndef CATM_GNSS_TASK_H
#define CATM_GNSS_TASK_H

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <stdint.h>
#include <stddef.h>

// Task function declaration
void vTaskCatMGNSS(void* pvParameters);

// Function to trigger immediate GNSS update
void requestGNSSUpdate();

// -----------------------------------------------------------------------------
// CatM command queue API for serialized modem access
// -----------------------------------------------------------------------------

enum class CatMCommandType : uint8_t {
    ConfigureMQTT,
    ConnectMQTT,
    PublishMQTT,
    SubscribeMQTT,
    UnsubscribeMQTT,
    DisconnectMQTT
};

struct CatMCommand {
    CatMCommandType type;
    TaskHandle_t requester;
    union {
        struct {
            char host[64];
            uint16_t port;
            char user[32];
            char pass[32];
        } config;
        struct {
            char topic[96];
            uint8_t qos;
            bool retain;
            size_t payloadLen;
            char payload[256];
        } publish;
        struct {
            char topic[96];
            uint8_t qos;
        } subscribe;
        struct {
            char topic[96];
        } unsubscribe;
    } data;
};

extern QueueHandle_t g_catmCommandQueue;

bool catmSubmitCommand(const CatMCommand& command,
                       TickType_t queueWaitTicks,
                       TickType_t responseWaitTicks,
                       bool& resultOut);

#endif // CATM_GNSS_TASK_H
