/*
 * FreeRTOS Task Configuration Header
 * StampPLC CatM+GNSS FreeRTOS Modularization
 * 
 * Contains all FreeRTOS task priorities, stack sizes, and timing parameters
 */

#ifndef TASK_CONFIG_H
#define TASK_CONFIG_H

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/event_groups.h>
#include "system_config.h"

// ============================================================================
// TASK PRIORITIES (Higher number = Higher priority)
// ============================================================================
#define TASK_PRIORITY_SYSTEM_MONITOR    3
#define TASK_PRIORITY_INDUSTRIAL_IO     4
#define TASK_PRIORITY_SENSOR            4
#define TASK_PRIORITY_GNSS              3
#define TASK_PRIORITY_CELLULAR          3
#define TASK_PRIORITY_DISPLAY           2
#define TASK_PRIORITY_DATA_TRANSMIT     2
#define TASK_PRIORITY_BUTTON_HANDLER    1

// ============================================================================
// TASK STACK SIZES (in words, 4 bytes per word on ESP32)
// ============================================================================
#define TASK_STACK_SIZE_SYSTEM_MONITOR  8192  // 32KB - prevent StatusBar stack overflow
#define TASK_STACK_SIZE_INDUSTRIAL_IO   2048  // 8KB - increased to prevent stack overflow
#define TASK_STACK_SIZE_SENSOR          2048  // 8KB - increased to prevent stack overflow
#define TASK_STACK_SIZE_GNSS            1024  // 4KB
#define TASK_STACK_SIZE_CELLULAR        1536  // 6KB
#define TASK_STACK_SIZE_DISPLAY         768   // 3KB
#define TASK_STACK_SIZE_DATA_TRANSMIT   1024  // 4KB
#define TASK_STACK_SIZE_BUTTON_HANDLER  4096  // 16KB (increased from 8KB)

// Application-specific task sizes (words) tuned from stack watermarks
#define TASK_STACK_SIZE_APP_DISPLAY     4096  // 16KB (M5GFX rendering)
#define TASK_STACK_SIZE_APP_GNSS        4864  // 19KB (AT I/O, JSON, parsing, GNSS gating)
#define TASK_STACK_SIZE_APP_MQTT        6144  // 24KB (JSON build + AT MQTT bursts + command handlers)

// ============================================================================
// TASK DELAYS AND TIMING (in FreeRTOS ticks)
// ============================================================================
#define TASK_DELAY_MS(x)                pdMS_TO_TICKS(x)
#define TASK_DELAY_100MS                pdMS_TO_TICKS(100)
#define TASK_DELAY_500MS                pdMS_TO_TICKS(500)
#define TASK_DELAY_1S                   pdMS_TO_TICKS(1000)
#define TASK_DELAY_5S                   pdMS_TO_TICKS(5000)
#define TASK_DELAY_10S                  pdMS_TO_TICKS(10000)
#define TASK_DELAY_30S                  pdMS_TO_TICKS(30000)

// ============================================================================
// QUEUE SIZES AND TIMEOUTS
// ============================================================================
#define QUEUE_SIZE_SENSOR_DATA          10
#define QUEUE_SIZE_GNSS_DATA            5
#define QUEUE_SIZE_CELLULAR_CMD         5
#define QUEUE_SIZE_DISPLAY_CMD          10
#define QUEUE_SIZE_ERROR_LOG            20
#define QUEUE_SIZE_BUTTON_EVENT         5

#define QUEUE_TIMEOUT_MS                100
#define QUEUE_TIMEOUT_TICKS             pdMS_TO_TICKS(QUEUE_TIMEOUT_MS)

// ============================================================================
// SEMAPHORE TIMEOUTS
// ============================================================================
#define SEMAPHORE_TIMEOUT_MS            1000
#define SEMAPHORE_TIMEOUT_TICKS         pdMS_TO_TICKS(SEMAPHORE_TIMEOUT_MS)

// ============================================================================
// EVENT GROUP BITS
// ============================================================================
#define EVENT_BIT_SYSTEM_READY          (1UL << 0)
#define EVENT_BIT_CELLULAR_READY        (1UL << 1)
#define EVENT_BIT_GNSS_READY            (1UL << 2)
#define EVENT_BIT_SENSORS_READY         (1UL << 3)
#define EVENT_BIT_DISPLAY_READY         (1UL << 4)
#define EVENT_BIT_ERROR_DETECTED        (1UL << 5)
#define EVENT_BIT_MAINTENANCE_MODE      (1UL << 6)
#define EVENT_BIT_SLEEP_MODE            (1UL << 7)

