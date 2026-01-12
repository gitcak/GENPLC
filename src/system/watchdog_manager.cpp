/*
 * Watchdog Manager Implementation
 * Advanced fault tolerance and recovery system with independent watchdogs
 */

#include "watchdog_manager.h"
#include "../include/memory_pool.h"
#include "../include/recovery_actions.h"
#include "../include/error_handler.h"
#include "../modules/logging/log_buffer.h"
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/timers.h>
#include <freertos/semphr.h>
#include <esp_task_wdt.h>

// ============================================================================
// GLOBAL INSTANCE
// ============================================================================
WatchdogManager* WatchdogManager::instance = nullptr;

// ============================================================================
// CONSTRUCTOR
// ============================================================================
WatchdogManager::WatchdogManager() {
    // Initialize watchdog registry
    memset(watchdogRegistry, 0, sizeof(watchdogRegistry));
    watchdogCount = 0;
    
    // Initialize system health metrics
    memset(&systemHealth, 0, sizeof(systemHealth));
    systemHealth.overall_score = 100.0f;
    systemHealth.last_health_check = millis();
    systemHealth.startup_time = millis();
    
    // Initialize recovery statistics
    memset(&recoveryStats, 0, sizeof(recoveryStats));
    recoveryStats.last_recovery = 0;
    
    // Initialize degradation manager
    degradationManager.active = false;
    degradationManager.level = DegradationLevel::NORMAL;
    degradationManager.reason = DegradationReason::NONE;
    degradationManager.start_time = 0;
    
    // Initialize synchronization
    watchdogMutex = xSemaphoreCreateMutex();
    recoveryMutex = xSemaphoreCreateMutex();
    healthMutex = xSemaphoreCreateMutex();
    
    // Initialize hardware watchdog
    if (!initHardwareWatchdog()) {
        Serial.println("WatchdogManager: Hardware watchdog initialization failed");
    }
    
    // Start watchdog monitoring task
    startWatchdogTask();
    
    Serial.println("WatchdogManager: Advanced fault tolerance system initialized");
}

// ============================================================================
// DESTRUCTOR
// ============================================================================
WatchdogManager::~WatchdogManager() {
    stopAllWatchdogs();
    
    if (watchdogMutex) {
        vSemaphoreDelete(watchdogMutex);
    }
    
    if (recoveryMutex) {
        vSemaphoreDelete(recoveryMutex);
    }
    
    if (healthMutex) {
        vSemaphoreDelete(healthMutex);
    }
    
    // Disable hardware watchdog
    disableHardwareWatchdog();
}

// ============================================================================
// WATCHDOG MANAGEMENT
// ============================================================================
bool WatchdogManager::registerWatchdog(const WatchdogInfo& info) {
    if (!takeWatchdogMutex()) return false;
    
    if (watchdogCount >= MAX_WATCHDOGS) {
        giveWatchdogMutex();
        REPORT_ERROR(ERROR_SYSTEM_FAULT, ErrorSeverity::ERROR, ErrorCategory::SYSTEM,
                      "Watchdog registry full");
        return false;
    }
    
    // Check for duplicate names
    for (uint8_t i = 0; i < watchdogCount; i++) {
        if (strcmp(watchdogRegistry[i].name, info.name) == 0) {
            giveWatchdogMutex();
            return false; // Already registered
        }
    }
    
    // Add new watchdog
    WatchdogInfo& watchdog = watchdogRegistry[watchdogCount];
    watchdog = info;
    watchdog.last_kick = millis();
    watchdog.trigger_count = 0;
    watchdog.is_active = true;
    watchdog.health_score = 100.0f;
    
    // Create software watchdog timer
    if (info.timeout_ms > 0) {
        watchdog.timer_handle = xTimerCreate(
            info.name,
            pdMS_TO_TICKS(info.timeout_ms),
            pdFALSE, // One-shot
            this,
            watchdogTimerCallback
        );
        
        if (!watchdog.timer_handle) {
            giveWatchdogMutex();
            REPORT_ERROR(ERROR_SYSTEM_FAULT, ErrorSeverity::ERROR, ErrorCategory::SYSTEM,
                          "Failed to create watchdog timer");
            return false;
        }
    }
    
    watchdogCount++;
    
    giveWatchdogMutex();
    
    logbuf_printf("WatchdogManager: Registered watchdog '%s' (timeout: %ums, priority: %u)",
                 info.name, info.timeout_ms, info.priority);
    
    return true;
}

