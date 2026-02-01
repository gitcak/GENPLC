# CatMGNSSModule Refactoring Plan

## Overview

This document outlines the refactoring of `CatMGNSSModule` to use the new modular `M5_SIM7080G` library while preserving GENPLC-specific features and FreeRTOS integration.

**Date:** 2026-02-01
**Status:** In Progress
**Library Version:** M5_SIM7080G v0.1.0

---

## Task List

| ID | Task | Status | Description |
|----|------|--------|-------------|
| #1 | Refactor CatMGNSSModule to use M5_SIM7080G library | pending | Main refactoring task - replace monolithic AT handling with modular classes |
| #2 | Integrate M5_SIM7080G core modem class | pending | Replace sendATCommand/waitForResponse with M5_SIM7080G::sendCommand |
| #3 | Integrate SIM7080G_Network for PDP/APN | pending | Replace configureAPN, activatePDP, ensureRegistered |
| #4 | Integrate SIM7080G_GNSS for GPS operations | pending | Replace enableGNSS, updateGNSSData, parseGNSSData |
| #5 | Add MQTT support via SIM7080G_MQTT | pending | Add MQTT client for ThingsBoard communication |
| #6 | Test build and verify functionality | pending | Compile and verify all features work |

---

## Current Architecture

### CatMGNSSModule (Monolithic - ~2100 LOC)

```
CatMGNSSModule
├── Hardware Interface
│   ├── HardwareSerial* serialModule
│   ├── SemaphoreHandle_t serialMutex
│   └── UART configuration (pins 4/5, 115200 baud)
├── AT Command Handling
│   ├── sendATCommand(String/char*, ...)
│   ├── waitForResponse(String/char*, ...)
│   └── Manual OK/ERROR parsing
├── Network Functions
│   ├── configureAPN()
│   ├── activatePDP()
│   ├── ensureRegistered()
│   ├── connectNetwork()
│   └── disconnectNetwork()
├── GNSS Functions
│   ├── enableGNSS() / disableGNSS()
│   ├── updateGNSSData()
│   ├── parseGNSSData() (2 overloads)
│   └── hasValidFix(), getSatellites()
├── Network Time (GENPLC-specific)
│   ├── configureNetworkTime()
│   ├── syncNetworkTime()
│   └── getNetworkTime()
├── Throughput Tracking (GENPLC-specific)
│   ├── updateNetworkStats()
│   ├── parseNetDevStatus()
│   └── txBytes/rxBytes/txBps/rxBps
└── HTTP Functions
    ├── sendHTTP()
    └── sendJSON()
```

### New M5_SIM7080G Library (Modular - ~850 LOC)

```
M5_SIM7080G (Core)
├── Init(), checkStatus(), wakeup()
├── sendCommand() -> AtResponse
└── flushInput(), available()

SIM7080G_Network
├── setAPN(), activatePDP(), deactivatePDP()
├── isPDPActive()
├── getSignalQuality(), getNetworkStatus()
└── waitForNetwork()

SIM7080G_GNSS
├── powerOn(), powerOff()
├── getFix() -> GNSSFix
├── setUpdateRate()
└── startNMEA(), stopNMEA(), getRawNMEA()

SIM7080G_MQTT
├── configure(), connect(), disconnect()
├── subscribe(), unsubscribe()
├── publish()
├── setCallback()
└── poll()

SIM7080G_HTTP
├── configure(), connect(), disconnect()
├── clearHeaders(), addHeader(), setHeaders()
├── get() -> HttpResponse
└── post() -> HttpResponse
```

---

## Target Architecture

### Refactored CatMGNSSModule

```cpp
class CatMGNSSModule {
private:
    // New library instances (composition)
    M5_SIM7080G* _modem;
    SIM7080G_Network* _network;
    SIM7080G_GNSS* _gnss;
    SIM7080G_MQTT* _mqtt;
    SIM7080G_HTTP* _http;

    // FreeRTOS (preserved)
    SemaphoreHandle_t serialMutex;

    // GENPLC-specific features (preserved)
    // - Network time sync
    // - Throughput tracking
    // - State machine
    // - Error tracking

public:
    // Simplified public API
    bool begin();
    void shutdown();

    // GNSS (delegates to _gnss)
    bool enableGNSS();
    bool disableGNSS();
    GNSSData getGNSSData();

    // Network (delegates to _network)
    bool connectNetwork(const String& apn);
    bool disconnectNetwork();
    bool isNetworkConnected();

    // MQTT (new capability)
    bool mqttConnect(const String& broker, uint16_t port, const String& clientId);
    bool mqttPublish(const String& topic, const String& payload);
    bool mqttSubscribe(const String& topic, MqttCallback cb);
    void mqttPoll();

    // Preserved GENPLC features
    bool syncNetworkTime(struct tm& utcOut, uint32_t timeoutMs);
    CellStatus getCellStatus();
    GnssStatus getGnssStatus();
};
```

