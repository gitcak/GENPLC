# Event-Driven Task Optimization Implementation Plan

## Priority 1: MQTT Task Optimization (Highest Impact)

### Current Issues
- **500ms blocking wait** for module initialization
- **2000ms blocking wait** for network connection
- **200ms fixed loop** regardless of data availability
- **500ms blocking wait** after module disconnect

### Proposed Solution

#### Step 1: Add Event Bits
```cpp
// In task_config.h
#define EVENT_BIT_MQTT_DATA_READY     (1UL << 8)
#define EVENT_BIT_MQTT_PUBLISH_REQ    (1UL << 9)
#define EVENT_BIT_MQTT_RECONNECT_REQ  (1UL << 10)
```

#### Step 2: Update MQTT Task
```cpp
void vTaskMQTT(void* pvParameters) {
    (void)pvParameters;
    settingsLoad(s);
    
    // Wait for module initialization event (non-blocking)
    EventBits_t bits = xEventGroupWaitBits(
        xEventGroupSystemStatus,
        EVENT_BIT_CELLULAR_READY,
        pdFALSE,
        pdTRUE,
        portMAX_DELAY
    );
    
    // Configure APN from settings
    if (strlen(s.apn)) {
        catmGnssModule->setApnCredentials(String(s.apn), String(s.apnUser), String(s.apnPass));
    }
    
    uint32_t lastPub = 0;
    uint32_t lastConnTry = 0;
    bool mqttUp = false;
    
    for (;;) {
        // Wait for data ready, publish request, or reconnect request
        bits = xEventGroupWaitBits(
            xEventGroupSystemStatus,
            EVENT_BIT_MQTT_DATA_READY | EVENT_BIT_MQTT_PUBLISH_REQ | EVENT_BIT_MQTT_RECONNECT_REQ,
            pdFALSE,
            pdFALSE,
            pdMS_TO_TICKS(1000)  // 1s timeout for periodic checks
        );
        
        // Handle module disconnect
        if (!catmGnssModule || !catmGnssModule->isModuleInitialized()) {
            mqttUp = false;
            // Wait for module to return
            xEventGroupWaitBits(xEventGroupSystemStatus, EVENT_BIT_CELLULAR_READY, 
                pdFALSE, pdTRUE, portMAX_DELAY);
            continue;
        }
        
        // Handle network disconnect
        if (!catmGnssModule->isNetworkConnected()) {
            mqttUp = false;
            // Wait for network connection
            xEventGroupWaitBits(xEventGroupSystemStatus, EVENT_BIT_CELLULAR_READY, 
                pdFALSE, pdTRUE, pdMS_TO_TICKS(30000));
            continue;
        }
        
        // Handle data publishing
        if (bits & (EVENT_BIT_MQTT_DATA_READY | EVENT_BIT_MQTT_PUBLISH_REQ)) {
            publishData();
        }
        
        // Handle reconnect request
        if (bits & EVENT_BIT_MQTT_RECONNECT_REQ) {
            mqttUp = false;
            reconnectMQTT();
        }
    }
}
```

#### Step 3: Trigger Events from Data Sources
```cpp
// In GNSS task - trigger MQTT publish when new data available
void vTaskGNSS(void* pvParameters) {
    for (;;) {
        // Get GNSS data
        if (getGNSSData()) {
            // Trigger MQTT publish
            xEventGroupSetBits(xEventGroupSystemStatus, EVENT_BIT_MQTT_DATA_READY);
        }
        
        // Wait for next update request or timeout
        xEventGroupWaitBits(xEventGroupSystemStatus, EVENT_BIT_GNSS_UPDATE_REQ, 
            pdFALSE, pdFALSE, pdMS_TO_TICKS(10000));
    }
}
```

## Priority 2: CatM+GNSS Task Optimization

### Current Issues
- **10s fixed interval** regardless of data availability
- **No response to external requests** for GNSS updates
- **Inefficient power usage** with fixed polling

### Proposed Solution

#### Step 1: Add GNSS Event Bits
```cpp
#define EVENT_BIT_GNSS_UPDATE_REQ    (1UL << 11)
#define EVENT_BIT_GNSS_DATA_READY    (1UL << 12)
```

