/*
 * Task Scheduler Header
 * Advanced task scheduling with dynamic priorities and CPU load balancing
 */

#ifndef TASK_SCHEDULER_H
#define TASK_SCHEDULER_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

// ============================================================================
// SCHEDULER CONFIGURATION
// ============================================================================
#define MAX_SCHEDULED_TASKS 16
#define CPU_LOAD_HISTORY_SIZE 20
#define PRIORITY_HISTORY_SIZE 8
#define STACK_USAGE_WARNING_THRESHOLD 1024  // 1KB minimum free stack
#define PERFORMANCE_PRIORITY_BOOST 2

#define TASK_SCHEDULER_STACK_SIZE 4096
#define TASK_SCHEDULER_PRIORITY 3
#define TASK_MONITOR_INTERVAL_MS 5000
#define LOAD_BALANCER_INTERVAL_MS 10000
#define OPTIMIZATION_INTERVAL_MS 60000

// ============================================================================
// LOAD BALANCING STRATEGIES
// ============================================================================
enum class LoadBalanceStrategy : uint8_t {
    AGGRESSIVE_PERFORMANCE = 0,
    BALANCED = 1,
    POWER_SAVING = 2,
    ADAPTIVE = 3
};

// ============================================================================
// TASK INFORMATION STRUCTURE
// ============================================================================
struct TaskInfo {
    TaskHandle_t handle;
    char name[32];
    UBaseType_t base_priority;
    UBaseType_t dynamic_priority;
    
    // Performance tracking
    bool is_active;
    bool is_critical;
    bool enable_auto_restart;
    uint32_t execution_count;
    uint32_t total_execution_time;
    uint32_t max_execution_time;
    uint32_t last_execution;
    uint32_t average_execution_time;
    uint32_t stack_peak;
    
    // Priority history
    UBaseType_t priority_history[PRIORITY_HISTORY_SIZE];
    UBaseType_t average_priority;
    uint8_t priority_history_index;
    
    // Resource usage
    uint32_t memory_allocated;
    uint32_t cpu_time_total;
    float cpu_percentage;
    
    // Health monitoring
    uint32_t last_health_check;
    uint8_t health_score;
    bool needs_restart;
    uint32_t restart_count;
    
    // Task configuration
    uint32_t max_allowed_execution_time;
    uint32_t health_check_interval;
    float max_cpu_percentage;
    uint32_t min_stack_free;
};

// ============================================================================
// PERFORMANCE METRICS
// ============================================================================
struct PerformanceMetrics {
    // CPU load tracking
    float current_cpu_load;
    float average_cpu_load;
    float max_cpu_load;
    float min_cpu_load;
    
    // Task statistics
    uint32_t total_tasks;
    uint32_t active_tasks;
    uint32_t critical_tasks;
    uint32_t task_restarts;
    uint32_t priority_adjustments;
    
    // Memory usage
    uint32_t total_heap_used;
    uint32_t total_stack_used;
    uint32_t total_memory_allocated;
    
    // Timing information
    uint32_t last_update;
    uint32_t last_optimization;
    uint32_t uptime;
    
    // System health
    float overall_health_score;
    bool system_is_healthy;
    uint8_t warning_count;
    uint8_t error_count;
    
    // Load balancing
    LoadBalanceStrategy current_strategy;
    uint32_t strategy_changes;
    uint32_t last_strategy_change;
    
    // Performance trends
    float cpu_trend[4];      // Hourly averages
    float memory_trend[4];   // Hourly averages
    uint8_t trend_index;
};

// ============================================================================
// TASK SCHEDULER CLASS
// ============================================================================
class TaskScheduler {
private:
    // Task registry
    TaskInfo taskRegistry[MAX_SCHEDULED_TASKS];
    uint8_t taskCount;
    
    // CPU load tracking
    float* cpuLoadHistory;
    float currentCpuLoad;
    uint8_t cpuLoadIndex;
    
    // Synchronization
    SemaphoreHandle_t schedulerMutex;
    SemaphoreHandle_t loadBalancerMutex;
    
    // Task handles
    TaskHandle_t taskMonitorTask;
    TaskHandle_t loadBalancerTask;
    
    // Internal methods
    void startSchedulerTasks();
    void stopAllScheduledTasks();
    
    // Load balancing strategies
    LoadBalanceStrategy determineLoadBalanceStrategy(float cpuLoad) const;
    void balanceForPerformance();
    void balanceForPower();
    void balanceForBalanced();
    void balanceAdaptive(float cpuLoad);
    
    // Performance analysis
    float measureCpuLoad();
    float estimateCpuLoadFromTasks();
    void updateCpuLoadHistory(float load);
    float calculateTaskEfficiency(const TaskInfo& task) const;
    float calculateExecutionTimeVariance(const TaskInfo& task) const;
    
    // Task management
    void restartTask(TaskInfo& task);
    void checkTaskHealth(TaskInfo& task, uint32_t currentTime);
    void recordPriorityChange(TaskInfo& task, UBaseType_t newPriority);
    
    // Synchronization helpers
    bool takeSchedulerMutex();
    bool takeLoadBalancerMutex();
    void giveSchedulerMutex();
    void giveLoadBalancerMutex();
    
    // Metrics updates
    void updateCpuMetrics();
    void updateMemoryMetrics();
    void updateHealthMetrics();
    
public:
    static TaskScheduler* instance;
    
    // Constructor/Destructor
    TaskScheduler();
    ~TaskScheduler();
    