---

## Detailed Refactoring Steps

### Task #2: Integrate Core Modem Class

**Current Code:**
```cpp
// catm_gnss_module.cpp:328-395
bool CatMGNSSModule::sendATCommand(const String& command, String& response, uint32_t timeout) {
    if (xSemaphoreTake(serialMutex, pdMS_TO_TICKS(3000)) != pdTRUE) {
        return false;
    }
    while (serialModule->available()) serialModule->read();
    serialModule->println(command);
    bool result = waitForResponse(response, timeout);
    xSemaphoreGive(serialMutex);
    return result;
}
```

**New Code:**
```cpp
// Use M5_SIM7080G with mutex wrapper
class CatMGNSSModule {
private:
    M5_SIM7080G* _modem;

    // Mutex-protected wrapper
    M5_SIM7080G::AtResponse sendCommand(const SIM7080G_String& cmd, uint32_t timeout = 1000) {
        if (xSemaphoreTake(serialMutex, pdMS_TO_TICKS(3000)) != pdTRUE) {
            return {M5_SIM7080G::Status::TransportError, ""};
        }
        auto result = _modem->sendCommand(cmd, timeout);
        xSemaphoreGive(serialMutex);
        return result;
    }
};
```

**Benefits:**
- Cleaner response handling with `AtResponse` struct
- Built-in OK/ERROR/TIMEOUT detection
- Reduced code duplication

---

### Task #3: Integrate Network Class

**Current Code:**
```cpp
// catm_gnss_module.cpp:940-1012
bool CatMGNSSModule::activatePDP(uint32_t timeoutMs) {
    String response;
    for (int attempt = 1; attempt <= MAX_RETRIES; attempt++) {
        if (sendATCommand("AT+CNACT=0,1", response, 10000)) {
            if (response.indexOf("OK") >= 0) {
                // Check for IP
                if (sendATCommand("AT+CNACT?", response, 3000)) {
                    if (response.indexOf("+CNACT: 0,1") >= 0) {
                        return true;
                    }
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
    return false;
}
```

**New Code:**
```cpp
bool CatMGNSSModule::activatePDP(uint32_t timeoutMs) {
    MutexGuard guard(serialMutex);
    if (!guard.acquired()) return false;

    return _network->activatePDP(0);  // profileId = 0
}

bool CatMGNSSModule::connectNetwork(const String& apn) {
    MutexGuard guard(serialMutex);
    if (!guard.acquired()) return false;

    if (!_network->setAPN(apn, apnUser_, apnPass_)) {
        return false;
    }

    if (!_network->waitForNetwork(60000)) {
        return false;
    }

    return _network->activatePDP(0);
}
```

---

### Task #4: Integrate GNSS Class

**Current Code:**
```cpp
// catm_gnss_module.cpp:567-663
bool CatMGNSSModule::parseGNSSData(const String& data) {
    int startIdx = data.indexOf("+CGNSINF: ");
    if (startIdx < 0) return false;

    // Manual field-by-field parsing (100+ lines)
    // ...
    gnssData.latitude = lat;
    gnssData.longitude = lon;
    // ...
}
```

**New Code:**
```cpp
bool CatMGNSSModule::updateGNSSData() {
    MutexGuard guard(serialMutex);
    if (!guard.acquired()) return false;

    sim7080g::GNSSFix fix = _gnss->getFix(2000);

    if (fix.valid) {
        gnssData.latitude = fix.latitude;
        gnssData.longitude = fix.longitude;
        gnssData.altitude = fix.altitude_m;
        gnssData.speed = static_cast<float>(fix.speed_kph);
        gnssData.course = static_cast<float>(fix.course_deg);
        gnssData.isValid = true;
        gnssData.lastUpdate = millis();

        // Parse UTC timestamp from fix.utc if needed
        parseUTCTimestamp(fix.utc);
    }

    return fix.valid;
}

bool CatMGNSSModule::enableGNSS() {
    MutexGuard guard(serialMutex);
    if (!guard.acquired()) return false;

    return _gnss->powerOn(5000);
}
```

---

### Task #5: Add MQTT Support

**New Capability:**
```cpp
// New MQTT methods in CatMGNSSModule
bool CatMGNSSModule::mqttConfigure(const String& broker, uint16_t port, const String& clientId) {
    MutexGuard guard(serialMutex);
    if (!guard.acquired()) return false;

    return _mqtt->configure(broker, port, clientId, 60, true);
}

bool CatMGNSSModule::mqttConnect(uint32_t timeout) {
    MutexGuard guard(serialMutex);
    if (!guard.acquired()) return false;

    return _mqtt->connect(timeout);
}

bool CatMGNSSModule::mqttPublish(const String& topic, const String& payload, int qos) {
    MutexGuard guard(serialMutex);
    if (!guard.acquired()) return false;

    return _mqtt->publish(topic, payload, qos, false, 10000);
}

bool CatMGNSSModule::mqttSubscribe(const String& topic, int qos) {
    MutexGuard guard(serialMutex);
    if (!guard.acquired()) return false;

    return _mqtt->subscribe(topic, qos, 3000);
}

void CatMGNSSModule::mqttPoll() {
    MutexGuard guard(serialMutex);
    if (!guard.acquired()) return;

    _mqtt->poll(0);  // Non-blocking poll
}

void CatMGNSSModule::setMqttCallback(SIM7080G_MQTT::MessageCallback cb) {
    _mqtt->setCallback(cb);
}
```