#### Step 2: Update GNSS Task
```cpp
void vTaskGNSS(void* pvParameters) {
    (void)pvParameters;
    
    for (;;) {
        // Wait for update request or periodic timeout
        EventBits_t bits = xEventGroupWaitBits(
            xEventGroupSystemStatus,
            EVENT_BIT_GNSS_UPDATE_REQ | EVENT_BIT_ERROR_DETECTED,
            pdFALSE,
            pdFALSE,
            pdMS_TO_TICKS(10000)  // 10s periodic update
        );
        
        if (bits & EVENT_BIT_ERROR_DETECTED) {
            // Handle error - wait for recovery
            xEventGroupWaitBits(xEventGroupSystemStatus, EVENT_BIT_CELLULAR_READY, 
                pdFALSE, pdTRUE, portMAX_DELAY);
            continue;
        }
        
        // Perform GNSS update
        if (updateGNSSData()) {
            // Signal data ready for MQTT
            xEventGroupSetBits(xEventGroupSystemStatus, EVENT_BIT_MQTT_DATA_READY);
        }
    }
}
```

#### Step 3: Add Manual GNSS Update Trigger
```cpp
// Function to trigger immediate GNSS update
void requestGNSSUpdate() {
    xEventGroupSetBits(xEventGroupSystemStatus, EVENT_BIT_GNSS_UPDATE_REQ);
}
```

## Priority 3: System Monitor Optimization

### Current Issues
- **5s fixed polling** for system status
- **No response to critical events** (errors, status changes)
- **Inefficient resource usage** with constant polling

### Proposed Solution

#### Step 1: Add System Event Bits
```cpp
#define EVENT_BIT_SYSTEM_ERROR       (1UL << 13)
#define EVENT_BIT_STATUS_CHANGE      (1UL << 14)
#define EVENT_BIT_HEAP_LOW           (1UL << 15)
```

#### Step 2: Update System Monitor
```cpp
void vTaskSystemMonitor(void* pvParameters) {
    (void)pvParameters;
    
    for (;;) {
        // Wait for system events or periodic timeout
        EventBits_t bits = xEventGroupWaitBits(
            xEventGroupSystemStatus,
            EVENT_BIT_SYSTEM_ERROR | EVENT_BIT_STATUS_CHANGE | EVENT_BIT_HEAP_LOW,
            pdFALSE,
            pdFALSE,
            pdMS_TO_TICKS(5000)  // 5s periodic check
        );
        
        // Handle immediate events
        if (bits & EVENT_BIT_SYSTEM_ERROR) {
            handleSystemError();
        }
        
        if (bits & EVENT_BIT_STATUS_CHANGE) {
            handleStatusChange();
        }
        
        if (bits & EVENT_BIT_HEAP_LOW) {
            handleLowMemory();
        }
        
        // Perform periodic system check
        performSystemCheck();
    }
}
```

## Implementation Steps

### Phase 1: Event Group Setup
1. **Add new event bits** to `task_config.h`
2. **Update event group initialization** in main.cpp
3. **Test event group functionality**

### Phase 2: MQTT Task Migration
1. **Update MQTT task** to use event-driven waits
2. **Add event triggers** from data sources
3. **Test MQTT functionality** with events

### Phase 3: GNSS Task Migration
1. **Update GNSS task** to use event-driven waits
2. **Add manual update triggers**
3. **Test GNSS functionality** with events

### Phase 4: System Monitor Migration
1. **Update system monitor** to use event-driven waits
2. **Add system event triggers**
3. **Test system monitoring** with events

### Phase 5: Testing & Validation
1. **Test all event-driven tasks** thoroughly
2. **Monitor system performance** and responsiveness
3. **Validate power efficiency** improvements
4. **Test error handling** and recovery

## Benefits

### Immediate Benefits
- **Reduced latency** - Tasks respond immediately to events
- **Better CPU utilization** - No unnecessary polling
- **Improved responsiveness** - Event-driven scheduling

### Long-term Benefits
- **Lower power consumption** - Tasks sleep until needed
- **Better system reliability** - Faster error recovery
- **Improved scalability** - Better resource management

## Risk Mitigation

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

## Success Metrics

### Performance Metrics
- **Task response time** - Measure time from event to task wake-up
- **CPU utilization** - Monitor CPU usage reduction
- **Memory usage** - Track memory efficiency improvements

### Reliability Metrics
- **Error recovery time** - Measure time to recover from errors
- **System stability** - Monitor for task deadlocks or hangs
- **Event handling** - Track event processing efficiency

### Power Metrics
- **Power consumption** - Measure power usage reduction
- **Wake-up frequency** - Monitor task wake-up patterns
- **Sleep efficiency** - Track time spent in sleep states
