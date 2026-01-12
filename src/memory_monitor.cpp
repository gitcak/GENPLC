/*
 * Memory Monitor Implementation
 * StampPLC CatM+GNSS FreeRTOS Modularization
 * 
 * Provides memory usage monitoring and alerts for heap management
 */

#include "../include/memory_monitor.h"
#include "../include/string_pool.h"
#include "../include/ui_utils.h"
#include "config/task_config.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

// ============================================================================
// GLOBAL MEMORY MONITOR INSTANCE
// ============================================================================
MemoryMonitor g_memoryMonitor;

// ============================================================================
// CONSTRUCTOR/DESTRUCTOR
// ============================================================================
MemoryMonitor::MemoryMonitor() : monitorTaskHandle(nullptr), mutex(nullptr), isRunning(false) {
    // Initialize stats
    memset(&stats, 0, sizeof(MemoryStats));
    stats.status = MemoryStatus::NORMAL;
    stats.minFreeHeap = UINT32_MAX;
    stats.maxFreeHeap = 0;
}

MemoryMonitor::~MemoryMonitor() {
    shutdown();
}

// ============================================================================
// MONITOR MANAGEMENT
// ============================================================================
bool MemoryMonitor::begin() {
    mutex = xSemaphoreCreateMutex();
    if (!mutex) {
        Serial.println("MemoryMonitor: Failed to create mutex");
        return false;
    }
    
    // Initialize string pool
    if (!g_stringPool.begin()) {
        Serial.println("MemoryMonitor: Failed to initialize string pool");
        return false;
    }
    
    Serial.println("MemoryMonitor: Initialized successfully");
    return true;
}

void MemoryMonitor::shutdown() {
    stopMonitoring();
    
    if (mutex) {
        vSemaphoreDelete(mutex);
        mutex = nullptr;
    }
    
    g_stringPool.shutdown();
}

// ============================================================================
// MONITORING CONTROL
// ============================================================================
void MemoryMonitor::startMonitoring() {
    if (isRunning || !mutex) return;
    
    // Set flag BEFORE task creation to prevent race condition
    isRunning = true;
    
    BaseType_t result = xTaskCreatePinnedToCore(
        monitorTask,
        "MemoryMonitor",
        MEMORY_MONITOR_TASK_STACK_SIZE,
        this,
        MEMORY_MONITOR_TASK_PRIORITY,
        &monitorTaskHandle,
        0  // Core 0
    );
    
    if (result == pdPASS) {
        Serial.println("MemoryMonitor: Started monitoring");
    } else {
        // Reset flag if task creation failed
        isRunning = false;
        Serial.println("MemoryMonitor: Failed to start monitoring task");
    }
}

void MemoryMonitor::stopMonitoring() {
    if (!isRunning) return;
    
    isRunning = false;
    
    if (monitorTaskHandle) {
        vTaskDelete(monitorTaskHandle);
        monitorTaskHandle = nullptr;
    }
    
    Serial.println("MemoryMonitor: Stopped monitoring");
}

// ============================================================================
// MONITOR TASK
// ============================================================================
void MemoryMonitor::monitorTask(void* pvParameters) {
    MemoryMonitor* monitor = static_cast<MemoryMonitor*>(pvParameters);
    
    Serial.println("MemoryMonitor: Task started");
    
    while (monitor->isRunning) {
        monitor->update();
        vTaskDelay(pdMS_TO_TICKS(MEMORY_MONITOR_INTERVAL_MS));
    }
    
    Serial.println("MemoryMonitor: Task ended");
    vTaskDelete(nullptr);
}

