/*
 * Task Scheduler Implementation
 * Advanced task scheduling with dynamic priorities and CPU load balancing
 */

#include "task_scheduler.h"
#include "../include/memory_pool.h"
#include "../include/error_handler.h"
#include "../modules/logging/log_buffer.h"
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

// ============================================================================
// GLOBAL INSTANCE
// ============================================================================
TaskScheduler* TaskScheduler::instance = nullptr;

// ============================================================================
// CONSTRUCTOR
// ============================================================================
TaskScheduler::TaskScheduler() {
    // Initialize task registry
    memset(taskRegistry, 0, sizeof(taskRegistry));
    taskCount = 0;
    
    // Initialize CPU load tracking
    cpuLoadHistory = (float*)POOL_ALLOC(CPU_LOAD_HISTORY_SIZE * sizeof(float));
    if (cpuLoadHistory) {
        memset(cpuLoadHistory, 0, CPU_LOAD_HISTORY_SIZE * sizeof(float));
        cpuLoadIndex = 0;
        currentCpuLoad = 0.0f;
    }
    
    // Initialize synchronization
    schedulerMutex = xSemaphoreCreateMutex();
    loadBalancerMutex = xSemaphoreCreateMutex();
    
    // Initialize performance metrics
    memset(&performanceMetrics, 0, sizeof(performanceMetrics));
    performanceMetrics.last_update = millis();
    
    // Start background tasks
    startSchedulerTasks();
    
    Serial.println("TaskScheduler: Advanced task scheduling initialized");
}

// ============================================================================
// DESTRUCTOR
// ============================================================================
TaskScheduler::~TaskScheduler() {
    // Stop all scheduled tasks
    stopAllScheduledTasks();
    
    // Cleanup resources
    if (cpuLoadHistory) {
        POOL_FREE(cpuLoadHistory);
    }
    
    if (schedulerMutex) {
        vSemaphoreDelete(schedulerMutex);
    }
    
    if (loadBalancerMutex) {
        vSemaphoreDelete(loadBalancerMutex);
    }
}

// ============================================================================
// TASK REGISTRATION
// ============================================================================
bool TaskScheduler::registerTask(const TaskInfo& taskInfo) {
    if (!takeSchedulerMutex()) return false;
    
    if (taskCount >= MAX_SCHEDULED_TASKS) {
        giveSchedulerMutex();
        REPORT_ERROR(ERROR_SYSTEM_FAULT, ErrorSeverity::ERROR, ErrorCategory::SYSTEM,
                      "Task registry full");
        return false;
    }
    
    // Check for duplicate handles
    for (uint8_t i = 0; i < taskCount; i++) {
        if (taskRegistry[i].handle == taskInfo.handle) {
            giveSchedulerMutex();
            return true; // Already registered
        }
    }
    
    // Add new task
    TaskInfo& task = taskRegistry[taskCount];
    task = taskInfo;
    task.is_active = false;
    task.execution_count = 0;
    task.total_execution_time = 0;
    task.max_execution_time = 0;
    task.last_execution = 0;
    task.average_execution_time = 0;
    task.stack_peak = 0;
    task.priority_history_index = 0;
    memset(task.priority_history, 0, sizeof(task.priority_history));
    
    // Set initial dynamic priority
    task.dynamic_priority = task.base_priority;
    
    taskCount++;
    
    giveSchedulerMutex();
    
    logbuf_printf("TaskScheduler: Registered task '%s' (handle: %p, priority: %u)",
                 task.name, task.handle, task.base_priority);
    
    return true;
}

bool TaskScheduler::unregisterTask(TaskHandle_t handle) {
    if (!takeSchedulerMutex()) return false;
    
    bool found = false;
    for (uint8_t i = 0; i < taskCount; i++) {
        if (taskRegistry[i].handle == handle) {
            // Shift remaining tasks
            for (uint8_t j = i; j < taskCount - 1; j++) {
                taskRegistry[j] = taskRegistry[j + 1];
            }
            taskCount--;
            found = true;
            break;
        }
    }
    
    giveSchedulerMutex();
    
    if (found) {
        logbuf_printf("TaskScheduler: Unregistered task handle %p", handle);
    }
    
    return found;
}

