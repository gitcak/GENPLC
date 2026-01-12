/*
 * Crash Recovery Header
 * StampPLC CatM+GNSS System
 * 
 * Provides crash detection, recovery, and system stability mechanisms
 * 
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2025 M5Stack Technology CO LTD
 */

#ifndef CRASH_RECOVERY_H
#define CRASH_RECOVERY_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/timers.h>
#include <freertos/semphr.h>
#include <esp_system.h>
#include <esp_heap_trace.h>

// ============================================================================
// CRASH RECOVERY CONFIGURATION
// ============================================================================
#define CRASH_RECOVERY_CHECK_INTERVAL_MS 5000    // 5 seconds
#define CRASH_RECOVERY_TASK_STACK_SIZE 4096      // 16KB stack
#define HARDWARE_WATCHDOG_TIMEOUT_MS 60000       // 60 seconds
#define HEAP_TRACE_RECORD_COUNT 100               // Number of heap trace records

// Heap thresholds are defined in system_config.h - use those values

// ============================================================================
// RECOVERY MODES
// ============================================================================
enum class RecoveryMode : uint8_t {
    NORMAL = 0,
    WATCHDOG_RECOVERY = 1,
    CRASH_RECOVERY = 2,
    SAFE_MODE = 3,
    EMERGENCY = 4
};

// ============================================================================
// CRASH STATISTICS
// ============================================================================
struct CrashStats {
    uint32_t totalResets;
    uint32_t memCorruptionCount;
    uint32_t stackOverflowCount;
    uint32_t taskFaultCount;
    uint32_t lastCrashTime;
    esp_reset_reason_t lastResetReason;
    RecoveryMode recoveryMode;
    bool systemStable;
};

// ============================================================================
// CRASH RECOVERY CLASS
// ============================================================================
class CrashRecovery {
private:
    // Task management
    TaskHandle_t recoveryTaskHandle;
    SemaphoreHandle_t mutex;
    bool isRunning;
    
    // Statistics
    CrashStats stats;
    
    // Watchdog state
    bool hardwareWatchdogEnabled;
    uint32_t lastWatchdogKick;
    TimerHandle_t watchdogTimer;  // FreeRTOS software watchdog timer
    
    // Bootloop detection
    static constexpr const char* BOOTLOOP_COUNTER_KEY = "bootloop_cnt";
    static constexpr const char* LAST_RESTART_TIME_KEY = "last_restart";
    static constexpr uint32_t BOOTLOOP_THRESHOLD = 3;  // 3 restarts in 60 seconds = bootloop
    static constexpr uint32_t BOOTLOOP_WINDOW_MS = 60000;  // 60 second window
    
    // Memory protection
    bool heapTraceEnabled;
    bool memoryCorruptionDetected;
    
    // Private methods
    static void recoveryTask(void* pvParameters);
    
    // Initialization
    bool initializeCrashDetection();
    bool initializeHardwareWatchdog();
    void analyzeResetReason();
    
    // Hardware watchdog
    void kickHardwareWatchdog();
    
    // Heap tracing
    esp_err_t enableHeapTracing();
    void disableHeapTracing();
    
    // System health checks
    void performSystemHealthCheck();
    void checkCriticalTaskStacks();
    void monitorTaskHealth();
    
    // Memory corruption detection
    void checkMemoryCorruption();
    void checkMemoryPatterns();
    
    // Recovery strategies
    void applyRecoveryStrategies();
    void applyWatchdogRecovery();
    void applyCrashRecovery();
    void applySafeMode();
    void applyEmergencyRecovery();
    
    // Recovery actions
    void triggerEmergencyMemoryRecovery();
    void triggerMemoryCleanup();
    void disableNonEssentialFeatures();
    void handleCriticalTaskFailure(const char* taskName);
    
    // System stability
    void updateSystemStability();
    
    // Utility methods
    const char* getResetReasonString(esp_reset_reason_t reason) const;

public:
    static CrashRecovery* instance;
    
    // Constructor/Destructor
    CrashRecovery();
    ~CrashRecovery();
    
    // Singleton access
    static CrashRecovery* getInstance() {
        if (!instance) {
            instance = new CrashRecovery();
        }
        return instance;
    }
    
    // Recovery management
    bool begin();
    void shutdown();
    void startRecovery();
    void stopRecovery();
    
    // Statistics access
    CrashStats getStats() const;
    void printStats() const;
    
    // System status
    bool isSystemStable() const { return stats.systemStable; }
    RecoveryMode getRecoveryMode() const { return stats.recoveryMode; }
    bool isInRecoveryMode() const { return stats.recoveryMode != RecoveryMode::NORMAL; }
    
    // Manual recovery triggers
    void triggerMemoryRecovery() { triggerEmergencyMemoryRecovery(); }
    void triggerSystemRestart() { esp_restart(); }
    
    // Bootloop detection (public for watchdog callback)
    bool detectBootloop();
    void recordRestart();
    void clearBootloopCounter();
    bool shouldPreventRestart();
    void disableHardwareWatchdog();  // Public for watchdog callback
};

// ============================================================================
// GLOBAL CRASH RECOVERY INSTANCE
// ============================================================================
extern CrashRecovery* g_crashRecovery;

// ============================================================================
// CONVENIENCE MACROS
// ============================================================================
#define CRASH_RECOVERY_BEGIN() g_crashRecovery->begin()
#define CRASH_RECOVERY_START() g_crashRecovery->startRecovery()
#define CRASH_RECOVERY_STOP() g_crashRecovery->stopRecovery()
#define CRASH_RECOVERY_STATS() g_crashRecovery->printStats()
#define CRASH_RECOVERY_IS_STABLE() g_crashRecovery->isSystemStable()
#define CRASH_RECOVERY_TRIGGER_MEMORY() g_crashRecovery->triggerMemoryRecovery()

// ============================================================================
// CRASH RECOVERY TASK CONFIGURATION
// ============================================================================
#ifndef CRASH_RECOVERY_TASK_PRIORITY
#define CRASH_RECOVERY_TASK_PRIORITY TASK_PRIORITY_SYSTEM_MONITOR
#endif

#endif // CRASH_RECOVERY_H