// Event-driven task coordination bits
#define EVENT_BIT_MQTT_DATA_READY       (1UL << 8)
#define EVENT_BIT_MQTT_PUBLISH_REQ      (1UL << 9)
#define EVENT_BIT_MQTT_RECONNECT_REQ    (1UL << 10)
#define EVENT_BIT_GNSS_UPDATE_REQ       (1UL << 11)
#define EVENT_BIT_GNSS_DATA_READY       (1UL << 12)
#define EVENT_BIT_SYSTEM_ERROR          (1UL << 13)
#define EVENT_BIT_STATUS_CHANGE         (1UL << 14)
#define EVENT_BIT_HEAP_LOW              (1UL << 15)

// ============================================================================
// TASK HANDLES
// ============================================================================
extern TaskHandle_t xTaskSystemMonitor;
extern TaskHandle_t xTaskIndustrialIO;
extern TaskHandle_t xTaskSensor;
extern TaskHandle_t xTaskGNSS;
extern TaskHandle_t xTaskCellular;
extern TaskHandle_t xTaskDisplay;
extern TaskHandle_t xTaskDataTransmit;
extern TaskHandle_t xTaskButtonHandler;

// ============================================================================
// QUEUE HANDLES
// ============================================================================
extern QueueHandle_t xQueueSensorData;
extern QueueHandle_t xQueueGNSSData;
extern QueueHandle_t xQueueCellularCmd;
extern QueueHandle_t xQueueDisplayCmd;
extern QueueHandle_t xQueueErrorLog;
extern QueueHandle_t xQueueButtonEvent;

// ============================================================================
// SEMAPHORE HANDLES
// ============================================================================
extern SemaphoreHandle_t xSemaphoreI2C;
extern SemaphoreHandle_t xSemaphoreSPI;
extern SemaphoreHandle_t xSemaphoreUART;
extern SemaphoreHandle_t xSemaphoreDisplay;

// ============================================================================
// EVENT GROUP HANDLES
// ============================================================================
extern EventGroupHandle_t xEventGroupSystemStatus;

// ============================================================================
// TASK CREATION MACROS
// ============================================================================
#define CREATE_TASK(taskFunction, taskName, stackSize, taskParams, priority, taskHandle) \
    xTaskCreate(taskFunction, taskName, stackSize, taskParams, priority, taskHandle)

#define CREATE_TASK_PINNED(taskFunction, taskName, stackSize, taskParams, priority, taskHandle, coreID) \
    xTaskCreatePinnedToCore(taskFunction, taskName, stackSize, taskParams, priority, taskHandle, coreID)

// ============================================================================
// TASK DELETION MACROS
// ============================================================================
#define DELETE_TASK(taskHandle) \
    if (taskHandle != NULL) { \
        vTaskDelete(taskHandle); \
        taskHandle = NULL; \
    }

// ============================================================================
// QUEUE OPERATION MACROS
// ============================================================================
#define SEND_TO_QUEUE(queue, data, timeout) \
    xQueueSend(queue, &data, timeout)

#define RECEIVE_FROM_QUEUE(queue, data, timeout) \
    xQueueReceive(queue, &data, timeout)

#define PEEK_QUEUE(queue, data, timeout) \
    xQueuePeek(queue, &data, timeout)

// ============================================================================
// SEMAPHORE OPERATION MACROS
// ============================================================================
#define TAKE_SEMAPHORE(semaphore, timeout) \
    xSemaphoreTake(semaphore, timeout)

#define GIVE_SEMAPHORE(semaphore) \
    xSemaphoreGive(semaphore)

// ============================================================================
// EVENT GROUP OPERATION MACROS
// ============================================================================
#define SET_EVENT_BIT(eventGroup, bit) \
    xEventGroupSetBits(eventGroup, bit)

#define CLEAR_EVENT_BIT(eventGroup, bit) \
    xEventGroupClearBits(eventGroup, bit)

#define WAIT_FOR_EVENT_BIT(eventGroup, bit, timeout) \
    xEventGroupWaitBits(eventGroup, bit, pdFALSE, pdFALSE, timeout)

// ============================================================================
// TASK MONITORING MACROS
// ============================================================================
#define GET_TASK_STACK_HIGH_WATER_MARK(taskHandle) \
    uxTaskGetStackHighWaterMark(taskHandle)

#define GET_TASK_RUNTIME_STATS() \
    vTaskGetRunTimeStats()

#define GET_TASK_LIST() \
    vTaskList()

#endif // TASK_CONFIG_H
