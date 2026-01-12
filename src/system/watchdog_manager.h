/*
 * Watchdog Manager Header
 * Advanced fault tolerance and recovery system with independent watchdogs
 */

#ifndef WATCHDOG_MANAGER_H
#define WATCHDOG_MANAGER_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/timers.h>
#include <freertos/semphr.h>

// ============================================================================
// WATCHDOG CONFIGURATION
// ============================================================================
#define MAX_WATCHDOGS 16
#define WATCHDOG_CHECK_INTERVAL_MS 1000
#define HARDWARE_WATCHDOG_TIMEOUT_MS 30000
#define HEALTH_CRITICAL_THRESHOLD 30.0f
#define HEALTH_WARNING_THRESHOLD 60.0f
#define HEALTH_RECOVERY_THRESHOLD 80.0f

#define WATCHDOG_TASK_STACK_SIZE 4096
#define WATCHDOG_TASK_PRIORITY 2

// ============================================================================
// RECOVERY ACTIONS
// ============================================================================
#include "../../include/recovery_actions.h"

// ============================================================================
// DEGRADATION LEVELS
// ============================================================================
enum class DegradationLevel : uint8_t {
    NORMAL = 0,
    MINOR = 1,
    MODERATE = 2,
    SEVERE = 3,
    CRITICAL = 4
};

enum class DegradationReason : uint8_t {
    NONE = 0,
    LOW_MEMORY = 1,
    HIGH_CPU = 2,
    TASK_FAILURES = 3,
    NETWORK_ISSUES = 4,
    MULTIPLE_FACTORS = 5
};

// ============================================================================
// WATCHDOG INFORMATION
// ============================================================================
struct WatchdogInfo {
    char name[32];
    TaskHandle_t task_handle;
    UBaseType_t priority;
    uint32_t timeout_ms;
    TimerHandle_t timer_handle;
    
    // Callbacks
    void (*restart_callback)(void);
    void (*task_create_function)(void);
    
    // Runtime information
    uint32_t last_kick;
    uint32_t trigger_count;
    bool is_active;
    float health_score;
    
    // Configuration
    bool enable_auto_restart;
    uint32_t max_triggers;
    RecoveryAction recovery_action;
};

// ============================================================================
// SYSTEM HEALTH METRICS
// ============================================================================
struct SystemHealth {
    float overall_score;
    float watchdog_health;
    float memory_health;
    float cpu_health;
    bool degraded;
    
    uint32_t last_health_check;
    uint32_t uptime;
    uint32_t startup_time;
    
    // Component health
    uint8_t healthy_tasks;
    uint8_t total_tasks;
    uint8_t critical_failures;
    uint8_t warning_count;
};

// ============================================================================
// RECOVERY STATISTICS
// ============================================================================
struct RecoveryStatistics {
    uint32_t total_recoveries;
    uint32_t successful_recoveries;
    uint32_t failed_recoveries;
    float recovery_success_rate;
    
    uint32_t last_recovery;
    uint32_t last_recovery_time;
    
    // Recovery action counts
    uint32_t task_restarts;
    uint32_t module_restarts;
    uint32_t system_restarts;
    uint32_t degradations;
    uint32_t buffer_clears;
    uint32_t hardware_resets;
};

// ============================================================================
// DEGRADATION MANAGER
// ============================================================================
struct DegradationManager {
    bool active;
    DegradationLevel level;
    DegradationReason reason;
    uint32_t start_time;
    uint32_t duration;
    
    // Degradation actions
    bool tasks_paused;
    bool features_disabled;
    bool data_reduced;
    bool monitoring_increased;
};

// ============================================================================
// WATCHDOG MANAGER CLASS
// ============================================================================
class WatchdogManager {
private:
    // Watchdog registry
    WatchdogInfo watchdogRegistry[MAX_WATCHDOGS];
    uint8_t watchdogCount;
    
    // Recovery statistics
    RecoveryStatistics recoveryStats;
    
    // Hardware watchdog
    bool hardwareWatchdogEnabled;
    
    // Synchronization
    SemaphoreHandle_t watchdogMutex;
    SemaphoreHandle_t recoveryMutex;
    SemaphoreHandle_t healthMutex;
    
    // Task handle
    TaskHandle_t watchdogTask;
    
    // Internal methods
    bool initHardwareWatchdog();
    void disableHardwareWatchdog();
    
    void startWatchdogTask();
    void stopAllWatchdogs();
    
    // Recovery actions
    bool restartWatchdogTask(const char* watchdogName);
    bool restartModule(const char* moduleName);
    bool performSystemRestart();
    bool initiateSystemDegradation();
    bool clearSystemBuffers();
    bool resetHardwareComponent(const char* componentName);
    bool performSafeShutdown();
    
    // Health monitoring
    void handleWatchdogTimeout(WatchdogInfo& watchdog);
    RecoveryAction determineRecoveryAction(const WatchdogInfo& watchdog);
    
    // Degradation management
    DegradationLevel calculateDegradationLevel() const;
    DegradationReason determineDegradationReason() const;
    void applyMinorDegradation();
    void applyModerateDegradation();
    void applySevereDegradation();
    void applyCriticalDegradation();
    