// ============================================================================
// PRIORITY MANAGEMENT
// ============================================================================
bool TaskScheduler::adjustTaskPriority(TaskHandle_t handle, UBaseType_t newPriority) {
    if (!takeSchedulerMutex()) return false;
    
    bool found = false;
    for (uint8_t i = 0; i < taskCount; i++) {
        if (taskRegistry[i].handle == handle) {
            TaskInfo& task = taskRegistry[i];
            
            // Validate priority range
            if (newPriority < 1 || newPriority > configMAX_PRIORITIES - 1) {
                giveSchedulerMutex();
                return false;
            }
            
            // Record priority change
            recordPriorityChange(task, newPriority);
            
            // Apply new priority
            task.dynamic_priority = newPriority;
            vTaskPrioritySet(handle, newPriority);
            
            found = true;
            break;
        }
    }
    
    giveSchedulerMutex();
    
    if (found) {
        logbuf_printf("TaskScheduler: Adjusted priority to %u for task handle %p", newPriority, handle);
    }
    
    return found;
}

void TaskScheduler::recordPriorityChange(TaskInfo& task, UBaseType_t newPriority) {
    // Store priority change in history
    task.priority_history[task.priority_history_index] = newPriority;
    task.priority_history_index = (task.priority_history_index + 1) % PRIORITY_HISTORY_SIZE;
    
    // Calculate average priority
    uint32_t sum = 0;
    for (uint8_t i = 0; i < PRIORITY_HISTORY_SIZE; i++) {
        sum += task.priority_history[i];
    }
    task.average_priority = sum / PRIORITY_HISTORY_SIZE;
}

// ============================================================================
// LOAD BALANCING
// ============================================================================
void TaskScheduler::balanceCpuLoad() {
    if (!takeLoadBalancerMutex()) return;
    
    // Get current CPU load
    float currentLoad = measureCpuLoad();
    updateCpuLoadHistory(currentLoad);
    currentCpuLoad = currentLoad;
    
    // Determine load balancing strategy
    LoadBalanceStrategy strategy = determineLoadBalanceStrategy(currentLoad);
    
    switch (strategy) {
        case LoadBalanceStrategy::AGGRESSIVE_PERFORMANCE:
            balanceForPerformance();
            break;
            
        case LoadBalanceStrategy::POWER_SAVING:
            balanceForPower();
            break;
            
        case LoadBalanceStrategy::BALANCED:
            balanceForBalanced();
            break;
            
        case LoadBalanceStrategy::ADAPTIVE:
            balanceAdaptive(currentLoad);
            break;
    }
    
    giveLoadBalancerMutex();
    
    logbuf_printf("CPU load: %.1f%%, strategy: %s", 
                 currentLoad * 100.0f, strategyToString(strategy));
}

LoadBalanceStrategy TaskScheduler::determineLoadBalanceStrategy(float cpuLoad) const {
    // Strategy based on current load and configuration
    if (cpuLoad > 0.8f) {
        return LoadBalanceStrategy::POWER_SAVING;
    } else if (cpuLoad > 0.5f) {
        return LoadBalanceStrategy::BALANCED;
    } else if (cpuLoad < 0.2f) {
        return LoadBalanceStrategy::AGGRESSIVE_PERFORMANCE;
    } else {
        return LoadBalanceStrategy::ADAPTIVE;
    }
}

void TaskScheduler::balanceForPerformance() {
    // Prioritize high-priority tasks, increase execution frequency
    for (uint8_t i = 0; i < taskCount; i++) {
        TaskInfo& task = taskRegistry[i];
        
        if (task.is_critical && task.base_priority < PERFORMANCE_PRIORITY_BOOST) {
            UBaseType_t boostedPriority = (UBaseType_t)(task.base_priority + PERFORMANCE_PRIORITY_BOOST);
            if (boostedPriority < configMAX_PRIORITIES - 1) {
                vTaskPrioritySet(task.handle, boostedPriority);
                task.dynamic_priority = boostedPriority;
            }
        }
    }
}

void TaskScheduler::balanceForPower() {
    // Reduce task priorities, extend delays for non-critical tasks
    for (uint8_t i = 0; i < taskCount; i++) {
        TaskInfo& task = taskRegistry[i];
        
        if (!task.is_critical) {
            UBaseType_t reducedPriority = (UBaseType_t)max((int)1, (int)(task.base_priority - 2));
            vTaskPrioritySet(task.handle, reducedPriority);
            task.dynamic_priority = reducedPriority;
        }
    }
}