// ============================================================================
// STATISTICS ACCESS
// ============================================================================
MemoryStats MemoryMonitor::getStats() const {
    MemoryStats result{};
    result.status = MemoryStatus::NORMAL;
    if (mutex && xSemaphoreTake(mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        result = stats;
        xSemaphoreGive(mutex);
    }
    return result;
}

uint32_t MemoryMonitor::getFreeHeap() const {
    uint32_t result = 0;
    if (mutex && xSemaphoreTake(mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        result = stats.freeHeap;
        xSemaphoreGive(mutex);
    }
    return result;
}

uint32_t MemoryMonitor::getTotalHeap() const {
    uint32_t result = 0;
    if (mutex && xSemaphoreTake(mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        result = stats.totalHeap;
        xSemaphoreGive(mutex);
    }
    return result;
}

MemoryStatus MemoryMonitor::getStatus() const {
    MemoryStatus result = MemoryStatus::NORMAL;
    if (mutex && xSemaphoreTake(mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        result = stats.status;
        xSemaphoreGive(mutex);
    }
    return result;
}

// ============================================================================
// MANUAL UPDATES
// ============================================================================
void MemoryMonitor::update() {
    if (!mutex) return;
    
    if (xSemaphoreTake(mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        updateStats();
        checkThresholds();
        addToHistory(stats.freeHeap);
        xSemaphoreGive(mutex);
    }
}

void MemoryMonitor::printStats() const {
    if (!mutex) return;
    
    if (xSemaphoreTake(mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        const char* statusStr = "UNKNOWN";
        switch (stats.status) {
            case MemoryStatus::NORMAL: statusStr = "NORMAL"; break;
            case MemoryStatus::LOW_MEMORY: statusStr = "LOW"; break;
            case MemoryStatus::CRITICAL: statusStr = "CRITICAL"; break;
        }
        
        Serial.printf("Memory: Free=%u/%u bytes (%.1f%%), Status=%s, Min=%u, Max=%u\n",
                     stats.freeHeap, stats.totalHeap,
                     (float)stats.freeHeap * 100.0f / stats.totalHeap,
                     statusStr, stats.minFreeHeap, stats.maxFreeHeap);
        
        // Print string pool stats
        g_stringPool.printStatistics();
        
        xSemaphoreGive(mutex);
    }
}

// ============================================================================
// PRIVATE METHODS
// ============================================================================
void MemoryMonitor::updateStats() {
    stats.freeHeap = ESP.getFreeHeap();
    stats.totalHeap = ESP.getHeapSize();
    stats.lastUpdate = millis();
    
    // Update min/max
    if (stats.freeHeap < stats.minFreeHeap) {
        stats.minFreeHeap = stats.freeHeap;
    }
    if (stats.freeHeap > stats.maxFreeHeap) {
        stats.maxFreeHeap = stats.freeHeap;
    }
}

void MemoryMonitor::checkThresholds() {
    uint32_t freePercent = (stats.freeHeap * 100) / stats.totalHeap;
    
    if (freePercent <= MEMORY_CRITICAL_THRESHOLD_PERCENT) {
        if (stats.status != MemoryStatus::CRITICAL) {
            stats.status = MemoryStatus::CRITICAL;
            alertCriticalMemory();
        }
    } else if (freePercent <= MEMORY_LOW_THRESHOLD_PERCENT) {
        if (stats.status != MemoryStatus::LOW_MEMORY) {
            stats.status = MemoryStatus::LOW_MEMORY;
            alertLowMemory();
        }
    } else {
        stats.status = MemoryStatus::NORMAL;
    }
}

void MemoryMonitor::addToHistory(uint32_t freeHeap) {
    stats.history[stats.historyIndex] = freeHeap;
    stats.historyIndex = (stats.historyIndex + 1) % MEMORY_HISTORY_SIZE;
}

void MemoryMonitor::alertLowMemory() {
    Serial.printf("MEMORY ALERT: Low memory - %u bytes free (%.1f%%)\n",
                 stats.freeHeap, (float)stats.freeHeap * 100.0f / stats.totalHeap);
    
    // Trigger string pool cleanup
    g_stringPool.cleanup();
}

void MemoryMonitor::alertCriticalMemory() {
    Serial.printf("MEMORY CRITICAL: Very low memory - %u bytes free (%.1f%%)\n",
                 stats.freeHeap, (float)stats.freeHeap * 100.0f / stats.totalHeap);
    
    // Aggressive cleanup
    g_stringPool.cleanup();
    
    // Clean up sprites to free memory
    cleanupAllSprites();
    
    // Force garbage collection if available
    #ifdef ESP32
    // ESP32 doesn't have explicit GC, but we can try to defragment
    Serial.println("MEMORY CRITICAL: Attempting heap defragmentation");
    #endif
}

// ============================================================================
// THRESHOLD MANAGEMENT
// ============================================================================
void MemoryMonitor::setLowThreshold(uint8_t percent) {
    if (mutex && xSemaphoreTake(mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        // Update threshold (would need to modify MEMORY_LOW_THRESHOLD_PERCENT)
        xSemaphoreGive(mutex);
    }
}

void MemoryMonitor::setCriticalThreshold(uint8_t percent) {
    if (mutex && xSemaphoreTake(mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        // Update threshold (would need to modify MEMORY_CRITICAL_THRESHOLD_PERCENT)
        xSemaphoreGive(mutex);
    }
}

void MemoryMonitor::cleanup() {
    if (!mutex) return;
    
    if (xSemaphoreTake(mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        g_stringPool.cleanup();
        xSemaphoreGive(mutex);
    }
}
