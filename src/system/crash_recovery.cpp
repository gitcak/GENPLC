/*
 * Crash Recovery Implementation
 * StampPLC CatM+GNSS System
 * 
 * Provides crash detection, recovery, and system stability mechanisms
 */

#include "crash_recovery.h"
#include "../include/error_handler.h"
#include "../include/memory_monitor.h"
#include "../include/string_pool.h"
#include "../modules/logging/log_buffer.h"
#include "config/task_config.h"
#include "config/system_config.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/timers.h>
#include <freertos/semphr.h>
#include <esp_system.h>
#include <esp_heap_trace.h>
#include <Preferences.h>

// ============================================================================
// GLOBAL CRASH RECOVERY INSTANCE
// ============================================================================
CrashRecovery* CrashRecovery::instance = nullptr;

// ============================================================================
// CONSTRUCTOR/DESTRUCTOR
// ============================================================================
CrashRecovery::CrashRecovery() : recoveryTaskHandle(nullptr), mutex(nullptr), isRunning(false) {
    // Initialize crash statistics
    memset(&stats, 0, sizeof(CrashStats));
    stats.lastCrashTime = 0;
    stats.totalResets = 0;
    stats.memCorruptionCount = 0;
    stats.stackOverflowCount = 0;
    stats.taskFaultCount = 0;
    stats.systemStable = false;
    stats.recoveryMode = RecoveryMode::NORMAL;
    
    // Initialize watchdog state
    hardwareWatchdogEnabled = false;
    lastWatchdogKick = 0;
    watchdogTimer = nullptr;
    
    // Initialize memory protection
    heapTraceEnabled = false;
    memoryCorruptionDetected = false;
}

CrashRecovery::~CrashRecovery() {
    shutdown();
}

// ============================================================================
// RECOVERY MANAGEMENT
// ============================================================================
bool CrashRecovery::begin() {
    Serial.println("CrashRecovery: Initializing crash recovery system");
    
    mutex = xSemaphoreCreateMutex();
    if (!mutex) {
        Serial.println("CrashRecovery: Failed to create mutex");
        return false;
    }
    
    // Initialize crash detection
    if (!initializeCrashDetection()) {
        Serial.println("CrashRecovery: Failed to initialize crash detection");
        return false;
    }
    
    // Initialize hardware watchdog
    if (!initializeHardwareWatchdog()) {
        Serial.println("CrashRecovery: Hardware watchdog initialization failed");
    }
    
    Serial.println("CrashRecovery: Crash recovery system initialized");
    return true;
}

void CrashRecovery::shutdown() {
    stopRecovery();
    
    if (heapTraceEnabled) {
        disableHeapTracing();
    }
    
    if (hardwareWatchdogEnabled) {
        disableHardwareWatchdog();
    }
    
    if (mutex) {
        vSemaphoreDelete(mutex);
        mutex = nullptr;
    }
    
    Serial.println("CrashRecovery: Crash recovery system shutdown");
}

// ============================================================================
// RECOVERY CONTROL
// ============================================================================
void CrashRecovery::startRecovery() {
    if (isRunning || !mutex) return;
    
    Serial.println("CrashRecovery: Starting crash recovery monitoring");
    
    isRunning = true;
    
    BaseType_t result = xTaskCreatePinnedToCore(
        recoveryTask,
        "CrashRecovery",
        CRASH_RECOVERY_TASK_STACK_SIZE,
        this,
        CRASH_RECOVERY_TASK_PRIORITY,
        &recoveryTaskHandle,
        0  // Core 0 for system monitoring
    );
    
    if (result == pdPASS) {
        Serial.println("CrashRecovery: Recovery monitoring task started");
    } else {
        isRunning = false;
        Serial.println("CrashRecovery: Failed to start recovery task");
    }
}