bool WatchdogManager::kickWatchdog(const char* name) {
    if (!takeWatchdogMutex()) return false;
    
    bool found = false;
    for (uint8_t i = 0; i < watchdogCount; i++) {
        WatchdogInfo& watchdog = watchdogRegistry[i];
        
        if (strcmp(watchdog.name, name) == 0 && watchdog.is_active) {
            // Reset timer
            if (watchdog.timer_handle) {
                xTimerChangePeriod(watchdog.timer_handle, pdMS_TO_TICKS(watchdog.timeout_ms), 0);
                xTimerStart(watchdog.timer_handle, 0);
            }
            
            watchdog.last_kick = millis();
            watchdog.health_score = min(100.0f, watchdog.health_score + 5.0f);
            found = true;
            break;
        }
    }
    
    giveWatchdogMutex();
    
    if (!found) {
        REPORT_ERROR(ERROR_SYSTEM_FAULT, ErrorSeverity::WARNING, ErrorCategory::SYSTEM,
                      "Attempted to kick unknown watchdog");
    }
    
    return found;
}

bool WatchdogManager::unregisterWatchdog(const char* name) {
    if (!takeWatchdogMutex()) return false;
    
    bool found = false;
    for (uint8_t i = 0; i < watchdogCount; i++) {
        WatchdogInfo& watchdog = watchdogRegistry[i];
        
        if (strcmp(watchdog.name, name) == 0) {
            // Stop and delete timer
            if (watchdog.timer_handle) {
                xTimerStop(watchdog.timer_handle, 0);
                xTimerDelete(watchdog.timer_handle, 0);
            }
            
            watchdog.is_active = false;
            
            // Shift remaining watchdogs
            for (uint8_t j = i; j < watchdogCount - 1; j++) {
                watchdogRegistry[j] = watchdogRegistry[j + 1];
            }
            watchdogCount--;
            found = true;
            break;
        }
    }
    
    giveWatchdogMutex();
    
    if (found) {
        logbuf_printf("WatchdogManager: Unregistered watchdog '%s'", name);
    }
    
    return found;
}

// ============================================================================
// RECOVERY ACTIONS
// ============================================================================
bool WatchdogManager::performRecovery(const char* watchdogName, RecoveryAction action) {
    if (!takeRecoveryMutex()) return false;
    
    bool success = false;
    uint32_t startTime = millis();
    
    logbuf_printf("WatchdogManager: Performing recovery action %d for watchdog '%s'",
                 (int)action, watchdogName);
    
    switch (action) {
        case RecoveryAction::RESTART_TASK:
            success = restartWatchdogTask(watchdogName);
            break;
            
        case RecoveryAction::RESTART_MODULE:
            success = restartModule(watchdogName);
            break;
            
        case RecoveryAction::SYSTEM_RESTART:
            success = performSystemRestart();
            break;
            
        case RecoveryAction::DEGRADE_SYSTEM:
            success = initiateSystemDegradation();
            break;
            
        case RecoveryAction::CLEAR_BUFFERS:
            success = clearSystemBuffers();
            break;
            
        case RecoveryAction::RESET_HARDWARE:
            success = resetHardwareComponent(watchdogName);
            break;
            
        case RecoveryAction::SAFE_SHUTDOWN:
            success = performSafeShutdown();
            break;
            
        default:
            REPORT_ERROR(ERROR_SYSTEM_FAULT, ErrorSeverity::ERROR, ErrorCategory::SYSTEM,
                          "Unknown recovery action");
            break;
    }
    
    uint32_t recoveryTime = millis() - startTime;
    
    // Update recovery statistics
    recoveryStats.total_recoveries++;
    recoveryStats.last_recovery = startTime;
    recoveryStats.last_recovery_time = recoveryTime;
    recoveryStats.recovery_success_rate = 
        (float)(recoveryStats.total_recoveries - recoveryStats.failed_recoveries) / 
        recoveryStats.total_recoveries * 100.0f;
    
    if (!success) {
        recoveryStats.failed_recoveries++;
        REPORT_ERROR(ERROR_SYSTEM_FAULT, ErrorSeverity::ERROR, ErrorCategory::SYSTEM,
                      "Recovery action failed");
    }
    
    giveRecoveryMutex();
    
    logbuf_printf("Recovery completed: success=%s, time=%ums",
                 success ? "YES" : "NO", recoveryTime);
    
    return success;
}

