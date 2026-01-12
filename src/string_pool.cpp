/*
 * String Pool Implementation
 * StampPLC CatM+GNSS FreeRTOS Modularization
 * 
 * Provides string pooling for frequently used strings to reduce heap fragmentation
 */

#include "../include/string_pool.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <string.h>

// ============================================================================
// COMMON STRING CONSTANTS
// ============================================================================
const char* STR_OK = "OK";
const char* STR_ERROR = "ERROR";
const char* STR_TIMEOUT = "TIMEOUT";
const char* STR_CONNECTED = "CONNECTED";
const char* STR_DISCONNECTED = "DISCONNECTED";
const char* STR_INITIALIZING = "INITIALIZING";
const char* STR_READY = "READY";
const char* STR_FAILED = "FAILED";
const char* STR_SUCCESS = "SUCCESS";

// ============================================================================
// GLOBAL STRING POOL INSTANCE
// ============================================================================
StringPool g_stringPool;

// ============================================================================
// CONSTRUCTOR/DESTRUCTOR
// ============================================================================
StringPool::StringPool() : mutex(nullptr), totalAllocations(0), totalHits(0) {
    // Initialize pool entries
    for (int i = 0; i < STRING_POOL_SIZE; i++) {
        pool[i].string[0] = '\0';
        pool[i].inUse = false;
        pool[i].lastUsed = 0;
    }
}

StringPool::~StringPool() {
    shutdown();
}

// ============================================================================
// POOL MANAGEMENT
// ============================================================================
bool StringPool::begin() {
    mutex = xSemaphoreCreateMutex();
    if (!mutex) {
        Serial.println("StringPool: Failed to create mutex");
        return false;
    }
    
    Serial.println("StringPool: Initialized successfully");
    return true;
}

void StringPool::shutdown() {
    if (mutex) {
        vSemaphoreDelete(mutex);
        mutex = nullptr;
    }
}

// ============================================================================
// STRING OPERATIONS
// ============================================================================
const char* StringPool::getString(const char* str) {
    if (!str || !mutex) return str;
    
    if (xSemaphoreTake(mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return str; // Fallback to original string
    }
    
    // Check if string already exists in pool
    int existingIndex = findExistingString(str);
    if (existingIndex >= 0) {
        pool[existingIndex].lastUsed = millis();
        totalHits++;
        xSemaphoreGive(mutex);
        return pool[existingIndex].string;
    }
    
    // Find available slot
    int slotIndex = findAvailableSlot();
    if (slotIndex < 0) {
        // Pool is full, cleanup and try again
        cleanupUnusedStrings();
        slotIndex = findAvailableSlot();
    }
    
    if (slotIndex >= 0) {
        // Copy string to pool
        strncpy(pool[slotIndex].string, str, MAX_STRING_LENGTH - 1);
        pool[slotIndex].string[MAX_STRING_LENGTH - 1] = '\0';
        pool[slotIndex].inUse = true;
        pool[slotIndex].lastUsed = millis();
        totalAllocations++;
        
        xSemaphoreGive(mutex);
        return pool[slotIndex].string;
    }
    
    xSemaphoreGive(mutex);
    return str; // Fallback to original string
}

void StringPool::releaseString(const char* str) {
    if (!str || !mutex) return;
    
    if (xSemaphoreTake(mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return;
    }
    
    // Find and mark string as unused
    for (int i = 0; i < STRING_POOL_SIZE; i++) {
        if (pool[i].inUse && strcmp(pool[i].string, str) == 0) {
            pool[i].inUse = false;
            break;
        }
    }
    
    xSemaphoreGive(mutex);
}

// ============================================================================
// PRIVATE METHODS
// ============================================================================
int StringPool::findAvailableSlot() {
    for (int i = 0; i < STRING_POOL_SIZE; i++) {
        if (!pool[i].inUse) {
            return i;
        }
    }
    return -1;
}

int StringPool::findExistingString(const char* str) {
    for (int i = 0; i < STRING_POOL_SIZE; i++) {
        if (pool[i].inUse && strcmp(pool[i].string, str) == 0) {
            return i;
        }
    }
    return -1;
}

void StringPool::cleanupUnusedStrings() {
    uint32_t currentTime = millis();
    const uint32_t CLEANUP_THRESHOLD = 30000; // 30 seconds
    
    for (int i = 0; i < STRING_POOL_SIZE; i++) {
        if (pool[i].inUse && (currentTime - pool[i].lastUsed) > CLEANUP_THRESHOLD) {
            pool[i].inUse = false;
            pool[i].string[0] = '\0';
        }
    }
}

// ============================================================================
// STATISTICS
// ============================================================================
uint32_t StringPool::getUsedSlots() const {
    uint32_t count = 0;
    for (int i = 0; i < STRING_POOL_SIZE; i++) {
        if (pool[i].inUse) count++;
    }
    return count;
}

void StringPool::printStatistics() const {
    if (!mutex) return;
    
    if (xSemaphoreTake(mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        Serial.printf("StringPool Stats: Allocations=%u, Hits=%u, Used=%u/%d\n",
                     totalAllocations, totalHits, getUsedSlots(), STRING_POOL_SIZE);
        xSemaphoreGive(mutex);
    }
}

void StringPool::cleanup() {
    if (!mutex) return;
    
    if (xSemaphoreTake(mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        cleanupUnusedStrings();
        xSemaphoreGive(mutex);
    }
}