void CrashRecovery::stopRecovery() {
    if (!isRunning) return;
    
    isRunning = false;
    
    if (recoveryTaskHandle) {
        vTaskDelete(recoveryTaskHandle);
        recoveryTaskHandle = nullptr;
    }
    
    Serial.println("CrashRecovery: Recovery monitoring stopped");
}

// ============================================================================
// CRASH DETECTION INITIALIZATION
// ============================================================================
bool CrashRecovery::initializeCrashDetection() {
    // Note: Tick hook registration removed - not available in ESP32 Arduino framework
    // The recovery monitoring task will handle periodic health checks instead
    
    // Initialize heap tracing for memory corruption detection
    #ifdef CONFIG_HEAP_TRACING
    esp_err_t err = enableHeapTracing();
    if (err != ESP_OK) {
        Serial.printf("CrashRecovery: Heap tracing not available: %s\n", esp_err_to_name(err));
    }
    #else
    // Heap tracing not available - continue without it
    #endif
    
    // Check for previous crash (reset reason analysis)
    analyzeResetReason();
    
    // Check for bootloop condition
    if (detectBootloop()) {
        Serial.println("CrashRecovery: BOOTLOOP DETECTED - Entering safe mode");
        stats.recoveryMode = RecoveryMode::EMERGENCY;
        
        // Don't start watchdog if we're in a bootloop
        Serial.println("CrashRecovery: Watchdog disabled due to bootloop");
        return true;  // Still return true so system can continue in safe mode
    } else {
        // Clear bootloop counter if we've been stable
        clearBootloopCounter();
    }
    
    Serial.println("CrashRecovery: Crash detection initialized");
    return true;
}

// ============================================================================
// SOFTWARE WATCHDOG (FreeRTOS Timer-based)
// ============================================================================
// Forward declaration
class CrashRecovery;

// Watchdog timer callback - triggers if not kicked in time
static void watchdogTimerCallback(TimerHandle_t xTimer) {
    CrashRecovery* recovery = static_cast<CrashRecovery*>(pvTimerGetTimerID(xTimer));
    if (recovery) {
        Serial.println("CrashRecovery: Watchdog timeout - system may be hung!");
        
        // Check for bootloop before restarting
        if (recovery->shouldPreventRestart()) {
            Serial.println("CrashRecovery: BOOTLOOP DETECTED - Preventing restart to avoid infinite loop!");
            Serial.println("CrashRecovery: System will remain in safe mode. Manual power cycle required.");
            
            // Disable watchdog to prevent further restarts
            recovery->disableHardwareWatchdog();
            
            // Enter emergency safe mode - disable problematic tasks
            // This allows the system to continue running in a degraded state
            // rather than rebooting infinitely
            while (1) {
                Serial.println("CrashRecovery: System in bootloop recovery mode - waiting for manual intervention");
                vTaskDelay(pdMS_TO_TICKS(10000));  // Wait 10 seconds before next message
            }
        } else {
            // Record restart attempt
            recovery->recordRestart();
            
            // Add delay before restart to allow serial output
            Serial.println("CrashRecovery: Restarting in 2 seconds...");
            vTaskDelay(pdMS_TO_TICKS(2000));
            
            ESP.restart();  // Emergency restart if watchdog times out
        }
    }
}

bool CrashRecovery::initializeHardwareWatchdog() {
    // Use FreeRTOS software watchdog timer instead of ESP-IDF hardware watchdog
    // This is PlatformIO/Arduino compatible and doesn't require ESP-IDF APIs
    
    watchdogTimer = xTimerCreate(
        "CrashRecoveryWDT",                    // Timer name
        pdMS_TO_TICKS(HARDWARE_WATCHDOG_TIMEOUT_MS),  // Period in ticks
        pdTRUE,                                // Auto-reload
        this,                                   // Timer ID (pointer to this instance)
        watchdogTimerCallback                  // Callback function
    );
    
    if (watchdogTimer == nullptr) {
        Serial.println("CrashRecovery: Failed to create watchdog timer");
        return false;
    }
    
    // Start the watchdog timer
    if (xTimerStart(watchdogTimer, 0) != pdPASS) {
        Serial.println("CrashRecovery: Failed to start watchdog timer");
        xTimerDelete(watchdogTimer, 0);
        watchdogTimer = nullptr;
        return false;
    }
    
    hardwareWatchdogEnabled = true;
    lastWatchdogKick = millis();
    
    Serial.println("CrashRecovery: Software watchdog initialized (FreeRTOS timer-based)");
    return true;
}