bool WatchdogManager::restartWatchdogTask(const char* watchdogName) {
    // Find the watchdog and associated task
    for (uint8_t i = 0; i < watchdogCount; i++) {
        WatchdogInfo& watchdog = watchdogRegistry[i];
        
        if (strcmp(watchdog.name, watchdogName) == 0 && watchdog.task_handle) {
            // Attempt graceful task restart
            logbuf_printf("Restarting task associated with watchdog '%s'", watchdogName);
            
            // Notify task to restart gracefully
            if (watchdog.restart_callback) {
                watchdog.restart_callback();
            }
            
            // Wait a moment for graceful shutdown
            vTaskDelay(pdMS_TO_TICKS(100));
            
            // Check if task is still running
            eTaskState state = eTaskGetState(watchdog.task_handle);
            if (state != eDeleted && state != eInvalid) {
                // Force task deletion
                vTaskDelete(watchdog.task_handle);
                
                // Recreate task if creation function provided
                if (watchdog.task_create_function) {
                    watchdog.task_create_function();
                }
            }
            
            watchdog.trigger_count++;
            return true;
        }
    }
    
    return false;
}

bool WatchdogManager::initiateSystemDegradation() {
    if (degradationManager.active) {
        return true; // Already degraded
    }
    
    logbuf_printf("WatchdogManager: Initiating system degradation");
    
    // Determine degradation level based on current health
    DegradationLevel level = calculateDegradationLevel();
    DegradationReason reason = determineDegradationReason();
    
    degradationManager.active = true;
    degradationManager.level = level;
    degradationManager.reason = reason;
    degradationManager.start_time = millis();
    
    // Apply degradation measures
    switch (level) {
        case DegradationLevel::MINOR:
            applyMinorDegradation();
            break;
            
        case DegradationLevel::MODERATE:
            applyModerateDegradation();
            break;
            
        case DegradationLevel::SEVERE:
            applySevereDegradation();
            break;
            
        case DegradationLevel::CRITICAL:
            applyCriticalDegradation();
            break;
    }
    
    systemHealth.degraded = true;
    
    logbuf_printf("System degraded to level %d, reason: %d", (int)level, (int)reason);
    
    return true;
}

// ============================================================================
// HEALTH MONITORING
// ============================================================================
void WatchdogManager::performHealthCheck() {
    if (!takeHealthMutex()) return;
    
    uint32_t currentTime = millis();
    systemHealth.last_health_check = currentTime;
    
    // Check individual watchdogs
    uint8_t healthyWatchdogs = 0;
    uint8_t totalWatchdogs = 0;
    
    for (uint8_t i = 0; i < watchdogCount; i++) {
        WatchdogInfo& watchdog = watchdogRegistry[i];
        
        if (watchdog.is_active) {
            totalWatchdogs++;
            
            // Check timeout
            uint32_t timeSinceKick = currentTime - watchdog.last_kick;
            
            if (timeSinceKick > watchdog.timeout_ms) {
                // Watchdog timeout
                handleWatchdogTimeout(watchdog);
                watchdog.health_score = max(0.0f, watchdog.health_score - 20.0f);
            } else {
                // Gradually improve health score
                watchdog.health_score = min(100.0f, watchdog.health_score + 1.0f);
                healthyWatchdogs++;
            }
        }
    }
    
    // Update overall system health
    float watchdogHealth = totalWatchdogs > 0 ? 
        (float)healthyWatchdogs / totalWatchdogs * 100.0f : 100.0f;
    
    // Calculate memory health
    uint32_t freeHeap = ESP.getFreeHeap();
    uint32_t totalHeap = ESP.getHeapSize();
    float memoryHealth = (float)freeHeap / totalHeap * 100.0f;
    
    // Calculate CPU health (would integrate with task scheduler)
    float cpuHealth = 85.0f; // Placeholder
    
    // Combine health metrics
    systemHealth.watchdog_health = watchdogHealth;
    systemHealth.memory_health = memoryHealth;
    systemHealth.cpu_health = cpuHealth;
    systemHealth.overall_score = (watchdogHealth * 0.4f) + 
                               (memoryHealth * 0.3f) + 
                               (cpuHealth * 0.3f);
    
    systemHealth.uptime = currentTime - systemHealth.startup_time;
    
    // Check if system health requires intervention
    if (systemHealth.overall_score < HEALTH_CRITICAL_THRESHOLD) {
        handleCriticalHealth();
    } else if (systemHealth.overall_score < HEALTH_WARNING_THRESHOLD) {
        handleWarningHealth();
    }
    
    giveHealthMutex();
}

