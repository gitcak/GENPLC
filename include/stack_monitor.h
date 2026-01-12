/*
 * Stack Monitoring System for No-PSRAM ESP32-S3
 * Monitors task stack usage to prevent overflow in memory-constrained environments
 */

#ifndef STACK_MONITOR_H
#define STACK_MONITOR_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "config/task_config.h"

// ============================================================================
// STACK MONITORING CONFIGURATION
// ============================================================================
#define STACK_MONITOR_CHECK_INTERVAL_MS  10000  // Check every 10 seconds
#define STACK_MONITOR_ENABLED          ENABLE_STACK_MONITORING

// ============================================================================
// STACK STATUS STRUCTURE
// ============================================================================
struct StackInfo {
    TaskHandle_t taskHandle;
    char taskName[16];
    uint32_t stackSize;
    uint32_t stackUsed;
    uint32_t stackFree;
    uint32_t highWaterMark;
    float usagePercent;
    bool isCritical;
    bool isWarning;
    uint32_t lastCheck;
};

// ============================================================================
// STACK MONITOR CLASS
// ============================================================================
class StackMonitor {
private:
    static StackMonitor* instance;
    
    // Task tracking
    StackInfo trackedTasks[16];  // Track up to 16 tasks
    uint8_t taskCount;
    
    // Statistics
    uint32_t totalChecks;
    uint32_t criticalEvents;
    uint32_t warningEvents;
    uint32_t lastCheckTime;
    
    // Mutex for thread safety
    SemaphoreHandle_t monitorMutex;
    
    StackMonitor();
    
    // Internal methods
    void updateTaskStackInfo(StackInfo& info);
    void checkStackOverflow(const StackInfo& info);
    void reportStackIssue(const StackInfo& info);
    void logStackStatistics();
    
public:
    // Singleton access
    static StackMonitor* getInstance();
    
    // Task management
    bool addTask(TaskHandle_t taskHandle, const char* taskName);
    bool removeTask(TaskHandle_t taskHandle);
    void updateTask(TaskHandle_t taskHandle, const char* taskName);
    
    // Monitoring
    void checkAllTasks();
    void checkTask(TaskHandle_t taskHandle);
    void startMonitoring();
    void stopMonitoring();
    
    // Statistics
    StackInfo* getTaskInfo(const char* taskName);
    StackInfo* getTaskInfoByHandle(TaskHandle_t taskHandle);
    uint8_t getTaskCount() const { return taskCount; }
    uint32_t getTotalChecks() const { return totalChecks; }
    uint32_t getCriticalEvents() const { return criticalEvents; }
    uint32_t getWarningEvents() const { return warningEvents; }
    
    // Diagnostics
    void printStackReport() const;
    void printTaskStack(const char* taskName) const;
    void printAllStacks() const;
    bool isTaskHealthy(const char* taskName) const;
    
    // Heap monitoring (for no-PSRAM systems)
    void checkHeapUsage();
    uint32_t getFreeHeap() const { return ESP.getFreeHeap(); }
    uint32_t getMinFreeHeap() const { return ESP.getMinFreeHeap(); }
    float getHeapUsagePercent() const;
    
    // Destructor
    ~StackMonitor();
};

// ============================================================================
// STACK MONITORING MACROS
// ============================================================================
#if STACK_MONITOR_ENABLED
    #define START_STACK_MONITORING() StackMonitor::getInstance()->startMonitoring()
    #define STOP_STACK_MONITORING() StackMonitor::getInstance()->stopMonitoring()
    #define ADD_STACK_MONITOR(task) StackMonitor::getInstance()->addTask(task, #task)
    #define REMOVE_STACK_MONITOR(task) StackMonitor::getInstance()->removeTask(task)
    #define CHECK_STACK_USAGE() StackMonitor::getInstance()->checkAllTasks()
    #define PRINT_STACK_REPORT() StackMonitor::getInstance()->printStackReport()
#else
    #define START_STACK_MONITORING() do {} while(0)
    #define STOP_STACK_MONITORING() do {} while(0)
    #define ADD_STACK_MONITOR(task) do {} while(0)
    #define REMOVE_STACK_MONITOR(task) do {} while(0)
    #define CHECK_STACK_USAGE() do {} while(0)
    #define PRINT_STACK_REPORT() do {} while(0)
#endif

// ============================================================================
// STACK GUARD CLASS (RAII for stack monitoring)
// ============================================================================
class StackGuard {
private:
    TaskHandle_t taskHandle;
    const char* taskName;
    uint32_t startStack;
    bool monitoring;
    
public:
    StackGuard(const char* name) : taskHandle(xTaskGetCurrentTaskHandle()), taskName(name), startStack(0), monitoring(false) {
        if (StackMonitor::getInstance()->addTask(taskHandle, taskName)) {
            monitoring = true;
            startStack = uxTaskGetStackHighWaterMark(taskHandle);
        }
    }
    
    ~StackGuard() {
        if (monitoring && StackMonitor::getInstance()) {
            StackMonitor::getInstance()->removeTask(taskHandle);
        }
    }
    
    uint32_t getStackUsed() const {
        return uxTaskGetStackHighWaterMark(taskHandle);
    }
    
    float getStackUsagePercent() const {
        if (!taskHandle) return 0.0f;
        
        // Get task info to determine stack size
        eTaskState state = eTaskGetState(taskHandle);
        if (state == eDeleted) return 0.0f;
        
        // Estimate stack size based on water mark and typical usage
        uint32_t highWaterMark = uxTaskGetStackHighWaterMark(taskHandle);
        uint32_t estimatedStackSize = highWaterMark * 2; // Rough estimate
        
        if (estimatedStackSize == 0) return 0.0f;
        
        uint32_t used = estimatedStackSize - highWaterMark;
        return ((float)used / estimatedStackSize) * 100.0f;
    }
    
    bool isStackHealthy() const {
        return getStackUsagePercent() < 80.0f;  // 80% threshold
    }
};

// ============================================================================
// HEAP MONITORING MACROS (for no-PSRAM systems)
// ============================================================================
#define CHECK_HEAP_USAGE() StackMonitor::getInstance()->checkHeapUsage()
#define GET_FREE_HEAP() ESP.getFreeHeap()
#define GET_MIN_FREE_HEAP() ESP.getMinFreeHeap()
#define IS_HEAP_CRITICAL() (ESP.getFreeHeap() < CRITICAL_HEAP_THRESHOLD)
#define IS_HEAP_LOW() (ESP.getFreeHeap() < MIN_FREE_HEAP_THRESHOLD)

#endif // STACK_MONITOR_H