void CrashRecovery::disableHardwareWatchdog() {
    if (hardwareWatchdogEnabled && watchdogTimer) {
        xTimerStop(watchdogTimer, 0);
        xTimerDelete(watchdogTimer, portMAX_DELAY);
        watchdogTimer = nullptr;
        hardwareWatchdogEnabled = false;
        Serial.println("CrashRecovery: Software watchdog disabled");
    }
}

void CrashRecovery::kickHardwareWatchdog() {
    if (hardwareWatchdogEnabled && watchdogTimer) {
        // Reset the timer by changing its period (forces restart)
        xTimerReset(watchdogTimer, 0);
        lastWatchdogKick = millis();
    }
}

// ============================================================================
// HEAP TRACING
// ============================================================================
esp_err_t CrashRecovery::enableHeapTracing() {
    #ifdef CONFIG_HEAP_TRACING
    static heap_trace_record_t trace_record[HEAP_TRACE_RECORD_COUNT];
    
    heap_trace_config_t config;
    config.calloc_enable = false;
    config.sample_mode = HEAP_TRACE_LEAKS;
    config.num_records = HEAP_TRACE_RECORD_COUNT;
    config.sampling_rate = 1;
    
    esp_err_t err = heap_trace_init_standalone(trace_record, config.num_records);
    if (err != ESP_OK) {
        return err;
    }
    
    err = heap_trace_start(HEAP_TRACE_LEAKS);
    if (err != ESP_OK) {
        return err;
    }
    
    heapTraceEnabled = true;
    Serial.println("CrashRecovery: Heap tracing enabled");
    return ESP_OK;
    #else
    return ESP_ERR_NOT_SUPPORTED;
    #endif
}

void CrashRecovery::disableHeapTracing() {
    #ifdef CONFIG_HEAP_TRACING
    if (heapTraceEnabled) {
        heap_trace_stop();
        heap_trace_dump();
        heapTraceEnabled = false;
        Serial.println("CrashRecovery: Heap tracing disabled");
    }
    #endif
}

// ============================================================================
// RESET REASON ANALYSIS
// ============================================================================
void CrashRecovery::analyzeResetReason() {
    esp_reset_reason_t resetReason = esp_reset_reason();
    
    Serial.printf("CrashRecovery: Reset reason: %s\n", getResetReasonString(resetReason));
    
    switch (resetReason) {
        case ESP_RST_UNKNOWN:
        case ESP_RST_POWERON:
        case ESP_RST_SW:
            // Normal resets - no crash recovery needed
            stats.recoveryMode = RecoveryMode::NORMAL;
            break;
            
        case ESP_RST_INT_WDT:
        case ESP_RST_TASK_WDT:
        case ESP_RST_WDT:
            stats.totalResets++;
            stats.recoveryMode = RecoveryMode::WATCHDOG_RECOVERY;
            Serial.println("CrashRecovery: Watchdog reset detected - entering recovery mode");
            break;
            
        case ESP_RST_PANIC:
            stats.totalResets++;
            stats.recoveryMode = RecoveryMode::CRASH_RECOVERY;
            Serial.println("CrashRecovery: System panic detected - entering crash recovery mode");
            break;
            
        case ESP_RST_EXT:
        case ESP_RST_BROWNOUT:
        case ESP_RST_SDIO:
            stats.totalResets++;
            stats.recoveryMode = RecoveryMode::SAFE_MODE;
            Serial.println("CrashRecovery: Hardware reset detected - entering safe mode");
            break;
            
        default:
            stats.totalResets++;
            stats.recoveryMode = RecoveryMode::SAFE_MODE;
            Serial.printf("CrashRecovery: Unknown reset reason - entering safe mode: %d\n", resetReason);
            break;
    }
    
    // Store reset info
    stats.lastResetReason = resetReason;
    stats.lastCrashTime = millis();
}