void TaskScheduler::balanceForBalanced() {
    // Restore base priorities with slight adjustments based on history
    for (uint8_t i = 0; i < taskCount; i++) {
        TaskInfo& task = taskRegistry[i];
        
        // Use average priority if it differs significantly from base
        if (abs((int)task.average_priority - (int)task.base_priority) > 2) {
            UBaseType_t adjustedPriority = (UBaseType_t)task.average_priority;
            vTaskPrioritySet(task.handle, adjustedPriority);
            task.dynamic_priority = adjustedPriority;
        } else {
            vTaskPrioritySet(task.handle, task.base_priority);
            task.dynamic_priority = task.base_priority;
        }
    }
}

void TaskScheduler::balanceAdaptive(float cpuLoad) {
    // Adaptive balancing based on recent performance
    for (uint8_t i = 0; i < taskCount; i++) {
        TaskInfo& task = taskRegistry[i];
        
        // Calculate efficiency score
        float efficiency = calculateTaskEfficiency(task);
        
        if (efficiency > 0.8f && task.average_execution_time > 0) {
            // High efficiency tasks get priority boost
            UBaseType_t boost = (UBaseType_t)min((int)3, (int)((1.0f - cpuLoad) * 5));
            UBaseType_t newPriority = (UBaseType_t)min((int)(configMAX_PRIORITIES - 1), 
                                                  (int)(task.base_priority + boost));
            vTaskPrioritySet(task.handle, newPriority);
            task.dynamic_priority = newPriority;
        } else if (efficiency < 0.3f) {
            // Low efficiency tasks get priority reduction
            UBaseType_t newPriority = (UBaseType_t)max((int)1, (int)(task.base_priority - 1));
            vTaskPrioritySet(task.handle, newPriority);
            task.dynamic_priority = newPriority;
        } else {
            // Normal priority
            vTaskPrioritySet(task.handle, task.base_priority);
            task.dynamic_priority = task.base_priority;
        }
    }
}

// ============================================================================
// TASK MONITORING
// ============================================================================
void TaskScheduler::monitorTaskPerformance() {
    if (!takeSchedulerMutex()) return;
    
    uint32_t currentTime = millis();
    
    for (uint8_t i = 0; i < taskCount; i++) {
        TaskInfo& task = taskRegistry[i];
        
        // Get current stack high water mark
        uint32_t stackHighWaterMark = uxTaskGetStackHighWaterMark(task.handle);
        if (stackHighWaterMark > task.stack_peak) {
            task.stack_peak = stackHighWaterMark;
        }
        
        // Check task health
        checkTaskHealth(task, currentTime);
        
        // Update performance metrics
        if (task.is_active) {
            uint32_t executionTime = currentTime - task.last_execution;
            task.total_execution_time += executionTime;
            task.execution_count++;
            
            if (executionTime > task.max_execution_time) {
                task.max_execution_time = executionTime;
            }
            
            // Calculate average execution time
            task.average_execution_time = task.total_execution_time / task.execution_count;
        }
    }
    
    giveSchedulerMutex();
}

void TaskScheduler::checkTaskHealth(TaskInfo& task, uint32_t currentTime) {
    // Check for task timeout
    if (task.is_active && task.max_execution_time > 0) {
        uint32_t executionTime = currentTime - task.last_execution;
        
        if (executionTime > task.max_execution_time * 3) {
            // Task may be stuck
            logbuf_printf("TaskScheduler: Task '%s' may be stuck (executing for %ums)",
                         task.name, executionTime);
            
            REPORT_ERROR(ERROR_SYSTEM_FAULT, ErrorSeverity::WARNING, ErrorCategory::SYSTEM,
                          "Possible task hang detected");
            
            // Consider task restart if enabled
            if (task.enable_auto_restart) {
                restartTask(task);
            }
        }
    }
    
    // Check stack usage
    if (task.stack_peak < STACK_USAGE_WARNING_THRESHOLD) {
        logbuf_printf("TaskScheduler: Task '%s' stack usage high (%u bytes remaining)",
                     task.name, task.stack_peak);
        
        REPORT_ERROR(ERROR_MEMORY_FAULT, ErrorSeverity::WARNING, ErrorCategory::SYSTEM,
                      "High stack usage detected");
    }
}

