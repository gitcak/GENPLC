/*
 * FreeRTOS Configuration Header
 * StampPLC CatM+GNSS FreeRTOS Modularization
 * 
 * ESP32-S3 optimized FreeRTOS configuration
 */

#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

// ============================================================================
// ESP32-S3 SPECIFIC CONFIGURATION
// ============================================================================
#define configUSE_ESP_IDF_HOOKS                    1
#define configUSE_ESP_IDF_TICK_HOOK               1
#define configUSE_ESP_IDF_MAIN_WRAPPER            1

// ============================================================================
// BASIC FREE RTOS CONFIGURATION
// ============================================================================
#define configUSE_PREEMPTION                       1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION    1
#define configUSE_TICKLESS_IDLE                    0
#define configUSE_TICKLESS_IDLE_SIMPLE_DEBUG       0
#define configUSE_TICKLESS_IDLE_SIMPLE_DEBUG_2     0

// ============================================================================
// SOFTWARE TIMER CONFIGURATION
// ============================================================================
#define configUSE_TIMERS                           1
#define configTIMER_TASK_PRIORITY                 (configMAX_PRIORITIES - 1)
#define configTIMER_QUEUE_LENGTH                  10
#define configTIMER_TASK_STACK_DEPTH              2048

// ============================================================================
// MEMORY ALLOCATION CONFIGURATION
// ============================================================================
#define configSUPPORT_STATIC_ALLOCATION           1
#define configSUPPORT_DYNAMIC_ALLOCATION          1
#define configTOTAL_HEAP_SIZE                     (131072)  // 128KB for ESP32-S3
#define configAPPLICATION_ALLOCATED_HEAP          0
#define configUSE_MALLOC_FAILED_HOOK              1

// ============================================================================
// TASK CONFIGURATION
// ============================================================================
#define configMAX_PRIORITIES                      32
#define configMAX_TASK_NAME_LEN                   16
#define configMINIMAL_STACK_SIZE                  128
#define configUSE_TRACE_FACILITY                  1
#define configUSE_STATS_FORMATTING_FUNCTIONS      1
#define configGENERATE_RUN_TIME_STATS            0

// ============================================================================
// STACK OVERFLOW DETECTION
// ============================================================================
#define configCHECK_FOR_STACK_OVERFLOW           2
#define configCHECK_FOR_STACK_OVERFLOW_HOOK      1

// ============================================================================
// IDLE CONFIGURATION
// ============================================================================
#define configUSE_IDLE_HOOK                       1
#define configIDLE_SHOULD_YIELD                   1
#define configUSE_MINIMAL_IDLE_HOOK               0

// ============================================================================
// TICK CONFIGURATION
// ============================================================================
#define configTICK_RATE_HZ                        1000
#define configUSE_TIME_SLICING                    1
#define configUSE_16_BIT_TICKS                    0
#define configUSE_TICK_HOOK                       0

// ============================================================================
// CO-ROUTINE CONFIGURATION
// ============================================================================
#define configUSE_CO_ROUTINES                     0
#define configMAX_CO_ROUTINE_PRIORITIES           2

// ============================================================================
// QUEUE CONFIGURATION
// ============================================================================
#define configUSE_QUEUE_SETS                      1
#define configUSE_COUNTING_SEMAPHORES             1
#define configUSE_MUTEXES                         1
#define configUSE_RECURSIVE_MUTEXES               1
#define configUSE_APPLICATION_TASK_TAG            1

// ============================================================================
// EVENT GROUP CONFIGURATION
// ============================================================================
#define configUSE_EVENT_GROUPS                    1
#define configUSE_TASK_NOTIFICATIONS              1
#define configUSE_TASK_NOTIFICATIONS_AS_QUEUE     1

// ============================================================================
// STREAM BUFFER CONFIGURATION
// ============================================================================
#define configUSE_STREAM_BUFFERS                  1
#define configUSE_MESSAGE_BUFFERS                 1

// ============================================================================
// DEBUGGING AND ASSERTIONS
// ============================================================================
#define configASSERT(x)                           if((x) == 0) { taskDISABLE_INTERRUPTS(); for(;;); }
#define configUSE_NEWLIB_REENTRANT                0
#define configENABLE_BACKWARD_COMPATIBILITY       0
#define configNUM_THREAD_LOCAL_STORAGE_POINTERS   0

// ============================================================================
// ESP32-S3 DUAL CORE OPTIMIZATION
// ============================================================================
#define configUSE_DUAL_CORE                       1
#define configUSE_CORE_0                          1
#define configUSE_CORE_1                          1
#define configCORE_0_TASK_PRIORITY_MIN           1
#define configCORE_1_TASK_PRIORITY_MIN           1
#define portNUM_PROCESSORS                        2

// ============================================================================
// ESP32 ARDUINO CORE COMPATIBILITY
// ============================================================================
#define XT_BOARD                                  1
#define XT_CLOCK_FREQ                             240000000
#define portTICK_RATE_MS                          portTICK_PERIOD_MS

// ============================================================================
// PERFORMANCE OPTIMIZATION
// ============================================================================
// (Duplicates removed - already defined in BASIC FREE RTOS CONFIGURATION)

// ============================================================================
// MEMORY PROTECTION
// ============================================================================
// (Duplicates removed - already defined in MEMORY ALLOCATION CONFIGURATION)

// ============================================================================
// TASK NOTIFICATIONS
// ============================================================================
// (Duplicates removed - already defined in EVENT GROUP CONFIGURATION)

// ============================================================================
// STACK OVERFLOW PROTECTION
// ============================================================================
// (Duplicates removed - already defined in STACK OVERFLOW DETECTION)

// ============================================================================
// WATCHDOG CONFIGURATION
// ============================================================================
#define configUSE_WATCHDOG_TIMER                  1
#define configWATCHDOG_TIMEOUT_MS                 30000

// ============================================================================
// POWER MANAGEMENT
// ============================================================================
// (Duplicates removed - already defined in BASIC FREE RTOS CONFIGURATION)

// ============================================================================
// INCLUDE DEFINITIONS
// ============================================================================
#define INCLUDE_vTaskPrioritySet                  1
#define INCLUDE_uxTaskPriorityGet                 1
#define INCLUDE_vTaskDelete                       1
#define INCLUDE_vTaskSuspend                      1
#define INCLUDE_xResumeFromISR                    1
#define INCLUDE_vTaskDelayUntil                   1
#define INCLUDE_vTaskDelay                        1
#define INCLUDE_xTaskGetSchedulerState            1
#define INCLUDE_xTaskGetCurrentTaskHandle         1
#define INCLUDE_uxTaskGetStackHighWaterMark       1
#define INCLUDE_xTaskGetIdleTaskHandle            1
#define INCLUDE_eTaskGetState                     1
#define INCLUDE_xEventGroupSetBitFromISR          1
#define INCLUDE_xTimerPendFunctionCall            1
#define INCLUDE_xTaskAbortDelay                   1
#define INCLUDE_xTaskGetHandle                    1
#define INCLUDE_xTaskResumeFromISR                1

// ============================================================================
// ESP32 ARDUINO COMPATIBILITY
// ============================================================================
// (Duplicates removed - already defined in ESP32-S3 SPECIFIC CONFIGURATION and MEMORY ALLOCATION CONFIGURATION)

// ============================================================================
// LEGACY HANDLE TYPE COMPATIBILITY
// ============================================================================
#ifndef xQueueHandle
#define xQueueHandle QueueHandle_t
#endif

#ifndef xSemaphoreHandle
#define xSemaphoreHandle SemaphoreHandle_t
#endif

#endif // FREERTOS_CONFIG_H