const char* CrashRecovery::getResetReasonString(esp_reset_reason_t reason) const {
    switch (reason) {
        case ESP_RST_POWERON: return "Power on";
        case ESP_RST_EXT: return "External pin";
        case ESP_RST_SW: return "Software";
        case ESP_RST_PANIC: return "Exception/panic";
        case ESP_RST_INT_WDT: return "Interrupt watchdog";
        case ESP_RST_TASK_WDT: return "Task watchdog";
        case ESP_RST_WDT: return "Other watchdog";
        case ESP_RST_DEEPSLEEP: return "Deep sleep";
        case ESP_RST_BROWNOUT: return "Brownout";
        case ESP_RST_SDIO: return "SDIO";
        default: {
            // Handle ESP32-S3 specific reset reasons that may not be in enum
            // These are checked numerically to avoid compilation errors
            uint32_t reason_val = static_cast<uint32_t>(reason);
            if (reason_val == 11) return "USB";        // ESP_RST_USB (if exists)
            if (reason_val == 12) return "JTAG";       // ESP_RST_JTAG (if exists)
            if (reason_val == 13) return "EFUSE";      // ESP_RST_EFUSE (if exists)
            if (reason_val == 14) return "Power glitch"; // ESP_RST_PWR_GLITCH (if exists)
            if (reason_val == 15) return "CPU lockup"; // ESP_RST_CPU_LOCKUP (if exists)
            return "Unknown";
        }
    }
}

// ============================================================================
// RECOVERY TASK
// ============================================================================
void CrashRecovery::recoveryTask(void* pvParameters) {
    CrashRecovery* recovery = static_cast<CrashRecovery*>(pvParameters);
    
    Serial.println("CrashRecovery: Recovery monitoring task started");
    
    // Initial system assessment
    recovery->performSystemHealthCheck();
    
    while (recovery->isRunning) {
        // Kick hardware watchdog
        recovery->kickHardwareWatchdog();
        
        // Perform periodic health checks
        recovery->performSystemHealthCheck();
        
        // Check for memory corruption
        recovery->checkMemoryCorruption();
        
        // Monitor task health
        recovery->monitorTaskHealth();
        
        // Apply recovery strategies if needed
        recovery->applyRecoveryStrategies();
        
        // Update system stability
        recovery->updateSystemStability();
        
        vTaskDelay(pdMS_TO_TICKS(CRASH_RECOVERY_CHECK_INTERVAL_MS));
    }
    
    Serial.println("CrashRecovery: Recovery monitoring task ended");
    vTaskDelete(nullptr);
}