// ============================================================================
// PERFORMANCE ANALYSIS
// ============================================================================
float TaskScheduler::measureCpuLoad() {
    // Use FreeRTOS runtime stats if available
    #if (configGENERATE_RUN_TIME_STATS == 1)
        static uint32_t previousTotalRunTime = 0;
        static uint32_t previousIdleTime = 0;
        
        uint32_t totalRunTime, idleTime;
        vTaskGetRunTimeStats(&totalRunTime, &idleTime);
        
        uint32_t totalDelta = totalRunTime - previousTotalRunTime;
        uint32_t idleDelta = idleTime - previousIdleTime;
        
        if (totalDelta > 0) {
            float cpuLoad = 1.0f - ((float)idleDelta / totalDelta);
            
            previousTotalRunTime = totalRunTime;
            previousIdleTime = idleTime;
            
            return cpuLoad;
        }
    #endif
    
    // Fallback: estimate based on task activity
    return estimateCpuLoadFromTasks();
}

float TaskScheduler::estimateCpuLoadFromTasks() {
    uint32_t activeTasks = 0;
    uint32_t totalTasks = 0;
    
    for (uint8_t i = 0; i < taskCount; i++) {
        TaskInfo& task = taskRegistry[i];
        totalTasks++;
        
        if (task.is_active) {
            activeTasks++;
        }
    }
    
    return totalTasks > 0 ? (float)activeTasks / totalTasks : 0.0f;
}

void TaskScheduler::updateCpuLoadHistory(float load) {
    cpuLoadHistory[cpuLoadIndex] = load;
    cpuLoadIndex = (cpuLoadIndex + 1) % CPU_LOAD_HISTORY_SIZE;
}

float TaskScheduler::getAverageCpuLoad() const {
    float sum = 0.0f;
    for (uint8_t i = 0; i < CPU_LOAD_HISTORY_SIZE; i++) {
        sum += cpuLoadHistory[i];
    }
    return sum / CPU_LOAD_HISTORY_SIZE;
}

float TaskScheduler::calculateTaskEfficiency(const TaskInfo& task) const {
    if (task.execution_count == 0 || task.average_execution_time == 0) {
        return 0.5f; // Neutral efficiency
    }
    
    // Efficiency based on consistent execution times
    float variance = calculateExecutionTimeVariance(task);
    float normalizedVariance = variance / (task.average_execution_time * task.average_execution_time);
    
    // Lower variance = higher efficiency
    return max(0.0f, 1.0f - normalizedVariance);
}

float TaskScheduler::calculateExecutionTimeVariance(const TaskInfo& task) const {
    if (task.execution_count < 2) {
        return 0.0f; // Cannot calculate variance with single sample
    }
    
    // Simple variance calculation (would need more data for accurate calculation)
    return (float)(task.max_execution_time) / 2.0f; // Simplified variance
}

void TaskScheduler::updatePerformanceMetrics() {
    if (!takeSchedulerMutex()) return;
    
    uint32_t currentTime = millis();
    
    // Update timing
    performanceMetrics.last_update = currentTime;
    performanceMetrics.uptime = currentTime - performanceMetrics.last_update; // Use last_update as startup time
    
    // Update task statistics
    performanceMetrics.total_tasks = taskCount;
    performanceMetrics.active_tasks = 0;
    performanceMetrics.critical_tasks = 0;
    
    for (uint8_t i = 0; i < taskCount; i++) {
        TaskInfo& task = taskRegistry[i];
        if (task.is_active) {
            performanceMetrics.active_tasks++;
        }
        if (task.is_critical) {
            performanceMetrics.critical_tasks++;
        }
    }
    
    // Update CPU metrics
    updateCpuMetrics();
    
    // Update memory metrics
    updateMemoryMetrics();
    
    // Update health metrics
    updateHealthMetrics();
    
    giveSchedulerMutex();
}

void TaskScheduler::updateCpuMetrics() {
    performanceMetrics.current_cpu_load = currentCpuLoad;
    
    if (currentCpuLoad > performanceMetrics.max_cpu_load) {
        performanceMetrics.max_cpu_load = currentCpuLoad;
    }
    
    if (performanceMetrics.min_cpu_load == 0.0f || currentCpuLoad < performanceMetrics.min_cpu_load) {
        performanceMetrics.min_cpu_load = currentCpuLoad;
    }
    
    performanceMetrics.average_cpu_load = getAverageCpuLoad();
}

