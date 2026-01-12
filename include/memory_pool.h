/*
 * Memory Pool Manager
 * Efficient memory allocation for embedded systems
 * Prevents heap fragmentation and stack overflows
 */

#ifndef MEMORY_POOL_H
#define MEMORY_POOL_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// ============================================================================
// MEMORY POOL CONFIGURATION (Optimized for no-PSRAM StampS3A)
// ============================================================================
#define MEMORY_POOL_SMALL_BLOCKS   8     // Number of small blocks (32 bytes each)
#define MEMORY_POOL_MEDIUM_BLOCKS  4     // Number of medium blocks (128 bytes each)
#define MEMORY_POOL_LARGE_BLOCKS   2     // Number of large blocks (512 bytes each)

#define MEMORY_POOL_SMALL_SIZE    32
#define MEMORY_POOL_MEDIUM_SIZE   128
#define MEMORY_POOL_LARGE_SIZE   512

// ============================================================================
// MEMORY BLOCK STRUCTURE
// ============================================================================
struct MemoryBlock {
    bool inUse;
    size_t size;
    void* data;
    TaskHandle_t owner;
    uint32_t timestamp;
};

// ============================================================================
// MEMORY POOL CLASS
// ============================================================================
class MemoryPool {
private:
    static MemoryPool* instance;
    
    // Memory pools
    MemoryBlock smallBlocks[MEMORY_POOL_SMALL_BLOCKS];
    MemoryBlock mediumBlocks[MEMORY_POOL_MEDIUM_BLOCKS];
    MemoryBlock largeBlocks[MEMORY_POOL_LARGE_BLOCKS];
    
    // Pool statistics
    size_t totalAllocated;
    size_t peakUsage;
    uint32_t allocCount;
    uint32_t freeCount;
    uint32_t fragmentationCount;
    
    // Mutex for thread safety
    SemaphoreHandle_t poolMutex;
    
    MemoryPool();
    
public:
    // Singleton access
    static MemoryPool* getInstance();
    
    // Memory allocation
    void* allocate(size_t size);
    void deallocate(void* ptr);
    
    // Pool management
    void initialize();
    void cleanup();
    void defragment();
    
    // Statistics
    size_t getAllocatedSize() const { return totalAllocated; }
    size_t getPeakUsage() const { return peakUsage; }
    uint32_t getAllocationCount() const { return allocCount; }
    uint32_t getFreeCount() const { return freeCount; }
    uint32_t getFragmentationCount() const { return fragmentationCount; }
    
    // Diagnostics
    void printStatistics() const;
    void printPoolStatus() const;
    bool isValidPointer(void* ptr) const;
    
    // Destructor
    ~MemoryPool();
};

// ============================================================================
// MEMORY ALLOCATION MACROS
// ============================================================================
#define POOL_ALLOC(size) MemoryPool::getInstance()->allocate(size)
#define POOL_FREE(ptr) MemoryPool::getInstance()->deallocate(ptr)
#define POOL_STATS() MemoryPool::getInstance()->printStatistics()

// ============================================================================
// MEMORY GUARD CLASS (RAII)
// ============================================================================
class MemoryGuard {
private:
    void* ptr;
    
public:
    explicit MemoryGuard(size_t size) : ptr(POOL_ALLOC(size)) {}
    
    ~MemoryGuard() {
        if (ptr) {
            POOL_FREE(ptr);
        }
    }
    
    void* get() const { return ptr; }
    void* release() { 
        void* temp = ptr; 
        ptr = nullptr; 
        return temp; 
    }
    
    // Prevent copying
    MemoryGuard(const MemoryGuard&) = delete;
    MemoryGuard& operator=(const MemoryGuard&) = delete;
};

#endif // MEMORY_POOL_H