void WatchdogManager::handleWatchdogTimeout(WatchdogInfo& watchdog) {
    watchdog.trigger_count++;
    
    logbuf_printf("WatchdogManager: Watchdog '%s' timeout (count: %u)",
                 watchdog.name, watchdog.trigger_count);
    
    // Determine recovery action based on priority and trigger count
    RecoveryAction action = determineRecoveryAction(watchdog);
    
    // Perform recovery
    performRecovery(watchdog.name, action);
    
    // Report error
    REPORT_ERROR_CONTEXT(ERROR_SYSTEM_FAULT, ErrorSeverity::ERROR, ErrorCategory::SYSTEM,
                       "Watchdog timeout detected", watchdog.name);
}

RecoveryAction WatchdogManager::determineRecoveryAction(const WatchdogInfo& watchdog) {
    // Based on priority and trigger count
    if (watchdog.priority <= 2 && watchdog.trigger_count >= 3) {
        return RecoveryAction::SYSTEM_RESTART;
    } else if (watchdog.priority <= 3 && watchdog.trigger_count >= 5) {
        return RecoveryAction::DEGRADE_SYSTEM;
    } else if (watchdog.trigger_count >= 2) {
        return RecoveryAction::RESTART_MODULE;
    } else if (watchdog.trigger_count >= 1) {
        return RecoveryAction::RESTART_TASK;
    }
    
    return RecoveryAction::RESTART_TASK; // Default action
}

// ============================================================================
// DEGRADATION MANAGEMENT
// ============================================================================
DegradationLevel WatchdogManager::calculateDegradationLevel() const {
    if (systemHealth.overall_score >= 80.0f) {
        return DegradationLevel::NORMAL;
    } else if (systemHealth.overall_score >= 60.0f) {
        return DegradationLevel::MINOR;
    } else if (systemHealth.overall_score >= 40.0f) {
        return DegradationLevel::MODERATE;
    } else if (systemHealth.overall_score >= 20.0f) {
        return DegradationLevel::SEVERE;
    } else {
        return DegradationLevel::CRITICAL;
    }
}

DegradationReason WatchdogManager::determineDegradationReason() const {
    // Analyze primary cause of poor health
    if (systemHealth.memory_health < 50.0f) {
        return DegradationReason::LOW_MEMORY;
    } else if (systemHealth.watchdog_health < 50.0f) {
        return DegradationReason::TASK_FAILURES;
    } else if (systemHealth.cpu_health < 50.0f) {
        return DegradationReason::HIGH_CPU;
    } else {
        return DegradationReason::MULTIPLE_FACTORS;
    }
}

void WatchdogManager::applyMinorDegradation() {
    logbuf_printf("Applying minor system degradation");
    
    // Reduce non-critical task priorities
    // Disable optional features
    // Increase monitoring frequency
}

void WatchdogManager::applyModerateDegradation() {
    logbuf_printf("Applying moderate system degradation");
    
    // Pause non-essential tasks
    // Reduce data collection rates
    // Clear non-critical buffers
}

void WatchdogManager::applySevereDegradation() {
    logbuf_printf("Applying severe system degradation");
    
    // Stop all non-critical tasks
    // Maintain only essential functions
    // Maximize memory recovery
}

void WatchdogManager::applyCriticalDegradation() {
    logbuf_printf("Applying critical system degradation");
    
    // Enter safe mode
    // Minimal operation only
    // Prepare for possible restart
}

