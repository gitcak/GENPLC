#include "memory_pool.h"
#include "config/task_config.h"
#include "../modules/logging/log_buffer.h"

// ============================================================================
// MEMORY POOL SINGLETON IMPLEMENTATION
// ============================================================================
MemoryPool* MemoryPool::instance = nullptr;

MemoryPool* MemoryPool::getInstance() {
    if (!instance) {
        instance = new MemoryPool();
    }
    return instance;
}

MemoryPool::MemoryPool() {
    totalAllocated = 0;
    peakUsage = 0;
    allocCount = 0;
    freeCount = 0;
    fragmentationCount = 0;
    
    // Initialize mutex
    poolMutex = xSemaphoreCreateMutex();
    if (!poolMutex) {
        Serial.println("MemoryPool: Failed to create mutex");
        return;
    }
    
    // Initialize all blocks
    for (int i = 0; i < MEMORY_POOL_SMALL_BLOCKS; i++) {
        smallBlocks[i].inUse = false;
        smallBlocks[i].size = MEMORY_POOL_SMALL_SIZE;
        smallBlocks[i].data = malloc(MEMORY_POOL_SMALL_SIZE);
        smallBlocks[i].owner = nullptr;
        smallBlocks[i].timestamp = 0;
    }
    
    for (int i = 0; i < MEMORY_POOL_MEDIUM_BLOCKS; i++) {
        mediumBlocks[i].inUse = false;
        mediumBlocks[i].size = MEMORY_POOL_MEDIUM_SIZE;
        mediumBlocks[i].data = malloc(MEMORY_POOL_MEDIUM_SIZE);
        mediumBlocks[i].owner = nullptr;
        mediumBlocks[i].timestamp = 0;
    }
    
    for (int i = 0; i < MEMORY_POOL_LARGE_BLOCKS; i++) {
        largeBlocks[i].inUse = false;
        largeBlocks[i].size = MEMORY_POOL_LARGE_SIZE;
        largeBlocks[i].data = malloc(MEMORY_POOL_LARGE_SIZE);
        largeBlocks[i].owner = nullptr;
        largeBlocks[i].timestamp = 0;
    }
    
    Serial.println("MemoryPool: Initialized with fixed-size pools");
    log_add("Memory pool system initialized");
}

MemoryPool::~MemoryPool() {
    cleanup();
    
    if (poolMutex) {
        vSemaphoreDelete(poolMutex);
    }
    
    if (instance) {
        delete instance;
        instance = nullptr;
    }
}

void MemoryPool::initialize() {
    // Initialization is done in constructor
    Serial.println("MemoryPool: Ready for allocations");
}

