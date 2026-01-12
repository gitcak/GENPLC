# Event Group System Fix

## Overview
Fix for undefined reference to `xEventGroupSystemStatus` linker error.

## Issue

### ❌ **Linker Error**
```
undefined reference to `xEventGroupSystemStatus'
```

**Problem:** The event group was declared as `extern` in multiple files but never actually defined/created.

## Solution

### ✅ **Event Group Declaration**

**Added to global variables section in `src/main.cpp`:**
```cpp
// System event group for task coordination
EventGroupHandle_t xEventGroupSystemStatus = nullptr;
```

**Location:** After other task handles, before other global variables

### ✅ **Event Group Creation**

**Added to setup function in `src/main.cpp`:**
```cpp
// Create system event group
xEventGroupSystemStatus = xEventGroupCreate();
if (xEventGroupSystemStatus == NULL) {
    Serial.println("ERROR: Failed to create system event group");
    return;
}
```

**Location:** After UI queue creation, before logging initialization

## Code Changes

### ✅ **Global Declaration**

**Before:**
```cpp
TaskHandle_t blinkTaskHandle = nullptr;
TaskHandle_t buttonTaskHandle = nullptr;
TaskHandle_t displayTaskHandle = nullptr;
TaskHandle_t statusBarTaskHandle = nullptr;
```

**After:**
```cpp
TaskHandle_t blinkTaskHandle = nullptr;
TaskHandle_t buttonTaskHandle = nullptr;
TaskHandle_t displayTaskHandle = nullptr;
TaskHandle_t statusBarTaskHandle = nullptr;

// System event group for task coordination
EventGroupHandle_t xEventGroupSystemStatus = nullptr;
```

### ✅ **Event Group Creation**

**Before:**
```cpp
// Create UI queue
g_uiQueue = xQueueCreate(16, sizeof(UIEvent));
// Init logging
log_init();
```

**After:**
```cpp
// Create UI queue
g_uiQueue = xQueueCreate(16, sizeof(UIEvent));

// Create system event group
xEventGroupSystemStatus = xEventGroupCreate();
if (xEventGroupSystemStatus == NULL) {
    Serial.println("ERROR: Failed to create system event group");
    return;
}

// Init logging
log_init();
```

## Usage Verification

### ✅ **Files Using Event Group**

**Files that declare `extern EventGroupHandle_t xEventGroupSystemStatus`:**
- `src/config/task_config.h` - Header declaration
- `src/modules/catm_gnss/catm_gnss_task.cpp` - CatM+GNSS task
- `src/modules/mqtt/mqtt_task.cpp` - MQTT task

**Files that use the event group:**
- `src/main.cpp` - Status bar task (`vTaskStatusBar`)
- `src/modules/catm_gnss/catm_gnss_task.cpp` - GNSS task coordination
- `src/modules/mqtt/mqtt_task.cpp` - MQTT task coordination

### ✅ **Event Bits Used**

**Event bits defined in `src/config/task_config.h`:**
```cpp
#define EVENT_BIT_CELLULAR_READY      (1UL << 0)
#define EVENT_BIT_MQTT_CONNECTED      (1UL << 1)
#define EVENT_BIT_MQTT_DATA_READY    (1UL << 8)
#define EVENT_BIT_MQTT_PUBLISH_REQ   (1UL << 9)
#define EVENT_BIT_MQTT_RECONNECT_REQ (1UL << 10)
#define EVENT_BIT_GNSS_UPDATE_REQ    (1UL << 11)
#define EVENT_BIT_GNSS_DATA_READY    (1UL << 12)
#define EVENT_BIT_SYSTEM_ERROR       (1UL << 13)
#define EVENT_BIT_STATUS_CHANGE      (1UL << 14)
#define EVENT_BIT_HEAP_LOW           (1UL << 15)
```

## Error Handling

### ✅ **Creation Failure Check**

**Added error handling:**
```cpp
if (xEventGroupSystemStatus == NULL) {
    Serial.println("ERROR: Failed to create system event group");
    return;
}
```

**Benefits:**
- **Early detection** of memory allocation failure
- **Clear error message** for debugging
- **Graceful failure** - stops setup if event group creation fails

## Memory Management

### ✅ **Event Group Lifecycle**

**Creation:** In `setup()` function during initialization
**Usage:** Throughout application lifetime for task coordination
**Cleanup:** Automatically cleaned up when application terminates

**Memory Usage:**
- **Event group object** - Small FreeRTOS structure
- **Event bits** - 32-bit value for event state
- **Minimal overhead** - Efficient task coordination mechanism

## Compilation Status

### ✅ **Linker Error Fixed**

**Before:**
```
undefined reference to `xEventGroupSystemStatus'
```

**After:**
- **Event group declared** - Global variable in main.cpp
- **Event group created** - `xEventGroupCreate()` in setup()
- **Error handling** - Check for creation failure
- **Ready for linking** - All references resolved

## Summary

**✅ Event Group System Fix: COMPLETE**

- **Event group declared** - Global variable in main.cpp
- **Event group created** - `xEventGroupCreate()` in setup function
- **Error handling added** - Check for creation failure
- **Linker error resolved** - All references now defined

**Key Changes:**
- **Added global declaration** - `EventGroupHandle_t xEventGroupSystemStatus = nullptr`
- **Added creation code** - `xEventGroupSystemStatus = xEventGroupCreate()`
- **Added error handling** - Check for NULL return value
- **Proper initialization** - Event group created before tasks that use it

**The event group system is now properly initialized and ready for task coordination. The linker error has been resolved.**
