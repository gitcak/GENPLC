/*
 * Memory Monitor Header
 * StampPLC CatM+GNSS FreeRTOS Modularization
 * 
 * Provides memory usage monitoring and alerts for heap management
 * 
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2025 M5Stack Technology CO LTD
 */

#ifndef MEMORY_MONITOR_H
#define MEMORY_MONITOR_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// ============================================================================
// MEMORY MONITOR CONFIGURATION
// ============================================================================
#define MEMORY_MONITOR_INTERVAL_MS 5000
#define MEMORY_LOW_THRESHOLD_PERCENT 20
#define MEMORY_CRITICAL_THRESHOLD_PERCENT 10
#define MEMORY_HISTORY_SIZE 10

// ============================================================================
// MEMORY STATUS LEVELS
// ============================================================================
enum class MemoryStatus {
    NORMAL,
    LOW_MEMORY,
    CRITICAL
};
// ============================================================================
struct MemoryStats {
    uint32_t freeHeap;
    uint32_t totalHeap;
    uint32_t minFreeHeap;
    uint32_t maxFreeHeap;
    uint32_t allocCount;
    uint32_t freeCount;
    MemoryStatus status;
    uint32_t lastUpdate;
    uint32_t history[MEMORY_HISTORY_SIZE];
    uint8_t historyIndex;
};

// ============================================================================
// MEMORY MONITOR CLASS
// ============================================================================
class MemoryMonitor {
private:
    MemoryStats stats;
    TaskHandle_t monitorTaskHandle;
    SemaphoreHandle_t mutex;
    bool isRunning;
    
    // Private methods
    static void monitorTask(void* pvParameters);
    void updateStats();
    void checkThresholds();
    void addToHistory(uint32_t freeHeap);
    void alertLowMemory();
    void alertCriticalMemory();

public:
    MemoryMonitor();
    ~MemoryMonitor();
    
    // Monitor management
    bool begin();
    void shutdown();
    
    // Statistics access
    MemoryStats getStats() const;
    uint32_t getFreeHeap() const;
    uint32_t getTotalHeap() const;
    MemoryStatus getStatus() const;
    
    // Monitoring control
    void startMonitoring();
    void stopMonitoring();
    bool isMonitoring() const { return isRunning; }
    
    // Manual updates
    void update();
    void printStats() const;
    
    // Threshold management
    void setLowThreshold(uint8_t percent);
    void setCriticalThreshold(uint8_t percent);
    
    // Cleanup
    void cleanup();
};

// ============================================================================
// GLOBAL MEMORY MONITOR INSTANCE
// ============================================================================
extern MemoryMonitor g_memoryMonitor;

// ============================================================================
// CONVENIENCE MACROS
// ============================================================================
#define MEMORY_UPDATE() g_memoryMonitor.update()
#define MEMORY_PRINT_STATS() g_memoryMonitor.printStats()
#define MEMORY_GET_FREE() g_memoryMonitor.getFreeHeap()
#define MEMORY_GET_STATUS() g_memoryMonitor.getStatus()

// ============================================================================
// MEMORY MONITORING TASK PRIORITY
// ============================================================================
#define MEMORY_MONITOR_TASK_PRIORITY TASK_PRIORITY_SYSTEM_MONITOR
#define MEMORY_MONITOR_TASK_STACK_SIZE 2048

#endif // MEMORY_MONITOR_H