void MemoryPool::cleanup() {
    if (!poolMutex) return;
    
    if (xSemaphoreTake(poolMutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        Serial.println("MemoryPool: Failed to acquire mutex for cleanup");
        return;
    }
    
    // Free all small blocks
    for (int i = 0; i < MEMORY_POOL_SMALL_BLOCKS; i++) {
        if (smallBlocks[i].data) {
            free(smallBlocks[i].data);
            smallBlocks[i].data = nullptr;
        }
        smallBlocks[i].inUse = false;
        smallBlocks[i].owner = nullptr;
    }
    
    // Free all medium blocks
    for (int i = 0; i < MEMORY_POOL_MEDIUM_BLOCKS; i++) {
        if (mediumBlocks[i].data) {
            free(mediumBlocks[i].data);
            mediumBlocks[i].data = nullptr;
        }
        mediumBlocks[i].inUse = false;
        mediumBlocks[i].owner = nullptr;
    }
    
    // Free all large blocks
    for (int i = 0; i < MEMORY_POOL_LARGE_BLOCKS; i++) {
        if (largeBlocks[i].data) {
            free(largeBlocks[i].data);
            largeBlocks[i].data = nullptr;
        }
        largeBlocks[i].inUse = false;
        largeBlocks[i].owner = nullptr;
    }
    
    totalAllocated = 0;
    allocCount = 0;
    freeCount = 0;
    
    xSemaphoreGive(poolMutex);
    Serial.println("MemoryPool: Cleanup completed");
}

void* MemoryPool::allocate(size_t size) {
    if (!poolMutex) {
        Serial.println("MemoryPool: Mutex not available");
        return nullptr;
    }
    
    if (size == 0) {
        return nullptr;
    }
    
    if (xSemaphoreTake(poolMutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        Serial.println("MemoryPool: Failed to acquire mutex for allocation");
        return nullptr;
    }
    
    MemoryBlock* block = nullptr;
    
    // Find appropriate block size
    if (size <= MEMORY_POOL_SMALL_SIZE) {
        // Find free small block
        for (int i = 0; i < MEMORY_POOL_SMALL_BLOCKS; i++) {
            if (!smallBlocks[i].inUse) {
                block = &smallBlocks[i];
                break;
            }
        }
    } else if (size <= MEMORY_POOL_MEDIUM_SIZE) {
        // Find free medium block
        for (int i = 0; i < MEMORY_POOL_MEDIUM_BLOCKS; i++) {
            if (!mediumBlocks[i].inUse) {
                block = &mediumBlocks[i];
                break;
            }
        }
    } else if (size <= MEMORY_POOL_LARGE_SIZE) {
        // Find free large block
        for (int i = 0; i < MEMORY_POOL_LARGE_BLOCKS; i++) {
            if (!largeBlocks[i].inUse) {
                block = &largeBlocks[i];
                break;
            }
        }
    } else {
        // Size too large for pool, fallback to heap
        Serial.printf("MemoryPool: Size %u too large for pool, using heap\n", size);
        fragmentationCount++;
        xSemaphoreGive(poolMutex);
        return malloc(size);
    }
    
    if (!block) {
        // No suitable block available
        Serial.printf("MemoryPool: No blocks available for size %u\n", size);
        fragmentationCount++;
        xSemaphoreGive(poolMutex);
        return nullptr;
    }
    
    // Mark block as used
    block->inUse = true;
    block->owner = xTaskGetCurrentTaskHandle();
    block->timestamp = millis();
    
    // Update statistics
    totalAllocated += block->size;
    allocCount++;
    if (totalAllocated > peakUsage) {
        peakUsage = totalAllocated;
    }
    
    void* ptr = block->data;
    
    xSemaphoreGive(poolMutex);
    
    Serial.printf("MemoryPool: Allocated %u bytes from pool\n", block->size);
    return ptr;
}

void MemoryPool::deallocate(void* ptr) {
    if (!ptr) {
        return;
    }
    
    if (!poolMutex) {
        Serial.println("MemoryPool: Mutex not available for deallocation");
        return;
    }
    
    if (xSemaphoreTake(poolMutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        Serial.println("MemoryPool: Failed to acquire mutex for deallocation");
        return;
    }
    
    bool found = false;
    
    // Search small blocks
    for (int i = 0; i < MEMORY_POOL_SMALL_BLOCKS; i++) {
        if (smallBlocks[i].data == ptr && smallBlocks[i].inUse) {
            smallBlocks[i].inUse = false;
            smallBlocks[i].owner = nullptr;
            totalAllocated -= smallBlocks[i].size;
            freeCount++;
            found = true;
            break;
        }
    }
    
    // Search medium blocks
    if (!found) {
        for (int i = 0; i < MEMORY_POOL_MEDIUM_BLOCKS; i++) {
            if (mediumBlocks[i].data == ptr && mediumBlocks[i].inUse) {
                mediumBlocks[i].inUse = false;
                mediumBlocks[i].owner = nullptr;
                totalAllocated -= mediumBlocks[i].size;
                freeCount++;
                found = true;
                break;
            }
        }
    }
    
    // Search large blocks
    if (!found) {
        for (int i = 0; i < MEMORY_POOL_LARGE_BLOCKS; i++) {
            if (largeBlocks[i].data == ptr && largeBlocks[i].inUse) {
                largeBlocks[i].inUse = false;
                largeBlocks[i].owner = nullptr;
                totalAllocated -= largeBlocks[i].size;
                freeCount++;
                found = true;
                break;
            }
        }
    }
    
    if (!found) {
        // Not found in pool, assume it was allocated from heap
        free(ptr);
        Serial.println("MemoryPool: Deallocated heap memory");
    } else {
        Serial.printf("MemoryPool: Deallocated %u bytes to pool\n", found ? 64 : 256); // Simplified
    }
    
    xSemaphoreGive(poolMutex);
}

void MemoryPool::defragment() {
    if (!poolMutex) return;
    
    if (xSemaphoreTake(poolMutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        Serial.println("MemoryPool: Failed to acquire mutex for defragmentation");
        return;
    }
    
    // Check for orphaned blocks (blocks marked as used but task no longer exists)
    uint32_t orphanedCount = 0;
    uint32_t currentTime = millis();
    
    // Check small blocks
    for (int i = 0; i < MEMORY_POOL_SMALL_BLOCKS; i++) {
        if (smallBlocks[i].inUse && smallBlocks[i].owner) {
            // Check if task still exists
            if (eTaskGetState(smallBlocks[i].owner) == eDeleted) {
                smallBlocks[i].inUse = false;
                smallBlocks[i].owner = nullptr;
                totalAllocated -= smallBlocks[i].size;
                orphanedCount++;
            } else if (currentTime - smallBlocks[i].timestamp > 300000) { // 5 minutes
                // Block held too long, assume orphaned
                smallBlocks[i].inUse = false;
                smallBlocks[i].owner = nullptr;
                totalAllocated -= smallBlocks[i].size;
                orphanedCount++;
            }
        }
    }
    
    // Check medium blocks
    for (int i = 0; i < MEMORY_POOL_MEDIUM_BLOCKS; i++) {
        if (mediumBlocks[i].inUse && mediumBlocks[i].owner) {
            if (eTaskGetState(mediumBlocks[i].owner) == eDeleted) {
                mediumBlocks[i].inUse = false;
                mediumBlocks[i].owner = nullptr;
                totalAllocated -= mediumBlocks[i].size;
                orphanedCount++;
            } else if (currentTime - mediumBlocks[i].timestamp > 300000) {
                mediumBlocks[i].inUse = false;
                mediumBlocks[i].owner = nullptr;
                totalAllocated -= mediumBlocks[i].size;
                orphanedCount++;
            }
        }
    }
    
    // Check large blocks
    for (int i = 0; i < MEMORY_POOL_LARGE_BLOCKS; i++) {
        if (largeBlocks[i].inUse && largeBlocks[i].owner) {
            if (eTaskGetState(largeBlocks[i].owner) == eDeleted) {
                largeBlocks[i].inUse = false;
                largeBlocks[i].owner = nullptr;
                totalAllocated -= largeBlocks[i].size;
                orphanedCount++;
            } else if (currentTime - largeBlocks[i].timestamp > 300000) {
                largeBlocks[i].inUse = false;
                largeBlocks[i].owner = nullptr;
                totalAllocated -= largeBlocks[i].size;
                orphanedCount++;
            }
        }
    }
    
    if (orphanedCount > 0) {
        Serial.printf("MemoryPool: Defragmented %u orphaned blocks\n", orphanedCount);
        fragmentationCount += orphanedCount;
    }
    
    xSemaphoreGive(poolMutex);
}

bool MemoryPool::isValidPointer(void* ptr) const {
    if (!ptr) return false;
    
    // Check small blocks
    for (int i = 0; i < MEMORY_POOL_SMALL_BLOCKS; i++) {
        if (smallBlocks[i].data == ptr && smallBlocks[i].inUse) {
            return true;
        }
    }
    
    // Check medium blocks
    for (int i = 0; i < MEMORY_POOL_MEDIUM_BLOCKS; i++) {
        if (mediumBlocks[i].data == ptr && mediumBlocks[i].inUse) {
            return true;
        }
    }
    
    // Check large blocks
    for (int i = 0; i < MEMORY_POOL_LARGE_BLOCKS; i++) {
        if (largeBlocks[i].data == ptr && largeBlocks[i].inUse) {
            return true;
        }
    }
    
    return false;
}

void MemoryPool::printStatistics() const {
    if (!poolMutex) return;
    
    if (xSemaphoreTake(poolMutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        Serial.println("MemoryPool: Failed to acquire mutex for statistics");
        return;
    }
    
    Serial.println("=== Memory Pool Statistics ===");
    Serial.printf("Total Allocated: %u bytes\n", totalAllocated);
    Serial.printf("Peak Usage: %u bytes\n", peakUsage);
    Serial.printf("Allocations: %u\n", allocCount);
    Serial.printf("Deallocations: %u\n", freeCount);
    Serial.printf("Fragmentation Events: %u\n", fragmentationCount);
    
    // Calculate pool utilization
    uint32_t smallUsed = 0, mediumUsed = 0, largeUsed = 0;
    
    for (int i = 0; i < MEMORY_POOL_SMALL_BLOCKS; i++) {
        if (smallBlocks[i].inUse) smallUsed++;
    }
    
    for (int i = 0; i < MEMORY_POOL_MEDIUM_BLOCKS; i++) {
        if (mediumBlocks[i].inUse) mediumUsed++;
    }
    
    for (int i = 0; i < MEMORY_POOL_LARGE_BLOCKS; i++) {
        if (largeBlocks[i].inUse) largeUsed++;
    }
    
    Serial.printf("Small Pool: %u/%u blocks used (%.1f%%)\n", 
                smallUsed, MEMORY_POOL_SMALL_BLOCKS, 
                (float)smallUsed / MEMORY_POOL_SMALL_BLOCKS * 100.0f);
    Serial.printf("Medium Pool: %u/%u blocks used (%.1f%%)\n", 
                mediumUsed, MEMORY_POOL_MEDIUM_BLOCKS, 
                (float)mediumUsed / MEMORY_POOL_MEDIUM_BLOCKS * 100.0f);
    Serial.printf("Large Pool: %u/%u blocks used (%.1f%%)\n", 
                largeUsed, MEMORY_POOL_LARGE_BLOCKS, 
                (float)largeUsed / MEMORY_POOL_LARGE_BLOCKS * 100.0f);
    
    Serial.println("============================");
    
    xSemaphoreGive(poolMutex);
}

void MemoryPool::printPoolStatus() const {
    if (!poolMutex) return;
    
    if (xSemaphoreTake(poolMutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        Serial.println("MemoryPool: Failed to acquire mutex for status");
        return;
    }
    
    Serial.println("=== Memory Pool Status ===");
    
    // Small blocks
    Serial.println("Small Blocks:");
    for (int i = 0; i < MEMORY_POOL_SMALL_BLOCKS; i++) {
        if (smallBlocks[i].inUse) {
            Serial.printf("  Block %d: Used by task %p, age %ums\n", 
                        i, smallBlocks[i].owner, 
                        millis() - smallBlocks[i].timestamp);
        }
    }
    
    // Medium blocks
    Serial.println("Medium Blocks:");
    for (int i = 0; i < MEMORY_POOL_MEDIUM_BLOCKS; i++) {
        if (mediumBlocks[i].inUse) {
            Serial.printf("  Block %d: Used by task %p, age %ums\n", 
                        i, mediumBlocks[i].owner, 
                        millis() - mediumBlocks[i].timestamp);
        }
    }
    
    // Large blocks
    Serial.println("Large Blocks:");
    for (int i = 0; i < MEMORY_POOL_LARGE_BLOCKS; i++) {
        if (largeBlocks[i].inUse) {
            Serial.printf("  Block %d: Used by task %p, age %ums\n", 
                        i, largeBlocks[i].owner, 
                        millis() - largeBlocks[i].timestamp);
        }
    }
    
    Serial.println("========================");
    
    xSemaphoreGive(poolMutex);
}
