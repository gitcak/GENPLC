#ifndef STORAGE_TASK_H
#define STORAGE_TASK_H

#include <Arduino.h>

struct LogRecord {
    enum Type : uint8_t { GNSS = 0, CELL = 1 } type;
    char line[256];
};

extern QueueHandle_t g_storageQ;
extern SemaphoreHandle_t g_sdMutex;

extern "C" void vTaskStorage(void* pvParameters);

#endif // STORAGE_TASK_H