void TaskScheduler::updateMemoryMetrics() {
    uint32_t freeHeap = ESP.getFreeHeap();
    uint32_t totalHeap = ESP.getHeapSize();
    
    performanceMetrics.total_heap_used = totalHeap - freeHeap;
    performanceMetrics.total_stack_used = 0;
    performanceMetrics.total_memory_allocated = performanceMetrics.total_heap_used;
    
    // Calculate stack usage
    for (uint8_t i = 0; i < taskCount; i++) {
        TaskInfo& task = taskRegistry[i];
        performanceMetrics.total_stack_used += task.stack_peak;
    }
}

void TaskScheduler::updateHealthMetrics() {
    // Simple health score calculation
    float cpuHealth = max(0.0f, 100.0f - (performanceMetrics.current_cpu_load * 100.0f));
    float memoryHealth = (float)(ESP.getFreeHeap() * 100) / ESP.getHeapSize();
    float taskHealth = taskCount > 0 ? 
        (float)performanceMetrics.active_tasks / taskCount * 100.0f : 100.0f;
    
    performanceMetrics.overall_health_score = (cpuHealth * 0.4f) + 
                                         (memoryHealth * 0.3f) + 
                                         (taskHealth * 0.3f);
    
    performanceMetrics.system_is_healthy = performanceMetrics.overall_health_score >= 70.0f;
}

void TaskScheduler::startSchedulerTasks() {
    startTaskMonitoring();
}

// Additional public methods
void TaskScheduler::stopAllScheduledTasks() {
    if (taskMonitorTask) {
        vTaskDelete(taskMonitorTask);
        taskMonitorTask = nullptr;
    }
    
    if (loadBalancerTask) {
        vTaskDelete(loadBalancerTask);
        loadBalancerTask = nullptr;
    }
}

void TaskScheduler::resetCpuLoadHistory() {
    if (cpuLoadHistory) {
        memset(cpuLoadHistory, 0, CPU_LOAD_HISTORY_SIZE * sizeof(float));
        cpuLoadIndex = 0;
        currentCpuLoad = 0.0f;
    }
}

void TaskScheduler::setLoadBalanceStrategy(LoadBalanceStrategy strategy) {
    if (takeSchedulerMutex()) {
        performanceMetrics.current_strategy = strategy;
        performanceMetrics.last_strategy_change = millis();
        performanceMetrics.strategy_changes++;
        giveSchedulerMutex();
    }
}

void TaskScheduler::enableAutoLoadBalancing(bool enable) {
    // Implementation would enable/disable automatic load balancing
    logbuf_printf("Auto load balancing %s", enable ? "enabled" : "disabled");
}

void TaskScheduler::restoreBasePriorities() {
    if (!takeSchedulerMutex()) return;
    
    for (uint8_t i = 0; i < taskCount; i++) {
        TaskInfo& task = taskRegistry[i];
        vTaskPrioritySet(task.handle, task.base_priority);
        task.dynamic_priority = task.base_priority;
    }
    
    giveSchedulerMutex();
}

void TaskScheduler::optimizePriorities() {
    balanceCpuLoad();
}

void TaskScheduler::performSystemHealthCheck() {
    updatePerformanceMetrics();
    
    if (performanceMetrics.overall_health_score < 50.0f) {
        REPORT_ERROR(ERROR_SYSTEM_FAULT, ErrorSeverity::ERROR, ErrorCategory::SYSTEM,
                      "System health critical");
    } else if (performanceMetrics.overall_health_score < 70.0f) {
        REPORT_ERROR(ERROR_SYSTEM_FAULT, ErrorSeverity::WARNING, ErrorCategory::SYSTEM,
                      "System health degraded");
    }
}

void TaskScheduler::generateHealthReport() {
    logbuf_printf("=== System Health Report ===");
    logbuf_printf("Overall Health: %.1f%%", performanceMetrics.overall_health_score);
    logbuf_printf("CPU Load: %.1f%%", performanceMetrics.current_cpu_load * 100.0f);
    logbuf_printf("Active Tasks: %u/%u", performanceMetrics.active_tasks, performanceMetrics.total_tasks);
    logbuf_printf("Heap Used: %u bytes", performanceMetrics.total_heap_used);
    logbuf_printf("Uptime: %lu ms", performanceMetrics.uptime);
    logbuf_printf("==========================");
}

bool TaskScheduler::boostTaskPriority(TaskHandle_t handle, uint8_t boost) {
    for (uint8_t i = 0; i < taskCount; i++) {
        if (taskRegistry[i].handle == handle) {
            UBaseType_t newPriority = taskRegistry[i].base_priority + boost;
            return adjustTaskPriority(handle, newPriority);
        }
    }
    return false;
}