    // Critical health handling
    void handleCriticalHealth();
    void handleWarningHealth();
    
    // Synchronization helpers
    bool takeWatchdogMutex();
    bool takeRecoveryMutex();
    bool takeHealthMutex();
    void giveWatchdogMutex();
    void giveRecoveryMutex();
    void giveHealthMutex();
    
    // Timer callback
    static void watchdogTimerCallback(TimerHandle_t xTimer);
    
public:
    static WatchdogManager* instance;
    
    // Constructor/Destructor
    WatchdogManager();
    ~WatchdogManager();
    
    // Singleton access
    static WatchdogManager* getInstance() {
        if (!instance) {
            instance = new WatchdogManager();
        }
        return instance;
    }
    
    // Watchdog management
    bool registerWatchdog(const WatchdogInfo& info);
    bool kickWatchdog(const char* name);
    bool unregisterWatchdog(const char* name);
    WatchdogInfo* getWatchdogInfo(const char* name);
    uint8_t getWatchdogCount() const { return watchdogCount; }
    
    // Recovery management
    bool performRecovery(const char* watchdogName, RecoveryAction action);
    void getRecoveryStatistics(RecoveryStatistics& outStats) const { outStats = recoveryStats; }
    void resetRecoveryStatistics();
    
    // Health monitoring
    void getSystemHealth(SystemHealth& outHealth) const { outHealth = systemHealth; }
    bool isSystemHealthy() const { return systemHealth.overall_score >= HEALTH_WARNING_THRESHOLD; }
    float getHealthScore() const { return systemHealth.overall_score; }
    
    // Degradation management
    void getDegradationStatus(DegradationManager& outManager) const { outManager = degradationManager; }
    bool isDegraded() const { return degradationManager.active; }
    DegradationLevel getDegradationLevel() const { return degradationManager.level; }
    
    // Advanced features
    void enableAutoRecovery(bool enable);
    void setDegradationThresholds(float warning, float critical);
    void configureWatchdogDefaults(uint32_t timeout, RecoveryAction action);
    
    // Health monitoring (public access for monitor task)
    void performHealthCheck();
    void kickHardwareWatchdog();
    
    // Degradation management (public access for monitor task)
    DegradationManager degradationManager;
    SystemHealth systemHealth;
    void liftSystemDegradation();
    
    // Diagnostics
    void printWatchdogRegistry() const;
    void printSystemHealth() const;
    void printRecoveryStatistics() const;
    void exportHealthData(char* buffer, size_t bufferSize);
};

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================
inline const char* recoveryActionToString(RecoveryAction action) {
    switch (action) {
        case RecoveryAction::RESTART_TASK: return "Restart Task";
        case RecoveryAction::RESTART_MODULE: return "Restart Module";
        case RecoveryAction::SYSTEM_RESTART: return "System Restart";
        case RecoveryAction::DEGRADE_SYSTEM: return "Degrade System";
        case RecoveryAction::CLEAR_BUFFERS: return "Clear Buffers";
        case RecoveryAction::RESET_HARDWARE: return "Reset Hardware";
        case RecoveryAction::SAFE_SHUTDOWN: return "Safe Shutdown";
        default: return "Unknown";
    }
}

inline const char* degradationLevelToString(DegradationLevel level) {
    switch (level) {
        case DegradationLevel::NORMAL: return "Normal";
        case DegradationLevel::MINOR: return "Minor";
        case DegradationLevel::MODERATE: return "Moderate";
        case DegradationLevel::SEVERE: return "Severe";
        case DegradationLevel::CRITICAL: return "Critical";
        default: return "Unknown";
    }
}

inline const char* degradationReasonToString(DegradationReason reason) {
    switch (reason) {
        case DegradationReason::NONE: return "None";
        case DegradationReason::LOW_MEMORY: return "Low Memory";
        case DegradationReason::HIGH_CPU: return "High CPU";
        case DegradationReason::TASK_FAILURES: return "Task Failures";
        case DegradationReason::NETWORK_ISSUES: return "Network Issues";
        case DegradationReason::MULTIPLE_FACTORS: return "Multiple Factors";
        default: return "Unknown";
    }
}

// ============================================================================
// TASK FUNCTION DECLARATIONS
// ============================================================================
void vWatchdogMonitorTask(void* pvParameters);

// ============================================================================
// CONVENIENCE MACROS
// ============================================================================
#define WATCHDOG_REGISTER(info) watchdogManager->registerWatchdog(info)
#define WATCHDOG_KICK(name) watchdogManager->kickWatchdog(name)
#define WATCHDOG_UNREGISTER(name) watchdogManager->unregisterWatchdog(name)
#define WATCHDOG_IS_HEALTHY() watchdogManager->isSystemHealthy()
#define WATCHDOG_HEALTH_SCORE() watchdogManager->getHealthScore()
#define WATCHDOG_SYSTEM_HEALTH() watchdogManager->getSystemHealth(systemHealth)

// ============================================================================
// EXTERNAL GLOBAL ACCESS
// ============================================================================
extern WatchdogManager* watchdogManager;

#endif // WATCHDOG_MANAGER_H