    // Singleton access
    static TaskScheduler* getInstance() {
        if (!instance) {
            instance = new TaskScheduler();
        }
        return instance;
    }
    
    // Task registration
    bool registerTask(const TaskInfo& taskInfo);
    bool unregisterTask(TaskHandle_t handle);
    TaskInfo* getTaskInfo(TaskHandle_t handle);
    TaskInfo* getTaskInfo(const char* name);
    uint8_t getTaskCount() const { return taskCount; }
    
    // Priority management
    bool adjustTaskPriority(TaskHandle_t handle, UBaseType_t newPriority);
    bool boostTaskPriority(TaskHandle_t handle, uint8_t boost = PERFORMANCE_PRIORITY_BOOST);
    bool reduceTaskPriority(TaskHandle_t handle, uint8_t reduction = 2);
    void restoreBasePriorities();
    void optimizePriorities();
    
    // Load balancing
    void balanceCpuLoad();
    void setLoadBalanceStrategy(LoadBalanceStrategy strategy);
    LoadBalanceStrategy getCurrentStrategy() const { return performanceMetrics.current_strategy; }
    void enableAutoLoadBalancing(bool enable);
    
    // Performance monitoring
    void monitorTaskPerformance();
    void startTaskMonitoring();
    void stopTaskMonitoring();
    void getPerformanceMetrics(PerformanceMetrics& outMetrics) const { outMetrics = performanceMetrics; }
    
    // CPU monitoring
    float getCurrentCpuLoad() const { return currentCpuLoad; }
    float getAverageCpuLoad() const;
    float getMaxCpuLoad() const { return performanceMetrics.max_cpu_load; }
    void resetCpuLoadHistory();
    
    // Health monitoring
    bool isSystemHealthy() const { return performanceMetrics.system_is_healthy; }
    float getSystemHealthScore() const { return performanceMetrics.overall_health_score; }
    void performSystemHealthCheck();
    void generateHealthReport();
    
    // Task optimization
    void optimizeForPerformance();
    void optimizeForPower();
    void optimizeForMemory();
    void optimizeOverall();
    
    
    // Statistics
    uint32_t getTaskRestartCount() const { return performanceMetrics.task_restarts; }
    uint32_t getPriorityAdjustmentCount() const { return performanceMetrics.priority_adjustments; }
    LoadBalanceStrategy getLoadBalanceStrategy() const { return performanceMetrics.current_strategy; }
    
    // Advanced features
    bool enableTaskAffinity();
    bool disableTaskAffinity();
    void setTaskCoreAffinity(TaskHandle_t handle, uint32_t coreMask);
    void migrateTaskToCore(TaskHandle_t handle, uint32_t targetCore);
    
    // Performance metrics (public access for monitor task)
    PerformanceMetrics performanceMetrics;
    
    // Task monitoring (needed for monitor task)
    void updatePerformanceMetrics();
    void optimizeTaskAffinity();
    
    // Debug and diagnostics
    void printTaskRegistry() const;
    void printPerformanceMetrics() const;
    void printLoadBalanceStatus() const;
    void exportTaskStatistics(char* buffer, size_t bufferSize);
};

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================
inline const char* strategyToString(LoadBalanceStrategy strategy) {
    switch (strategy) {
        case LoadBalanceStrategy::AGGRESSIVE_PERFORMANCE: return "Aggressive Performance";
        case LoadBalanceStrategy::BALANCED: return "Balanced";
        case LoadBalanceStrategy::POWER_SAVING: return "Power Saving";
        case LoadBalanceStrategy::ADAPTIVE: return "Adaptive";
        default: return "Unknown";
    }
}

inline const char* taskStateToString(eTaskState state) {
    switch (state) {
        case eRunning: return "Running";
        case eReady: return "Ready";
        case eBlocked: return "Blocked";
        case eSuspended: return "Suspended";
        case eDeleted: return "Deleted";
        case eInvalid: return "Invalid";
        default: return "Unknown";
    }
}

inline bool isTaskHealthy(const TaskInfo& task) {
    return (task.health_score >= 70) && 
           (task.stack_peak >= task.min_stack_free) &&
           (task.cpu_percentage <= task.max_cpu_percentage) &&
           (!task.needs_restart);
}

// ============================================================================
// TASK FUNCTION DECLARATIONS
// ============================================================================
void vTaskSchedulerMonitor(void* pvParameters);
void vTaskLoadBalancer(void* pvParameters);

// ============================================================================
// CONVENIENCE MACROS
// ============================================================================
#define SCHEDULER_REGISTER_TASK(taskInfo) taskScheduler->registerTask(taskInfo)
#define SCHEDULER_ADJUST_PRIORITY(handle, priority) taskScheduler->adjustTaskPriority(handle, priority)
#define SCHEDULER_BOOST_PRIORITY(handle) taskScheduler->boostTaskPriority(handle)
#define SCHEDULER_REDUCE_PRIORITY(handle) taskScheduler->reduceTaskPriority(handle)
#define SCHEDULER_GET_CPU_LOAD() taskScheduler->getCurrentCpuLoad()
#define SCHEDULER_IS_HEALTHY() taskScheduler->isSystemHealthy()
#define SCHEDULER_PERFORMANCE_METRICS() taskScheduler->getPerformanceMetrics(performanceMetrics)

// ============================================================================
// EXTERNAL GLOBAL ACCESS
// ============================================================================
extern TaskScheduler* taskScheduler;

#endif // TASK_SCHEDULER_H
