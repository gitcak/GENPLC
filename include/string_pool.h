/*
 * String Pool Header
 * StampPLC CatM+GNSS FreeRTOS Modularization
 * 
 * Provides string pooling for frequently used strings to reduce heap fragmentation
 * 
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2025 M5Stack Technology CO LTD
 */

#ifndef STRING_POOL_H
#define STRING_POOL_H

#include <Arduino.h>

// ============================================================================
// STRING POOL CONFIGURATION
// ============================================================================
#define STRING_POOL_SIZE 32
#define MAX_STRING_LENGTH 64

// ============================================================================
// STRING POOL ENTRY
// ============================================================================
struct StringPoolEntry {
    char string[MAX_STRING_LENGTH];
    bool inUse;
    uint32_t lastUsed;
};

// ============================================================================
// STRING POOL CLASS
// ============================================================================
class StringPool {
private:
    StringPoolEntry pool[STRING_POOL_SIZE];
    SemaphoreHandle_t mutex;
    uint32_t totalAllocations;
    uint32_t totalHits;
    
    // Private methods
    int findAvailableSlot();
    int findExistingString(const char* str);
    void cleanupUnusedStrings();

public:
    StringPool();
    ~StringPool();
    
    // Pool management
    bool begin();
    void shutdown();
    
    // String operations
    const char* getString(const char* str);
    void releaseString(const char* str);
    
    // Statistics
    uint32_t getTotalAllocations() const { return totalAllocations; }
    uint32_t getTotalHits() const { return totalHits; }
    uint32_t getUsedSlots() const;
    void printStatistics() const;
    
    // Cleanup
    void cleanup();
};

// ============================================================================
// GLOBAL STRING POOL INSTANCE
// ============================================================================
extern StringPool g_stringPool;

// ============================================================================
// CONVENIENCE MACROS
// ============================================================================
#define POOL_STRING(str) g_stringPool.getString(str)
#define RELEASE_STRING(str) g_stringPool.releaseString(str)

// ============================================================================
// COMMON STRING CONSTANTS
// ============================================================================
extern const char* STR_OK;
extern const char* STR_ERROR;
extern const char* STR_TIMEOUT;
extern const char* STR_CONNECTED;
extern const char* STR_DISCONNECTED;
extern const char* STR_INITIALIZING;
extern const char* STR_READY;
extern const char* STR_FAILED;
extern const char* STR_SUCCESS;

#endif // STRING_POOL_H
