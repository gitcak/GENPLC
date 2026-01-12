/*
 * Memory Safety Utilities
 * Provides enhanced null checking and bounds validation for ESP32 crash prevention
 */

#ifndef MEMORY_SAFETY_H
#define MEMORY_SAFETY_H

#include <Arduino.h>
#include <string.h>

// ============================================================================
// MEMORY SAFETY MACROS
// ============================================================================

// Safe pointer access with null checking
#define SAFE_PTR_ACCESS(ptr, type) \
    ((ptr) ? (*(static_cast<type*>(ptr))) : (type)0)

// Safe array access with bounds checking
#define SAFE_ARRAY_ACCESS(arr, index, size) \
    (((index) >= 0 && (index) < (size)) ? (arr)[(index)] : (typeof(arr[0]))0)

// Safe string operations with length checking
#define SAFE_STR_LEN(str) \
    ((str) ? strlen(str) : 0)

#define SAFE_STR_COPY(dest, src, dest_size) \
    do { \
        if ((dest) && (src) && (dest_size) > 0) { \
            size_t src_len = strlen(src); \
            size_t copy_len = (src_len < (size_t)(dest_size) - 1) ? src_len : (size_t)(dest_size) - 1; \
            memcpy(dest, src, copy_len); \
            dest[copy_len] = '\0'; \
        } else if ((dest) && (dest_size) > 0) { \
            dest[0] = '\0'; \
        } \
    } while(0)

// Safe substring operations
#define SAFE_SUBSTRING(str, start, len) \
    ((str) && (start) >= 0 && (len) > 0 && (start) < (int)strlen(str) ? \
     String((str) + (start)).substring(0, (len)) : String())

// ============================================================================
// MEMORY SAFETY FUNCTIONS
// ============================================================================

/**
 * @brief Safely access a pointer with bounds checking
 * @param ptr Pointer to access
 * @param offset Byte offset from pointer
 * @param size Size of data to access
 * @param defaultValue Default value if access is unsafe
 * @return True if access is safe, false otherwise
 */
static inline bool safeMemoryAccess(const void* ptr, size_t offset, size_t size, void* dest, size_t destSize) {
    if (!ptr || !dest) return false;
    
    // Check for integer overflow in offset calculation
    if (offset > SIZE_MAX - size) return false;
    
    // Simple heuristic: don't access memory that seems invalid
    // (this is basic protection, not foolproof)
    const uintptr_t ptrAddr = (uintptr_t)ptr + offset;
    if (ptrAddr < 0x20000000 || ptrAddr > 0x80000000) {
        return false; // ESP32 RAM is typically in 0x20000000-0x80000000 range
    }
    
    if (size > destSize) return false;
    
    memcpy(dest, (const uint8_t*)ptr + offset, size);
    return true;
}

/**
 * @brief Validate if a pointer is likely valid
 * @param ptr Pointer to validate
 * @return True if pointer appears valid, false otherwise
 */
static inline bool isValidPointer(const void* ptr) {
    if (!ptr) return false;
    
    uintptr_t addr = (uintptr_t)ptr;
    
    // Check for common invalid pointer patterns
    if (addr < 0x1000) return false; // Too low, likely null deref
    if (addr > 0x80000000) return false; // Too high for ESP32 RAM
    
    // Check for alignment (most valid pointers are 4-byte aligned on ESP32)
    if (addr % 4 != 0) {
        // Allow some misalignment but be suspicious
        return false;
    }
    
    return true;
}

/**
 * @brief Safe string comparison with null checking
 * @param str1 First string
 * @param str2 Second string  
 * @return True if strings are equal (both null or both equal), false otherwise
 */
static inline bool safeStrEqual(const char* str1, const char* str2) {
    if (!str1 && !str2) return true;
    if (!str1 || !str2) return false;
    return strcmp(str1, str2) == 0;
}

/**
 * @brief Safe string to integer conversion with validation
 * @param str String to convert
 * @param defaultValue Default value if conversion fails
 * @return Converted integer value or default
 */
static inline int safeStrToInt(const char* str, int defaultValue = 0) {
    if (!str || !str[0]) return defaultValue;
    
    char* endPtr;
    long result = strtol(str, &endPtr, 10);
    
    // Check if conversion was successful
    if (endPtr == str) return defaultValue; // No digits found
    
    // Check for overflow/underflow
    if (result > INT_MAX || result < INT_MIN) return defaultValue;
    
    return (int)result;
}

/**
 * @brief Safe string to float conversion with validation
 * @param str String to convert
 * @param defaultValue Default value if conversion fails
 * @return Converted float value or default
 */
static inline float safeStrToFloat(const char* str, float defaultValue = 0.0f) {
    if (!str || !str[0]) return defaultValue;
    
    char* endPtr;
    float result = strtof(str, &endPtr);
    
    // Check if conversion was successful
    if (endPtr == str) return defaultValue; // No digits found
    
    return result;
}

/**
 * @brief Validate array indices
 * @param index Index to check
 * @param size Array size
 * @return True if index is valid, false otherwise
 */
static inline bool isValidArrayIndex(int index, size_t size) {
    return index >= 0 && (size_t)index < size;
}

/**
 * @brief Check if a value is within range
 * @param value Value to check
 * @param min Minimum value
 * @param max Maximum value
 * @return True if value is within range, false otherwise
 */
static inline bool isInRange(int value, int min, int max) {
    return value >= min && value <= max;
}

/**
 * @brief Safe bounds checking for numeric operations
 * @param value Value to check
 * @param min Minimum allowed value
 * @param max Maximum allowed value
 * @param defaultValue Default value if out of bounds
 * @return Value within bounds or default
 */
static inline int clampToBounds(int value, int min, int max, int defaultValue) {
    if (value < min || value > max) {
        return defaultValue;
    }
    return value;
}

#endif // MEMORY_SAFETY_H