---

## Code Mapping Reference

| Current Method | New Implementation |
|----------------|-------------------|
| `sendATCommand()` | `_modem->sendCommand()` + mutex |
| `waitForResponse()` | Built into `sendCommand()` |
| `configureAPN()` | `_network->setAPN()` |
| `activatePDP()` | `_network->activatePDP()` |
| `ensureRegistered()` | `_network->waitForNetwork()` |
| `isPDPActive()` (new) | `_network->isPDPActive()` |
| `getSignalStrength()` | `_network->getSignalQuality()` |
| `enableGNSS()` | `_gnss->powerOn()` |
| `disableGNSS()` | `_gnss->powerOff()` |
| `updateGNSSData()` | `_gnss->getFix()` |
| `parseGNSSData()` | Eliminated (built into `getFix()`) |
| `sendHTTP()` | `_http->post()` / `_http->get()` |
| N/A (new) | `_mqtt->publish()`, `subscribe()`, etc. |

---

## Features to Preserve (GENPLC-specific)

These features are NOT in the new library and must be kept:

1. **Network Time Sync (NTP)**
   - `configureNetworkTime()` - AT+CLTS, AT+CNTPCID, AT+CNTP
   - `syncNetworkTime()` - AT+CNTP trigger
   - Keep existing implementation

2. **Throughput Tracking**
   - `updateNetworkStats()` - Custom AT command parsing
   - `txBytes`, `rxBytes`, `txBps`, `rxBps`
   - Keep existing implementation

3. **SIM Status Checking**
   - AT+CPIN? handling in `begin()`
   - PIN/PUK error detection
   - Keep existing implementation

4. **FreeRTOS Integration**
   - `serialMutex` protection
   - `vTaskCatMGNSS` task function
   - Keep existing patterns

5. **State Machine**
   - `CatMGNSSState` enum
   - State transitions
   - Keep existing implementation

---

## Helper Classes to Add

### MutexGuard (RAII pattern)
```cpp
class MutexGuard {
public:
    explicit MutexGuard(SemaphoreHandle_t mutex, TickType_t timeout = pdMS_TO_TICKS(3000))
        : _mutex(mutex), _acquired(false) {
        if (_mutex) {
            _acquired = (xSemaphoreTake(_mutex, timeout) == pdTRUE);
        }
    }

    ~MutexGuard() {
        if (_acquired && _mutex) {
            xSemaphoreGive(_mutex);
        }
    }

    bool acquired() const { return _acquired; }

private:
    SemaphoreHandle_t _mutex;
    bool _acquired;
};
```

---

## Testing Plan

### Unit Tests
1. Modem initialization with new library
2. AT command response parsing (OK, ERROR, TIMEOUT)
3. Network registration and PDP activation
4. GNSS fix parsing
5. MQTT connect/publish/subscribe

### Integration Tests
1. Full boot sequence with CatM module
2. Network attach and IP acquisition
3. GNSS fix acquisition
4. MQTT publish to ThingsBoard
5. NTP time sync (preserved feature)

### Regression Tests
1. Existing UI pages still display correct data
2. CellStatus/GnssStatus DTOs still work
3. Error handling and recovery
4. Throughput tracking still works

---

## Files to Modify

| File | Changes |
|------|---------|
| `src/modules/catm_gnss/catm_gnss_module.h` | Add library includes, new member variables |
| `src/modules/catm_gnss/catm_gnss_module.cpp` | Refactor methods to use library classes |
| `src/modules/catm_gnss/catm_gnss_task.cpp` | Update task to use new MQTT methods |
| `platformio.ini` | Already updated with lib_extra_dirs |

---

## Estimated Impact

- **Lines Removed:** ~800 (AT parsing, response handling)
- **Lines Added:** ~200 (library integration, MQTT support)
- **Net Reduction:** ~600 LOC
- **New Capability:** Full MQTT support with callbacks

---

## Rollback Plan

The old library backup is removed, but the current working code is in git:
```bash
git checkout HEAD -- src/modules/catm_gnss/
```

The new library can be reverted by:
```bash
git checkout HEAD -- libraries/M5_SIM7080G/
git checkout HEAD -- platformio.ini
```