// ============================================================================
// SYSTEM HEALTH CHECKS
// ============================================================================
void CrashRecovery::performSystemHealthCheck() {
    if (!mutex) return;
    
    if (xSemaphoreTake(mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return;
    }
    
    // Check heap health
    uint32_t freeHeap = ESP.getFreeHeap();
    uint32_t minFreeHeap = ESP.getMinFreeHeap();
    
    if (freeHeap < CRITICAL_HEAP_THRESHOLD) {
        stats.memCorruptionCount++;
        Serial.printf("CrashRecovery: Critical heap low: %u bytes\n", freeHeap);
        
        // Trigger emergency memory recovery
        triggerEmergencyMemoryRecovery();
    } else if (freeHeap < MIN_FREE_HEAP_THRESHOLD) {
        Serial.printf("CrashRecovery: Low heap: %u bytes\n", freeHeap);
        
        // Trigger memory cleanup
        triggerMemoryCleanup();
    }
    
    // Check for heap fragmentation
    if (minFreeHeap < freeHeap * 0.5) {
        Serial.printf("CrashRecovery: Heap fragmentation detected: min=%u, current=%u\n", 
                     minFreeHeap, freeHeap);
    }
    
    // Check stack health for critical tasks
    checkCriticalTaskStacks();
    
    xSemaphoreGive(mutex);
}

void CrashRecovery::checkCriticalTaskStacks() {
    // List of critical tasks to monitor
    const char* criticalTasks[] = {
        "CrashRecovery",
        "MemoryMonitor", 
        "StatusBar",
        "Display",
        "CatMGNSS"
    };
    
    for (const char* taskName : criticalTasks) {
        TaskHandle_t taskHandle = xTaskGetHandle(taskName);
        if (taskHandle) {
            UBaseType_t highWaterMark = uxTaskGetStackHighWaterMark(taskHandle);
            uint32_t stackSize = 0;
            
            // Check stack health using high water mark
            // vTaskGetInfo is not available in all FreeRTOS configs, so we use a simpler approach
            eTaskState state = eTaskGetState(taskHandle);
            if (state != eDeleted && state != eInvalid) {
                // Use high water mark as indicator - low values mean high usage
                // Threshold: less than 512 bytes remaining = critical
                // Threshold: less than 1024 bytes remaining = warning
                uint32_t remainingBytes = highWaterMark * sizeof(StackType_t);
                
                if (remainingBytes < 512) {
                    stats.stackOverflowCount++;
                    Serial.printf("CrashRecovery: Critical stack overflow in task '%s': %u bytes remaining\n", 
                                 taskName, remainingBytes);
                    
                    // Report stack overflow
                    REPORT_ERROR(ERROR_MEMORY_FAULT, ErrorSeverity::CRITICAL, ErrorCategory::MEMORY,
                                "Critical task stack overflow detected");
                } else if (remainingBytes < 1024) {
                    Serial.printf("CrashRecovery: High stack usage in task '%s': %u bytes remaining\n", 
                                 taskName, remainingBytes);
                }
            }
        }
    }
}

void CrashRecovery::monitorTaskHealth() {
    // Check if critical tasks are still running
    const char* criticalTasks[] = {
        "CrashRecovery",
        "MemoryMonitor",
        "StatusBar"
    };
    
    uint32_t currentTime = millis();
    
    for (const char* taskName : criticalTasks) {
        TaskHandle_t taskHandle = xTaskGetHandle(taskName);
        if (!taskHandle) {
            stats.taskFaultCount++;
            Serial.printf("CrashRecovery: Critical task '%s' not found\n", taskName);
            
            // Try to restart critical task if possible
            handleCriticalTaskFailure(taskName);
        } else {
            eTaskState state = eTaskGetState(taskHandle);
            if (state == eDeleted || state == eInvalid) {
                stats.taskFaultCount++;
                Serial.printf("CrashRecovery: Critical task '%s' in invalid state: %d\n", taskName, state);
                
                handleCriticalTaskFailure(taskName);
            }
        }
    }
}

// ============================================================================
// MEMORY CORRUPTION DETECTION
// ============================================================================
void CrashRecovery::checkMemoryCorruption() {
    if (!heapTraceEnabled) return;
    
    #ifdef CONFIG_HEAP_TRACING
    // Check for memory leaks and corruption
    size_t totalLeaks = heap_trace_get_count();
    if (totalLeaks > 0) {
        stats.memCorruptionCount++;
        Serial.printf("CrashRecovery: Memory corruption detected: %zu leaks\n", totalLeaks);
        
        // Dump heap trace for debugging
        heap_trace_dump();
        
        // Trigger memory recovery
        triggerEmergencyMemoryRecovery();
    }
    #endif
    
    // Check for obvious corruption patterns
    checkMemoryPatterns();
}

void CrashRecovery::checkMemoryPatterns() {
    // Check for common corruption patterns
    uint32_t freeHeap = ESP.getFreeHeap();
    
    // Sudden large memory changes indicate corruption
    static uint32_t lastFreeHeap = 0;
    if (lastFreeHeap > 0) {
        int32_t heapChange = (int32_t)freeHeap - (int32_t)lastFreeHeap;
        
        // If heap suddenly increased by more than 10KB, possible corruption
        if (heapChange > 10240) {
            Serial.printf("CrashRecovery: Suspicious heap increase: %d bytes\n", heapChange);
            stats.memCorruptionCount++;
            memoryCorruptionDetected = true;
        }
        // If heap suddenly decreased by more than 20KB, possible memory leak
        else if (heapChange < -20480) {
            Serial.printf("CrashRecovery: Suspicious heap decrease: %d bytes\n", heapChange);
            stats.memCorruptionCount++;
        }
    }
    
    lastFreeHeap = freeHeap;
}

// ============================================================================
// RECOVERY STRATEGIES
// ============================================================================
void CrashRecovery::applyRecoveryStrategies() {
    if (!mutex) return;
    
    if (xSemaphoreTake(mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return;
    }
    
    switch (stats.recoveryMode) {
        case RecoveryMode::NORMAL:
            // Normal operation - just monitor
            break;
            
        case RecoveryMode::WATCHDOG_RECOVERY:
            applyWatchdogRecovery();
            break;
            
        case RecoveryMode::CRASH_RECOVERY:
            applyCrashRecovery();
            break;
            
        case RecoveryMode::SAFE_MODE:
            applySafeMode();
            break;
            
        case RecoveryMode::EMERGENCY:
            applyEmergencyRecovery();
            break;
    }
    
    xSemaphoreGive(mutex);
}

void CrashRecovery::applyWatchdogRecovery() {
    static uint32_t recoveryStartTime = 0;
    if (recoveryStartTime == 0) {
        recoveryStartTime = millis();
        Serial.println("CrashRecovery: Applying watchdog recovery strategy");
    }
    
    // Give system time to stabilize
    if (millis() - recoveryStartTime > 30000) { // 30 seconds
        Serial.println("CrashRecovery: Watchdog recovery completed - returning to normal");
        stats.recoveryMode = RecoveryMode::NORMAL;
        recoveryStartTime = 0;
    }
}

void CrashRecovery::applyCrashRecovery() {
    static uint32_t recoveryStartTime = 0;
    if (recoveryStartTime == 0) {
        recoveryStartTime = millis();
        Serial.println("CrashRecovery: Applying crash recovery strategy");
        
        // Perform aggressive cleanup
        triggerEmergencyMemoryRecovery();
    }
    
    // Monitor for 60 seconds before returning to normal
    if (millis() - recoveryStartTime > 60000) {
        if (stats.systemStable) {
            Serial.println("CrashRecovery: Crash recovery completed - returning to normal");
            stats.recoveryMode = RecoveryMode::NORMAL;
            recoveryStartTime = 0;
        }
    }
}

void CrashRecovery::applySafeMode() {
    static bool safeModeInitialized = false;
    if (!safeModeInitialized) {
        safeModeInitialized = true;
        Serial.println("CrashRecovery: Entering safe mode");
        
        // Disable non-essential features
        disableNonEssentialFeatures();
        
        // Reduce memory usage
        triggerMemoryCleanup();
    }
}

void CrashRecovery::applyEmergencyRecovery() {
    Serial.println("CrashRecovery: Applying emergency recovery");
    
    // Most aggressive recovery
    triggerEmergencyMemoryRecovery();
    disableNonEssentialFeatures();
    
    // Consider system restart if recovery fails
    static uint32_t emergencyStartTime = 0;
    if (emergencyStartTime == 0) {
        emergencyStartTime = millis();
    }
    
    if (millis() - emergencyStartTime > 30000) { // 30 seconds
        if (!stats.systemStable) {
            Serial.println("CrashRecovery: Emergency recovery failed - initiating system restart");
            vTaskDelay(pdMS_TO_TICKS(1000));
            esp_restart();
        }
    }
}

// ============================================================================
// RECOVERY ACTIONS
// ============================================================================
void CrashRecovery::triggerEmergencyMemoryRecovery() {
    Serial.println("CrashRecovery: Triggering emergency memory recovery");
    
    // Force garbage collection
    g_memoryMonitor.cleanup();
    
    // Clean up string pool
    g_stringPool.cleanup();
    
    // Clean up UI sprites
    extern void cleanupAllSprites();
    cleanupAllSprites();
    
    // Report memory recovery
    uint32_t freeHeap = ESP.getFreeHeap();
    Serial.printf("CrashRecovery: Emergency recovery completed - free heap: %u bytes\n", freeHeap);
    
    logbuf_printf("Emergency memory recovery - free heap: %u bytes", freeHeap);
}

void CrashRecovery::triggerMemoryCleanup() {
    Serial.println("CrashRecovery: Triggering memory cleanup");
    
    // Standard cleanup
    g_memoryMonitor.cleanup();
    g_stringPool.cleanup();
    
    uint32_t freeHeap = ESP.getFreeHeap();
    Serial.printf("CrashRecovery: Memory cleanup completed - free heap: %u bytes\n", freeHeap);
}

void CrashRecovery::disableNonEssentialFeatures() {
    Serial.println("CrashRecovery: Disabling non-essential features");
    
    // Web server functionality removed - not required for this project
    
    // Reduce display refresh rate
    extern void reduceDisplayRefreshRate();
    reduceDisplayRefreshRate();
    
    // Disable optional logging
    extern void reduceLoggingLevel();
    reduceLoggingLevel();
}

void CrashRecovery::handleCriticalTaskFailure(const char* taskName) {
    Serial.printf("CrashRecovery: Handling critical task failure: %s\n", taskName);
    
    // For now, just log the failure
    // In a full implementation, we would attempt to restart critical tasks
    logbuf_printf("Critical task failure detected: %s", taskName);
    
    REPORT_ERROR(ERROR_SYSTEM_FAULT, ErrorSeverity::CRITICAL, ErrorCategory::SYSTEM,
                "Critical task failure detected");
}

// ============================================================================
// SYSTEM STABILITY ASSESSMENT
// ============================================================================
void CrashRecovery::updateSystemStability() {
    static uint32_t lastStabilityCheck = 0;
    if (millis() - lastStabilityCheck < 5000) return; // Check every 5 seconds
    
    lastStabilityCheck = millis();
    
    uint32_t currentTime = millis();
    bool currentlyStable = true;
    
    // Check heap stability
    uint32_t freeHeap = ESP.getFreeHeap();
    if (freeHeap < MIN_FREE_HEAP_THRESHOLD) {
        currentlyStable = false;
    }
    
    // Check for recent crashes
    if (currentTime - stats.lastCrashTime < 60000) { // Recent crash within 1 minute
        currentlyStable = false;
    }
    
    // Check memory corruption status
    if (memoryCorruptionDetected) {
        currentlyStable = false;
        memoryCorruptionDetected = false; // Reset flag
    }
    
    // Update system stability
    stats.systemStable = currentlyStable;
    
    // Log stability changes
    static bool lastStable = false;
    if (currentlyStable != lastStable) {
        Serial.printf("CrashRecovery: System stability changed: %s\n", 
                     currentlyStable ? "STABLE" : "UNSTABLE");
        lastStable = currentlyStable;
    }
}

// ============================================================================
// STATISTICS AND DIAGNOSTICS
// ============================================================================
CrashStats CrashRecovery::getStats() const {
    CrashStats result{};
    if (mutex && xSemaphoreTake(mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        result = stats;
        xSemaphoreGive(mutex);
    }
    return result;
}

void CrashRecovery::printStats() const {
    if (!mutex) return;
    
    if (xSemaphoreTake(mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        Serial.println("CrashRecovery: Failed to acquire mutex for stats");
        return;
    }
    
    Serial.println("\n=== Crash Recovery Statistics ===");
    Serial.printf("Total Resets: %u\n", stats.totalResets);
    Serial.printf("Memory Corruption Events: %u\n", stats.memCorruptionCount);
    Serial.printf("Stack Overflow Events: %u\n", stats.stackOverflowCount);
    Serial.printf("Task Fault Events: %u\n", stats.taskFaultCount);
    Serial.printf("Last Reset Reason: %s\n", getResetReasonString(stats.lastResetReason));
    Serial.printf("Recovery Mode: %d\n", (int)stats.recoveryMode);
    Serial.printf("System Stable: %s\n", stats.systemStable ? "YES" : "NO");
    Serial.printf("Free Heap: %u bytes\n", ESP.getFreeHeap());
    Serial.println("===============================\n");
    
    xSemaphoreGive(mutex);
}

// ============================================================================
// BOOTLOOP DETECTION
// ============================================================================
bool CrashRecovery::detectBootloop() {
    Preferences prefs;
    if (!prefs.begin("crash_rec", true)) {  // Read-only
        return false;
    }
    
    uint32_t bootloopCount = prefs.getUInt(BOOTLOOP_COUNTER_KEY, 0);
    uint32_t lastRestartTime = prefs.getUInt(LAST_RESTART_TIME_KEY, 0);
    prefs.end();
    
    uint32_t currentTime = millis();
    
    // If last restart was more than BOOTLOOP_WINDOW_MS ago, reset counter
    if (lastRestartTime > 0 && (currentTime - lastRestartTime) > BOOTLOOP_WINDOW_MS) {
        clearBootloopCounter();
        return false;
    }
    
    // Check if we've exceeded threshold
    if (bootloopCount >= BOOTLOOP_THRESHOLD) {
        Serial.printf("CrashRecovery: Bootloop detected! Count: %u (threshold: %u)\n", 
                     bootloopCount, BOOTLOOP_THRESHOLD);
        return true;
    }
    
    return false;
}

void CrashRecovery::recordRestart() {
    Preferences prefs;
    if (!prefs.begin("crash_rec", false)) {  // Read-write
        return;
    }
    
    uint32_t bootloopCount = prefs.getUInt(BOOTLOOP_COUNTER_KEY, 0);
    uint32_t lastRestartTime = prefs.getUInt(LAST_RESTART_TIME_KEY, 0);
    uint32_t currentTime = millis();
    
    // If last restart was more than BOOTLOOP_WINDOW_MS ago, reset counter
    if (lastRestartTime > 0 && (currentTime - lastRestartTime) > BOOTLOOP_WINDOW_MS) {
        bootloopCount = 0;
    }
    
    bootloopCount++;
    prefs.putUInt(BOOTLOOP_COUNTER_KEY, bootloopCount);
    prefs.putUInt(LAST_RESTART_TIME_KEY, currentTime);
    prefs.end();
    
    Serial.printf("CrashRecovery: Recorded restart #%u\n", bootloopCount);
}

void CrashRecovery::clearBootloopCounter() {
    Preferences prefs;
    if (!prefs.begin("crash_rec", false)) {  // Read-write
        return;
    }
    
    prefs.remove(BOOTLOOP_COUNTER_KEY);
    prefs.remove(LAST_RESTART_TIME_KEY);
    prefs.end();
    
    Serial.println("CrashRecovery: Bootloop counter cleared");
}

bool CrashRecovery::shouldPreventRestart() {
    return detectBootloop();
}

// ============================================================================
// EXTERNAL GLOBAL INSTANCE
// ============================================================================
CrashRecovery* g_crashRecovery = nullptr;