bool TaskScheduler::reduceTaskPriority(TaskHandle_t handle, uint8_t reduction) {
    for (uint8_t i = 0; i < taskCount; i++) {
        if (taskRegistry[i].handle == handle) {
            UBaseType_t newPriority = taskRegistry[i].base_priority - reduction;
            if (newPriority < 1) newPriority = 1;
            return adjustTaskPriority(handle, newPriority);
        }
    }
    return false;
}

TaskInfo* TaskScheduler::getTaskInfo(TaskHandle_t handle) {
    for (uint8_t i = 0; i < taskCount; i++) {
        if (taskRegistry[i].handle == handle) {
            return &taskRegistry[i];
        }
    }
    return nullptr;
}

TaskInfo* TaskScheduler::getTaskInfo(const char* name) {
    for (uint8_t i = 0; i < taskCount; i++) {
        if (strcmp(taskRegistry[i].name, name) == 0) {
            return &taskRegistry[i];
        }
    }
    return nullptr;
}

void TaskScheduler::optimizeForPerformance() {
    setLoadBalanceStrategy(LoadBalanceStrategy::AGGRESSIVE_PERFORMANCE);
    balanceCpuLoad();
}

void TaskScheduler::optimizeForPower() {
    setLoadBalanceStrategy(LoadBalanceStrategy::POWER_SAVING);
    balanceCpuLoad();
}

void TaskScheduler::optimizeForMemory() {
    // Memory optimization would involve garbage collection and buffer cleanup
    logbuf_printf("Optimizing for memory usage");
}

void TaskScheduler::optimizeOverall() {
    setLoadBalanceStrategy(LoadBalanceStrategy::BALANCED);
    balanceCpuLoad();
}

bool TaskScheduler::enableTaskAffinity() {
    #if (configNUM_CORES > 1)
        optimizeTaskAffinity();
        return true;
    #else
        return false;
    #endif
}

bool TaskScheduler::disableTaskAffinity() {
    // Remove core affinity restrictions
    #if (configNUM_CORES > 1)
        for (uint8_t i = 0; i < taskCount; i++) {
            TaskInfo& task = taskRegistry[i];
            vTaskCoreAffinitySet(task.handle, tskNO_AFFINITY);
        }
        return true;
    #else
        return false;
    #endif
}

void TaskScheduler::setTaskCoreAffinity(TaskHandle_t handle, uint32_t coreMask) {
    #if (configNUM_CORES > 1)
        vTaskCoreAffinitySet(handle, coreMask);
    #endif
}

void TaskScheduler::migrateTaskToCore(TaskHandle_t handle, uint32_t targetCore) {
    #if (configNUM_CORES > 1)
        setTaskCoreAffinity(handle, 1 << targetCore);
    #endif
}

void TaskScheduler::printTaskRegistry() const {
    logbuf_printf("=== Task Registry ===");
    for (uint8_t i = 0; i < taskCount; i++) {
        const TaskInfo& task = taskRegistry[i];
        logbuf_printf("Task: %s, Priority: %u/%u, Stack: %u, Active: %s",
                     task.name, task.dynamic_priority, task.base_priority,
                     task.stack_peak, task.is_active ? "Yes" : "No");
    }
    logbuf_printf("===================");
}

void TaskScheduler::printPerformanceMetrics() const {
    logbuf_printf("=== Performance Metrics ===");
    logbuf_printf("CPU Load: %.1f%% (Avg: %.1f%%, Max: %.1f%%)",
                 performanceMetrics.current_cpu_load * 100.0f,
                 performanceMetrics.average_cpu_load * 100.0f,
                 performanceMetrics.max_cpu_load * 100.0f);
    logbuf_printf("Memory: %u bytes heap, %u bytes stack",
                 performanceMetrics.total_heap_used, performanceMetrics.total_stack_used);
    logbuf_printf("Health: %.1f%%, Tasks: %u/%u active",
                 performanceMetrics.overall_health_score,
                 performanceMetrics.active_tasks, performanceMetrics.total_tasks);
    logbuf_printf("========================");
}

void TaskScheduler::printLoadBalanceStatus() const {
    logbuf_printf("=== Load Balance Status ===");
    logbuf_printf("Strategy: %s", strategyToString(performanceMetrics.current_strategy));
    logbuf_printf("Strategy Changes: %u", performanceMetrics.strategy_changes);
    logbuf_printf("Last Change: %lu ms ago", millis() - performanceMetrics.last_strategy_change);
    logbuf_printf("==========================");
}

