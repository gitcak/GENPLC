# Debug System String Optimization Plan

## Current Issues
- DebugMessage struct uses 3 String fields per message
- Ring buffer holds 100 messages = 300 String allocations
- High-frequency logging creates heap fragmentation
- Long-running tasks accumulate memory issues

## Proposed Solution: Fixed-Size Char Arrays

### Phase 1: Update DebugMessage Structure

**Current:**
```cpp
struct DebugMessage {
    DebugMessageType type;
    uint32_t timestamp;
    String source;      // Dynamic allocation
    String message;      // Dynamic allocation
    String data;        // Dynamic allocation
    uint32_t messageId;
};
```

**Proposed:**
```cpp
struct DebugMessage {
    DebugMessageType type;
    uint32_t timestamp;
    char source[24];     // Fixed size - module names
    char message[48];   // Fixed size - log messages
    char data[96];      // Fixed size - data payload
    uint32_t messageId;
};
```

### Phase 2: Update Debug Logging Functions

**Current:**
```cpp
void DebugSystem::logInfo(const String& source, const String& message, const String& data) {
    DebugMessage msg;
    msg.source = source;      // String assignment
    msg.message = message;    // String assignment
    msg.data = data;          // String assignment
    // ...
}
```

**Proposed:**
```cpp
void DebugSystem::logInfo(const char* source, const char* message, const char* data) {
    DebugMessage msg;
    strncpy(msg.source, source, sizeof(msg.source) - 1);
    msg.source[sizeof(msg.source) - 1] = '\0';
    
    strncpy(msg.message, message, sizeof(msg.message) - 1);
    msg.message[sizeof(msg.message) - 1] = '\0';
    
    strncpy(msg.data, data, sizeof(msg.data) - 1);
    msg.data[sizeof(msg.data) - 1] = '\0';
    // ...
}
```

### Phase 3: Update Serial Logging

**Current:**
```cpp
void DebugSystem::logToSerial(const DebugMessage& message) {
    String typeStr;
    // ... type conversion ...
    Serial.printf("[DEBUG_%s] [%s] %s", typeStr.c_str(), message.source.c_str(), message.message.c_str());
    if (message.data.length() > 0) {
        Serial.printf(" | %s", message.data.c_str());
    }
}
```

**Proposed:**
```cpp
void DebugSystem::logToSerial(const DebugMessage& message) {
    const char* typeStr = getMessageTypeString(message.type);
    Serial.printf("[DEBUG_%s] [%s] %s", typeStr, message.source, message.message);
    if (strlen(message.data) > 0) {
        Serial.printf(" | %s", message.data);
    }
}

const char* DebugSystem::getMessageTypeString(DebugMessageType type) {
    switch (type) {
        case DebugMessageType::INFO: return "INFO";
        case DebugMessageType::WARNING: return "WARN";
        case DebugMessageType::ERROR: return "ERROR";
        case DebugMessageType::UART_RX: return "UART_RX";
        case DebugMessageType::UART_TX: return "UART_TX";
        case DebugMessageType::AT_COMMAND: return "AT_CMD";
        case DebugMessageType::AT_RESPONSE: return "AT_RSP";
        case DebugMessageType::GNSS_DATA: return "GNSS";
        case DebugMessageType::CELLULAR_STATUS: return "CELL";
        case DebugMessageType::SYSTEM_STATUS: return "SYS";
        default: return "UNKNOWN";
    }
}
```

### Phase 4: Update Statistics Functions

**Current:**
```cpp
String DebugSystem::getDebugSummary() const {
    String summary = "Debug System Summary:\n";
    summary += "CATM Module: " + String(catmStats.isOperational ? "ONLINE" : "OFFLINE") + "\n";
    // ... more String concatenation ...
    return summary;
}
```

**Proposed:**
```cpp
void DebugSystem::getDebugSummary(char* buffer, size_t bufferSize) const {
    snprintf(buffer, bufferSize,
        "Debug System Summary:\n"
        "CATM Module: %s\n"
        "GNSS Module: %s\n"
        "Total Messages: %u\n"
        "CATM UART: RX:%u TX:%u\n"
        "GNSS UART: RX:%u TX:%u",
        catmStats.isOperational ? "ONLINE" : "OFFLINE",
        gnssStats.isOperational ? "ONLINE" : "OFFLINE",
        totalMessages,
        catmUART.messagesReceived, catmUART.messagesTransmitted,
        gnssUART.messagesReceived, gnssUART.messagesTransmitted
    );
}
```

## Implementation Steps

### Step 1: Update Header File
1. Modify `DebugMessage` struct in `include/debug_system.h`
2. Update function signatures to use `const char*`
3. Add helper function declarations

### Step 2: Update Implementation
1. Modify all logging functions in `src/debug_system.cpp`
2. Update serial logging functions
3. Replace String operations with fixed buffer operations

### Step 3: Update Callers
1. Update all calls to debug functions
2. Replace String literals with char arrays where needed
3. Update macro definitions in header

### Step 4: Testing
1. Test debug logging functionality
2. Monitor heap usage during extended operation
3. Verify no memory leaks or fragmentation

## Benefits

### Memory Efficiency
- **Eliminated 300 String allocations** from ring buffer
- **Fixed memory footprint** per debug message
- **No heap fragmentation** from String operations

### Performance
- **Faster message creation** (no dynamic allocation)
- **Reduced memory overhead** (no String object overhead)
- **Better cache locality** with fixed-size structures

### Reliability
- **Predictable memory usage** in embedded environment
- **No risk of heap exhaustion** from debug logging
- **Stable operation** during extended runtime

## Migration Considerations

### Backward Compatibility
- Update all debug macro calls to use string literals
- Ensure existing code compiles with new signatures
- Test all debug logging paths

### Buffer Size Limits
- Source: 24 chars (module names)
- Message: 48 chars (log messages)
- Data: 96 chars (data payload)
- Truncate longer strings with ellipsis if needed

### Error Handling
- Ensure null termination of all strings
- Handle truncation gracefully
- Add bounds checking for buffer operations

## Alternative: Hybrid Approach

If some messages need longer data fields:
```cpp
struct DebugMessage {
    DebugMessageType type;
    uint32_t timestamp;
    char source[24];
    char message[48];
    char data[96];
    char* longData;        // For messages > 96 chars
    uint32_t messageId;
};
```

But this adds complexity and potential for memory leaks, so fixed-size approach is preferred.
