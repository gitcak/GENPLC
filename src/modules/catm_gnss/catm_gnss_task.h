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

#endif // CATM_GNSS_TASK_H
