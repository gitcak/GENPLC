# Task Delay Analysis & Event-Driven Optimization

## Overview
Analysis of blocking delays (`vTaskDelay`) in FreeRTOS tasks and opportunities for event-driven optimization.

## Current Delay Patterns

### 1. Main Task Delays (`src/main.cpp`)

#### Short Delays (Acceptable)
- **Line 347**: `vTaskDelay(pdMS_TO_TICKS(10))` - Display animation frame rate
- **Line 870**: `vTaskDelay(pdMS_TO_TICKS(20))` - Button polling rate
- **Line 875**: `vTaskDelay(pdMS_TO_TICKS(20))` - Button handling rate

#### Long Delays (Optimization Candidates)
- **Line 851**: `vTaskDelay(pdMS_TO_TICKS(1000))` - 1s sleep in idle task
- **Line 1012**: `vTaskDelay(pdMS_TO_TICKS(1000))` - Display initialization wait
- **Line 1260**: `vTaskDelay(pdMS_TO_TICKS(5000))` - 5s sleep in status bar task
- **Line 1282**: `vTaskDelay(pdMS_TO_TICKS(5000))` - 5s sleep in system monitor

### 2. Module Task Delays

#### CatM+GNSS Task (`src/modules/catm_gnss/catm_gnss_task.cpp`)
- **Line 89**: `vTaskDelay(pdMS_TO_TICKS(10000))` - 10s sleep between GNSS updates

#### MQTT Task (`src/modules/mqtt/mqtt_task.cpp`)
- **Line 16**: `vTaskDelay(pdMS_TO_TICKS(500))` - Wait for module initialization
- **Line 32**: `vTaskDelay(pdMS_TO_TICKS(500))` - Wait after module disconnect
- **Line 38**: `vTaskDelay(pdMS_TO_TICKS(2000))` - Wait for network connection
- **Line 66**: `vTaskDelay(pdMS_TO_TICKS(200))` - Main loop delay

#### CatM+GNSS Module (`src/modules/catm_gnss/catm_gnss_module.cpp`)
- **Line 94**: `delay(150)` - AT command settle time
- **Line 163**: `delay(500)` - Module initialization delay
- **Line 215**: `delay(10)` - Short command delay
- **Line 398**: `vTaskDelay(pdMS_TO_TICKS(2000))` - Network disconnect retry
- **Line 431**: `vTaskDelay(pdMS_TO_TICKS(2000))` - Connection retry
- **Line 717**: `delay(500)` - Serial transmission delay
- **Line 942**: `vTaskDelay(pdMS_TO_TICKS(1000))` - Module sleep

## Optimization Opportunities

### 1. Event-Driven Module Initialization

**Current (Blocking):**
```cpp
// MQTT Task - Wait for module initialization
while (!catmGnssModule || !catmGnssModule->isModuleInitialized()) {
    vTaskDelay(pdMS_TO_TICKS(500));  // Blocking wait
}
```

**Proposed (Event-Driven):**
```cpp
// Wait for module initialization event
EventBits_t bits = xEventGroupWaitBits(
    xEventGroupSystemStatus,
    EVENT_BIT_CELLULAR_READY,
    pdFALSE,  // Clear bits on exit
    pdTRUE,   // Wait for all bits
    portMAX_DELAY
);
```

### 2. Event-Driven Network Status

**Current (Polling):**
```cpp
// MQTT Task - Poll network status
if (!catmGnssModule->isNetworkConnected()) {
    vTaskDelay(pdMS_TO_TICKS(2000));  // Blocking wait
    continue;
}
```

**Proposed (Event-Driven):**
```cpp
// Wait for network connection event
EventBits_t bits = xEventGroupWaitBits(
    xEventGroupSystemStatus,
    EVENT_BIT_CELLULAR_READY,
    pdFALSE,
    pdTRUE,
    pdMS_TO_TICKS(30000)  // Timeout after 30s
);
```

### 3. Event-Driven GNSS Updates

**Current (Fixed Interval):**
```cpp
// CatM+GNSS Task - Fixed 10s interval
vTaskDelay(pdMS_TO_TICKS(10000));  // Always wait 10s
```

**Proposed (Event-Driven):**
```cpp
// Wait for GNSS update request or timeout
EventBits_t bits = xEventGroupWaitBits(
    xEventGroupSystemStatus,
    EVENT_BIT_GNSS_READY | EVENT_BIT_ERROR_DETECTED,
    pdFALSE,
    pdFALSE,  // Wait for any bit
    pdMS_TO_TICKS(10000)  // 10s timeout
);
```

### 4. Event-Driven MQTT Publishing

**Current (Fixed Interval):**
```cpp
// MQTT Task - Fixed 200ms loop
vTaskDelay(pdMS_TO_TICKS(200));  // Always wait 200ms
```