void TaskScheduler::exportTaskStatistics(char* buffer, size_t bufferSize) {
    if (!buffer || bufferSize == 0) return;
    
    snprintf(buffer, bufferSize,
             "TaskStats: total=%u,active=%u,cpu=%.1f%%,health=%.1f%%,restarts=%u",
             performanceMetrics.total_tasks,
             performanceMetrics.active_tasks,
             performanceMetrics.current_cpu_load * 100.0f,
             performanceMetrics.overall_health_score,
             performanceMetrics.task_restarts);
}

// ============================================================================
// TASK MANAGEMENT
// ============================================================================
void TaskScheduler::startTaskMonitoring() {
    if (!taskMonitorTask) {
        xTaskCreate(
            vTaskSchedulerMonitor,
            "TaskMonitor",
            TASK_SCHEDULER_STACK_SIZE,
            nullptr,
            TASK_SCHEDULER_PRIORITY,
            &taskMonitorTask
        );
    }
}

void TaskScheduler::restartTask(TaskInfo& task) {
    logbuf_printf("TaskScheduler: Restarting task '%s'", task.name);
    
    // Note: This would need to be implemented carefully to ensure proper cleanup
    // For now, just log the restart request
    performanceMetrics.task_restarts++;
}

void TaskScheduler::optimizeTaskAffinity() {
    #if (configNUM_CORES > 1)
        // Distribute tasks across cores for ESP32-S3
        uint32_t core0Tasks = 0;
        uint32_t core1Tasks = 0;
        
        for (uint8_t i = 0; i < taskCount; i++) {
            TaskInfo& task = taskRegistry[i];
            
            eTaskState state = eTaskGetState(task.handle);
            if (state != eDeleted && state != eInvalid) {
                if (core0Tasks <= core1Tasks) {
                    vTaskCoreAffinitySet(task.handle, 0);
                    core0Tasks++;
                } else {
                    vTaskCoreAffinitySet(task.handle, 1);
                    core1Tasks++;
                }
            }
        }
        
        logbuf_printf("TaskScheduler: Distributed tasks - Core0: %u, Core1: %u",
                     core0Tasks, core1Tasks);
    #endif
}

// ============================================================================
// SYNCHRONIZATION HELPERS
// ============================================================================
bool TaskScheduler::takeSchedulerMutex() {
    return (schedulerMutex && 
            xSemaphoreTake(schedulerMutex, pdMS_TO_TICKS(100)) == pdTRUE);
}

bool TaskScheduler::takeLoadBalancerMutex() {
    return (loadBalancerMutex && 
            xSemaphoreTake(loadBalancerMutex, pdMS_TO_TICKS(50)) == pdTRUE);
}

void TaskScheduler::giveSchedulerMutex() {
    if (schedulerMutex) {
        xSemaphoreGive(schedulerMutex);
    }
}

void TaskScheduler::giveLoadBalancerMutex() {
    if (loadBalancerMutex) {
        xSemaphoreGive(loadBalancerMutex);
    }
}

// ============================================================================
// TASK FUNCTIONS
// ============================================================================
void vTaskSchedulerMonitor(void* pvParameters) {
    TaskScheduler* scheduler = TaskScheduler::getInstance();
    
    while (true) {
        // Monitor task performance
        scheduler->monitorTaskPerformance();
        
        // Balance CPU load
        scheduler->balanceCpuLoad();
        
        // Update performance metrics
        scheduler->updatePerformanceMetrics();
        
        // Check for optimizations
        if (millis() - scheduler->performanceMetrics.last_optimization > OPTIMIZATION_INTERVAL_MS) {
            scheduler->optimizeTaskAffinity();
            scheduler->performanceMetrics.last_optimization = millis();
        }
        
        vTaskDelay(pdMS_TO_TICKS(TASK_MONITOR_INTERVAL_MS));
    }
}

void vTaskLoadBalancer(void* pvParameters) {
    TaskScheduler* scheduler = TaskScheduler::getInstance();
    
    while (true) {
        // Perform load balancing
        scheduler->balanceCpuLoad();
        
        vTaskDelay(pdMS_TO_TICKS(LOAD_BALANCER_INTERVAL_MS));
    }
}
