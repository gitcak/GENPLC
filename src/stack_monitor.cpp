#include "stack_monitor.h"
#include "config/system_config.h"
#include "../modules/logging/log_buffer.h"

// ============================================================================
// STACK MONITOR SINGLETON IMPLEMENTATION
// ============================================================================
StackMonitor* StackMonitor::instance = nullptr;

StackMonitor* StackMonitor::getInstance() {
    if (!instance) {
        instance = new StackMonitor();
    }
    return instance;
}

StackMonitor::StackMonitor() {
    taskCount = 0;
    totalChecks = 0;
    criticalEvents = 0;
    warningEvents = 0;
    lastCheckTime = 0;
    
    // Initialize task tracking
    for (int i = 0; i < 16; i++) {
        trackedTasks[i].taskHandle = nullptr;
        trackedTasks[i].taskName[0] = '\0';
        trackedTasks[i].stackSize = 0;
        trackedTasks[i].stackUsed = 0;
        trackedTasks[i].stackFree = 0;
        trackedTasks[i].highWaterMark = 0;
        trackedTasks[i].usagePercent = 0.0f;
        trackedTasks[i].isCritical = false;
        trackedTasks[i].isWarning = false;
        trackedTasks[i].lastCheck = 0;
    }
    
    // Create mutex
    monitorMutex = xSemaphoreCreateMutex();
    if (!monitorMutex) {
        Serial.println("StackMonitor: Failed to create mutex");
    }
    
    Serial.println("StackMonitor: Initialized for no-PSRAM optimization");
}

StackMonitor::~StackMonitor() {
    stopMonitoring();
    
    if (monitorMutex) {
        vSemaphoreDelete(monitorMutex);
    }
    
    if (instance) {
        delete instance;
        instance = nullptr;
    }
}