**Proposed (Event-Driven):**
```cpp
// Wait for publish request or periodic timeout
EventBits_t bits = xEventGroupWaitBits(
    xEventGroupSystemStatus,
    EVENT_BIT_DATA_READY | EVENT_BIT_ERROR_DETECTED,
    pdFALSE,
    pdFALSE,
    pdMS_TO_TICKS(200)  // 200ms timeout for periodic checks
);
```

## Implementation Strategy

### Phase 1: Event Group Setup

**Add to `task_config.h`:**
```cpp
// Additional event bits for task coordination
#define EVENT_BIT_DATA_READY          (1UL << 8)
#define EVENT_BIT_GNSS_UPDATE_REQ    (1UL << 9)
#define EVENT_BIT_MQTT_PUBLISH_REQ   (1UL << 10)
#define EVENT_BIT_MODULE_STATUS_CHG  (1UL << 11)
```

### Phase 2: Module Status Events

**Update CatM+GNSS Module:**
```cpp
// Set events when status changes
void CatMGNSSModule::setModuleStatus(bool connected) {
    if (connected != isConnected) {
        isConnected = connected;
        xEventGroupSetBits(xEventGroupSystemStatus, 
            connected ? EVENT_BIT_CELLULAR_READY : EVENT_BIT_ERROR_DETECTED);
    }
}
```

### Phase 3: Task Coordination

**Update MQTT Task:**
```cpp
void vTaskMQTT(void* pvParameters) {
    // Wait for module initialization event
    xEventGroupWaitBits(xEventGroupSystemStatus, EVENT_BIT_CELLULAR_READY, 
        pdFALSE, pdTRUE, portMAX_DELAY);
    
    for (;;) {
        // Wait for data ready or timeout
        EventBits_t bits = xEventGroupWaitBits(xEventGroupSystemStatus,
            EVENT_BIT_DATA_READY | EVENT_BIT_ERROR_DETECTED,
            pdFALSE, pdFALSE, pdMS_TO_TICKS(200));
        
        if (bits & EVENT_BIT_DATA_READY) {
            // Publish data immediately
            publishData();
        }
        
        if (bits & EVENT_BIT_ERROR_DETECTED) {
            // Handle error
            handleError();
        }
    }
}
```

## Benefits

### Performance
- **Reduced latency** - Tasks respond immediately to events
- **Better CPU utilization** - No unnecessary polling
- **Improved responsiveness** - Event-driven instead of time-driven

### Power Efficiency
- **Lower power consumption** - Tasks sleep until needed
- **Reduced wake-ups** - Only wake on actual events
- **Better battery life** - For battery-powered applications

### System Reliability
- **Faster error recovery** - Immediate response to failures
- **Better resource management** - Tasks only run when needed
- **Improved real-time behavior** - Event-driven scheduling

## Migration Priority

### High Priority (Immediate Impact)
1. **MQTT Task** - Replace polling with event-driven publishing
2. **CatM+GNSS Task** - Replace fixed intervals with event-driven updates
3. **Module Initialization** - Replace blocking waits with event groups

### Medium Priority (Performance Improvement)
1. **System Monitor** - Replace 5s polling with event-driven monitoring
2. **Status Bar** - Replace 5s polling with event-driven updates
3. **Network Status** - Replace polling with event-driven status changes

### Low Priority (Acceptable as-is)
1. **Display Animation** - 10ms delays are acceptable for smooth animation
2. **Button Polling** - 20ms delays are acceptable for responsive UI
3. **AT Command Delays** - Hardware-specific timing requirements

## Implementation Considerations

### Event Group Management
- **Clear bits appropriately** - Avoid stale event states
- **Handle timeouts** - Provide fallback behavior
- **Monitor event usage** - Ensure events are set when needed

### Task Synchronization
- **Avoid deadlocks** - Careful event group usage
- **Handle priority inversion** - Consider task priorities
- **Monitor task states** - Ensure tasks don't get stuck

### Backward Compatibility
- **Maintain existing behavior** - Don't break current functionality
- **Gradual migration** - Implement event-driven patterns incrementally
- **Testing** - Thoroughly test event-driven implementations

## Alternative: Hybrid Approach

For some tasks, combine event-driven with periodic timeouts:
```cpp
// Wait for event or periodic timeout
EventBits_t bits = xEventGroupWaitBits(xEventGroupSystemStatus,
    EVENT_BIT_DATA_READY,
    pdFALSE,
    pdFALSE,
    pdMS_TO_TICKS(1000)  // 1s periodic check
);

if (bits & EVENT_BIT_DATA_READY) {
    // Handle immediate event
} else {
    // Handle periodic timeout
}
```

This provides both responsiveness and reliability.