// ============================================================================
// HARDWARE WATCHDOG
// ============================================================================
bool WatchdogManager::initHardwareWatchdog() {
    // Configure ESP32 hardware watchdog
    esp_err_t err = esp_task_wdt_init(
        HARDWARE_WATCHDOG_TIMEOUT_MS,
        true  // Panic on timeout
    );
    
    if (err == ESP_OK) {
        err = esp_task_wdt_add(nullptr); // Add current task
        hardwareWatchdogEnabled = true;
        return true;
    }
    
    return false;
}

void WatchdogManager::kickHardwareWatchdog() {
    if (hardwareWatchdogEnabled) {
        esp_task_wdt_reset();
    }
}

void WatchdogManager::disableHardwareWatchdog() {
    if (hardwareWatchdogEnabled) {
        esp_task_wdt_delete(nullptr);
        esp_task_wdt_deinit();
        hardwareWatchdogEnabled = false;
    }
}

// ============================================================================
// TASK MANAGEMENT
// ============================================================================
void WatchdogManager::startWatchdogTask() {
    xTaskCreate(
        vWatchdogMonitorTask,
        "WatchdogMonitor",
        WATCHDOG_TASK_STACK_SIZE,
        nullptr,
        WATCHDOG_TASK_PRIORITY,
        &watchdogTask
    );
}

void WatchdogManager::stopAllWatchdogs() {
    if (watchdogTask) {
        vTaskDelete(watchdogTask);
        watchdogTask = nullptr;
    }
    
    // Stop all watchdog timers
    for (uint8_t i = 0; i < watchdogCount; i++) {
        WatchdogInfo& watchdog = watchdogRegistry[i];
        
        if (watchdog.timer_handle) {
            xTimerStop(watchdog.timer_handle, 0);
            xTimerDelete(watchdog.timer_handle, 0);
            watchdog.timer_handle = nullptr;
        }
        watchdog.is_active = false;
    }
}

// ============================================================================
// TIMER CALLBACK
// ============================================================================
void WatchdogManager::watchdogTimerCallback(TimerHandle_t xTimer) {
    WatchdogManager* manager = static_cast<WatchdogManager*>(pvTimerGetTimerID(xTimer));
    
    if (manager) {
        // Find the watchdog associated with this timer
        const char* timerName = pcTimerGetName(xTimer);
        for (uint8_t i = 0; i < manager->watchdogCount; i++) {
            if (strcmp(manager->watchdogRegistry[i].name, timerName) == 0) {
                manager->handleWatchdogTimeout(manager->watchdogRegistry[i]);
                break;
            }
        }
    }
}

// ============================================================================
// SYNCHRONIZATION HELPERS
// ============================================================================
bool WatchdogManager::takeWatchdogMutex() {
    return (watchdogMutex && 
            xSemaphoreTake(watchdogMutex, pdMS_TO_TICKS(100)) == pdTRUE);
}

bool WatchdogManager::takeRecoveryMutex() {
    return (recoveryMutex && 
            xSemaphoreTake(recoveryMutex, pdMS_TO_TICKS(1000)) == pdTRUE);
}

bool WatchdogManager::takeHealthMutex() {
    return (healthMutex && 
            xSemaphoreTake(healthMutex, pdMS_TO_TICKS(100)) == pdTRUE);
}

void WatchdogManager::giveWatchdogMutex() {
    if (watchdogMutex) {
        xSemaphoreGive(watchdogMutex);
    }
}

void WatchdogManager::giveRecoveryMutex() {
    if (recoveryMutex) {
        xSemaphoreGive(recoveryMutex);
    }
}

void WatchdogManager::giveHealthMutex() {
    if (healthMutex) {
        xSemaphoreGive(healthMutex);
    }
}

// ============================================================================
// WATCHDOG MONITOR TASK
// ============================================================================
void vWatchdogMonitorTask(void* pvParameters) {
    WatchdogManager* manager = WatchdogManager::getInstance();
    
    while (true) {
        // Perform health check
        manager->performHealthCheck();
        
        // Kick hardware watchdog
        manager->kickHardwareWatchdog();
        
        // Update degradation status
        if (manager->degradationManager.active) {
            // Check if degradation can be lifted
            if (manager->systemHealth.overall_score > HEALTH_RECOVERY_THRESHOLD) {
                manager->liftSystemDegradation();
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(WATCHDOG_CHECK_INTERVAL_MS));
    }
}