// ============================================================================
// TASK MANAGEMENT
// ============================================================================
bool StackMonitor::addTask(TaskHandle_t taskHandle, const char* taskName) {
    if (!taskHandle || !taskName || taskCount >= 16) {
        return false;
    }
    
    if (!monitorMutex) {
        return false;
    }
    
    if (xSemaphoreTake(monitorMutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return false;
    }
    
    // Check if task already exists
    for (int i = 0; i < taskCount; i++) {
        if (trackedTasks[i].taskHandle == taskHandle) {
            xSemaphoreGive(monitorMutex);
            return true; // Already tracked
        }
    }
    
    // Add new task
    StackInfo& info = trackedTasks[taskCount];
    info.taskHandle = taskHandle;
    strlcpy(info.taskName, taskName, sizeof(info.taskName));
    
    // Estimate stack size (since uxTaskGetStackSize is not available)
    uint32_t highWaterMark = uxTaskGetStackHighWaterMark(taskHandle);
    info.stackSize = highWaterMark * 2; // Rough estimate
    info.stackUsed = 0;
    info.stackFree = info.stackSize;
    info.highWaterMark = highWaterMark;
    info.usagePercent = 0.0f;
    info.isCritical = false;
    info.isWarning = false;
    info.lastCheck = millis();
    
    taskCount++;
    
    xSemaphoreGive(monitorMutex);
    
    Serial.printf("StackMonitor: Added task '%s' (handle: %p)\n", taskName, taskHandle);
    return true;
}

bool StackMonitor::removeTask(TaskHandle_t taskHandle) {
    if (!taskHandle || !monitorMutex) {
        return false;
    }
    
    if (xSemaphoreTake(monitorMutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return false;
    }
    
    bool found = false;
    for (int i = 0; i < taskCount; i++) {
        if (trackedTasks[i].taskHandle == taskHandle) {
            // Shift remaining tasks
            for (int j = i; j < taskCount - 1; j++) {
                trackedTasks[j] = trackedTasks[j + 1];
            }
            taskCount--;
            found = true;
            break;
        }
    }
    
    xSemaphoreGive(monitorMutex);
    
    if (found) {
        Serial.printf("StackMonitor: Removed task (handle: %p)\n", taskHandle);
    }
    
    return found;
}

void StackMonitor::updateTask(TaskHandle_t taskHandle, const char* taskName) {
    if (!taskHandle || !taskName) return;
    
    removeTask(taskHandle);
    addTask(taskHandle, taskName);
}

// ============================================================================
// MONITORING
// ============================================================================
void StackMonitor::startMonitoring() {
    Serial.println("StackMonitor: Starting stack monitoring");
    log_add("Stack monitoring started for no-PSRAM optimization");
}

void StackMonitor::stopMonitoring() {
    Serial.println("StackMonitor: Stopping stack monitoring");
    log_add("Stack monitoring stopped");
}

void StackMonitor::checkAllTasks() {
    if (!monitorMutex) return;
    
    uint32_t currentTime = millis();
    if (currentTime - lastCheckTime < STACK_MONITOR_CHECK_INTERVAL_MS) {
        return;
    }
    
    if (xSemaphoreTake(monitorMutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return;
    }
    
    lastCheckTime = currentTime;
    totalChecks++;
    
    // Check all tracked tasks
    for (int i = 0; i < taskCount; i++) {
        updateTaskStackInfo(trackedTasks[i]);
        checkStackOverflow(trackedTasks[i]);
    }
    
    // Check heap usage (important for no-PSRAM systems)
    checkHeapUsage();
    
    // Log statistics every 10 checks
    if (totalChecks % 10 == 0) {
        logStackStatistics();
    }
    
    xSemaphoreGive(monitorMutex);
}

void StackMonitor::checkTask(TaskHandle_t taskHandle) {
    if (!taskHandle || !monitorMutex) return;
    
    if (xSemaphoreTake(monitorMutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return;
    }
    
    StackInfo* info = getTaskInfoByHandle(taskHandle);
    if (info) {
        updateTaskStackInfo(*info);
        checkStackOverflow(*info);
    }
    
    xSemaphoreGive(monitorMutex);
}

// ============================================================================
// INTERNAL METHODS
// ============================================================================
void StackMonitor::updateTaskStackInfo(StackInfo& info) {
    if (!info.taskHandle) return;
    
    uint32_t currentHighWaterMark = uxTaskGetStackHighWaterMark(info.taskHandle);
    uint32_t stackUsed = info.stackSize - (currentHighWaterMark * 4);
    
    info.stackUsed = stackUsed;
    info.stackFree = currentHighWaterMark * 4;
    info.highWaterMark = currentHighWaterMark;
    info.usagePercent = ((float)stackUsed / info.stackSize) * 100.0f;
    info.lastCheck = millis();
    
    // Update warning/critical flags
    info.isCritical = (info.usagePercent >= 90.0f);
    info.isWarning = (info.usagePercent >= 80.0f && info.usagePercent < 90.0f);
}

void StackMonitor::checkStackOverflow(const StackInfo& info) {
    if (!info.taskHandle) return;
    
    if (info.isCritical) {
        criticalEvents++;
        reportStackIssue(info);
        
        // Critical: log immediate
        logbuf_printf("CRITICAL: Task '%s' stack usage: %.1f%% (%u/%u bytes)",
                    info.taskName, info.usagePercent, info.stackUsed, info.stackSize);
    } else if (info.isWarning) {
        warningEvents++;
        
        // Warning: log less frequently
        static uint32_t lastWarningLog = 0;
        uint32_t currentTime = millis();
        if (currentTime - lastWarningLog > 60000) { // Log warnings every minute
            lastWarningLog = currentTime;
            logbuf_printf("WARNING: Task '%s' stack usage: %.1f%% (%u/%u bytes)",
                        info.taskName, info.usagePercent, info.stackUsed, info.stackSize);
        }
    }
}

void StackMonitor::reportStackIssue(const StackInfo& info) {
    Serial.printf("StackMonitor: CRITICAL - Task '%s' stack overflow risk!\n", info.taskName);
    Serial.printf("  Stack Size: %u bytes\n", info.stackSize);
    Serial.printf("  Stack Used: %u bytes (%.1f%%)\n", info.stackUsed, info.usagePercent);
    Serial.printf("  Stack Free: %u bytes\n", info.stackFree);
    Serial.printf("  High Water Mark: %u words\n", info.highWaterMark);
    
    // Set system error flag
    extern EventGroupHandle_t xEventGroupSystemStatus;
    if (xEventGroupSystemStatus) {
        xEventGroupSetBits(xEventGroupSystemStatus, EVENT_BIT_ERROR_DETECTED);
    }
}

void StackMonitor::checkHeapUsage() {
    uint32_t freeHeap = ESP.getFreeHeap();
    uint32_t minFreeHeap = ESP.getMinFreeHeap();
    
    if (freeHeap < CRITICAL_HEAP_THRESHOLD) {
        Serial.printf("StackMonitor: CRITICAL - Heap low: %u bytes (min: %u)\n", freeHeap, minFreeHeap);
        
        // Set heap low event
        extern EventGroupHandle_t xEventGroupSystemStatus;
        if (xEventGroupSystemStatus) {
            xEventGroupSetBits(xEventGroupSystemStatus, EVENT_BIT_HEAP_LOW);
        }
        
        logbuf_printf("CRITICAL: Heap low: %u bytes (< %u threshold)", freeHeap, CRITICAL_HEAP_THRESHOLD);
    } else if (freeHeap < MIN_FREE_HEAP_THRESHOLD) {
        static uint32_t lastHeapWarning = 0;
        uint32_t currentTime = millis();
        if (currentTime - lastHeapWarning > 30000) { // Log heap warnings every 30 seconds
            lastHeapWarning = currentTime;
            Serial.printf("StackMonitor: WARNING - Heap low: %u bytes\n", freeHeap);
            logbuf_printf("WARNING: Heap low: %u bytes (< %u threshold)", freeHeap, MIN_FREE_HEAP_THRESHOLD);
        }
    }
}

void StackMonitor::logStackStatistics() {
    uint32_t freeHeap = ESP.getFreeHeap();
    uint32_t totalHeap = ESP.getHeapSize();
    float heapUsagePercent = ((float)(totalHeap - freeHeap) / totalHeap) * 100.0f;
    
    logbuf_printf("Stack stats: checks=%u, critical=%u, warnings=%u, heap=%.1f%% (%u free)",
                totalChecks, criticalEvents, warningEvents, heapUsagePercent, freeHeap);
}

// ============================================================================
// STATISTICS AND DIAGNOSTICS
// ============================================================================
StackInfo* StackMonitor::getTaskInfo(const char* taskName) {
    if (!taskName || !monitorMutex) return nullptr;
    
    if (xSemaphoreTake(monitorMutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return nullptr;
    }
    
    StackInfo* result = nullptr;
    for (int i = 0; i < taskCount; i++) {
        if (strcmp(trackedTasks[i].taskName, taskName) == 0) {
            result = &trackedTasks[i];
            break;
        }
    }
    
    xSemaphoreGive(monitorMutex);
    return result;
}

StackInfo* StackMonitor::getTaskInfoByHandle(TaskHandle_t taskHandle) {
    if (!taskHandle || !monitorMutex) return nullptr;
    
    if (xSemaphoreTake(monitorMutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return nullptr;
    }
    
    StackInfo* result = nullptr;
    for (int i = 0; i < taskCount; i++) {
        if (trackedTasks[i].taskHandle == taskHandle) {
            result = &trackedTasks[i];
            break;
        }
    }
    
    xSemaphoreGive(monitorMutex);
    return result;
}

bool StackMonitor::isTaskHealthy(const char* taskName) const {
    StackInfo* info = const_cast<StackMonitor*>(this)->getTaskInfo(taskName);
    if (!info) return false;
    
    return !info->isCritical && info->usagePercent < 85.0f;
}

float StackMonitor::getHeapUsagePercent() const {
    uint32_t freeHeap = ESP.getFreeHeap();
    uint32_t totalHeap = ESP.getHeapSize();
    return ((float)(totalHeap - freeHeap) / totalHeap) * 100.0f;
}

// ============================================================================
// DIAGNOSTIC PRINTING
// ============================================================================
void StackMonitor::printStackReport() const {
    if (!monitorMutex) return;
    
    if (xSemaphoreTake(monitorMutex, pdMS_TO_TICKS(2000)) != pdTRUE) {
        Serial.println("StackMonitor: Failed to acquire mutex for report");
        return;
    }
    
    Serial.println("\n=== Stack Monitor Report ===");
    Serial.printf("Total Checks: %u\n", totalChecks);
    Serial.printf("Critical Events: %u\n", criticalEvents);
    Serial.printf("Warning Events: %u\n", warningEvents);
    Serial.printf("Free Heap: %u bytes\n", ESP.getFreeHeap());
    Serial.printf("Min Free Heap: %u bytes\n", ESP.getMinFreeHeap());
    Serial.printf("Heap Usage: %.1f%%\n", getHeapUsagePercent());
    Serial.printf("Tracked Tasks: %u\n", taskCount);
    Serial.println("--------------------------");
    
    // Print each task
    for (int i = 0; i < taskCount; i++) {
        const StackInfo& info = trackedTasks[i];
        Serial.printf("Task: %s\n", info.taskName);
        Serial.printf("  Stack Size: %u bytes\n", info.stackSize);
        Serial.printf("  Stack Used: %u bytes (%.1f%%)\n", info.stackUsed, info.usagePercent);
        Serial.printf("  Stack Free: %u bytes\n", info.stackFree);
        Serial.printf("  High Water Mark: %u words\n", info.highWaterMark);
        Serial.printf("  Status: %s\n", 
                    info.isCritical ? "CRITICAL" : 
                    info.isWarning ? "WARNING" : "OK");
        Serial.println();
    }
    
    Serial.println("============================\n");
    
    xSemaphoreGive(monitorMutex);
}

void StackMonitor::printTaskStack(const char* taskName) const {
    StackInfo* info = const_cast<StackMonitor*>(this)->getTaskInfo(taskName);
    if (!info) {
        Serial.printf("StackMonitor: Task '%s' not found\n", taskName);
        return;
    }
    
    Serial.printf("=== Stack Info for '%s' ===\n", taskName);
    Serial.printf("Stack Size: %u bytes\n", info->stackSize);
    Serial.printf("Stack Used: %u bytes (%.1f%%)\n", info->stackUsed, info->usagePercent);
    Serial.printf("Stack Free: %u bytes\n", info->stackFree);
    Serial.printf("High Water Mark: %u words\n", info->highWaterMark);
    Serial.printf("Status: %s\n", 
                info->isCritical ? "CRITICAL" : 
                info->isWarning ? "WARNING" : "OK");
    Serial.println("==========================\n");
}

void StackMonitor::printAllStacks() const {
    printStackReport();
}
