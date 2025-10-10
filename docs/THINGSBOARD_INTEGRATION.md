# ThingsBoard Integration Guide for StamPLC GPS Tracker

## Overview

This guide explains how to integrate your StamPLC GPS tracker with ThingsBoard IoT platform. ThingsBoard provides device management, dashboards, alerts, and OTA updates out of the box.

## Understanding Settings Storage

### Warning Important: No settings.json File

Your device **does NOT use a settings.json file**. Instead, settings are stored in:

**ESP32's NVS (Non-Volatile Storage)** flash memory using the Arduino `Preferences` library.

**Location**: Stored internally in ESP32's flash partition  
**Persistence**: Settings survive reboots and power cycles  
**Access**: Via [`settings_store.cpp`](../src/modules/settings/settings_store.cpp) API

### Settings Structure

From [`settings_store.h`](../src/modules/settings/settings_store.h):

```cpp
struct AppSettings {
    char wifiSsid[32];     // Not used for cellular
    char wifiPass[64];     // Not used for cellular
    char apn[48];          // Cellular APN
    char apnUser[32];      // APN username
    char apnPass[32];      // APN password
    char mqttHost[64];     // MQTT broker address
    uint16_t mqttPort;     // MQTT broker port
    char mqttUser[32];     // MQTT username/token
    char mqttPass[32];     // MQTT password
};
```

**NVS Keys** (in namespace "stamplc"):
- `apn` -> Cellular APN
- `apnU` -> APN username
- `apnP` -> APN password
- `mHost` -> MQTT broker hostname (Soracom Beam endpoint)
- `mPort` -> MQTT port (default 1883)
- `mUser` -> MQTT username (Soracom Beam device key ID)
- `mPass` -> MQTT password (Soracom Beam device key secret)

## ThingsBoard Setup

### Step 1: Complete Device Profile Creation

In ThingsBoard UI (you saw this screen):

1. **Navigate**: Device profiles -> Add device profile
2. **Name**: "StamPLC GPS Tracker"
3. **Transport Configuration**:
   - Transport type: **MQTT**
   - Telemetry topic: `v1/devices/me/telemetry`
   - Attributes publish: `v1/devices/me/attributes`  
   - Attributes subscribe: `v1/devices/me/attributes`
   - Payload: **JSON**
4. **Alarm rules**: Skip for now (Next)
5. **Device provisioning**: Disabled (Next)
6. **Click "Add"** to create

### Step 2: Create Device

1. **Navigate**: Devices -> **"+"** -> Add new device
2. **Fill in**:
   - Name: `StamPLC-001` (or use your device's MAC address)
   - Device profile: Select "StamPLC GPS Tracker"
   - Label: `GPS Tracker` (optional)
3. **Click "Add"**
4. **CRITICAL**: Copy the **Device credentials** (Access Token)
   - Example: `A1B2C3D4E5F6G7H8I9J0`
   - This is your MQTT username for authentication

### Step 3: Configure Device for ThingsBoard

Add this temporary configuration code to [`src/main.cpp`](../src/main.cpp):

```cpp
#include <Preferences.h>

void setup() {
    Serial.begin(115200);
    delay(1000);
    
    // ============================================================
    // THINGSBOARD CONFIGURATION (ONE-TIME SETUP)
    // Add this code, upload once, then remove it
    // ============================================================
    Preferences prefs;
    if (prefs.begin("stamplc", false)) {
        // MQTT Settings (Soracom Beam)
        prefs.putString("mHost", "beam.soracom.io");         // Soracom Beam endpoint
        prefs.putUInt("mPort", 1883);                        // MQTT port
        prefs.putString("mUser", "k-0000");                 // Beam device key ID
        prefs.putString("mPass", "HbC8wO+i1/hl4ogngeWABA=="); // Beam device key secret
        
        // Cellular Settings
        prefs.putString("apn", "soracom.io");                // Your carrier's APN
        prefs.putString("apnU", "sora");                     // APN user (if required)
        prefs.putString("apnP", "sora");                     // APN pass (if required)
        
        prefs.end();
        Serial.println("OKOKOK ThingsBoard settings saved to NVS flash! OKOKOK");
        Serial.println("Remove this configuration code and re-upload!");
    } else {
        Serial.println("Failed to open NVS preferences!");
    }
    // ============================================================
    // END OF TEMPORARY CONFIGURATION CODE
    // ============================================================
    
    // ... rest of your existing setup() code ...
}
```

**Replace these values if needed:**
- `beam.soracom.io` -> Custom Soracom Beam endpoint (if you use a different Beam entry)
- `k-0000` / `HbC8wO+i1/hl4ogngeWABA==` -> Device-specific Beam key ID/secret
- `soracom.io` -> Alternate cellular APN credentials if your SIM uses different values

> ⚠️ The Beam entry itself must be configured to forward to ThingsBoard (`thingsboard.vardanetworks.com:8883`) and inject the ThingsBoard access token as the upstream MQTT username.

**Configuration Process:**
1. Add code to `setup()`
2. Build and upload: `pio run --target upload` (uses default env `m5stack-stamps3-freertos` from `platformio.ini`)
3. Watch serial monitor for "OKOKOK ThingsBoard settings saved"
4. **Remove the configuration code** from `setup()`
5. Upload again - settings are now permanently stored!

## Current MQTT vs ThingsBoard Topics

### Current Implementation (Generic MQTT)

```
Device publishes to:
  v1/devices/me/telemetry       -> GPS + device telemetry
  v1/devices/me/attributes      -> Static attributes (firmware, hardware)

Device subscribes to:
  v1/devices/me/rpc/request/+   -> Incoming commands
```

### ThingsBoard Format (What We Need)

```
Device publishes to:
  v1/devices/me/telemetry       -> GPS data, stats (combined)
  v1/devices/me/attributes      -> Device metadata

Device subscribes to:
  v1/devices/me/rpc/request/+   -> RPC commands

Device responds to:
  v1/devices/me/rpc/response/{requestId}  -> RPC responses
```

## Integration Options

### Option A: Quick Test (No Code Changes)

Test basic connectivity with current implementation:

**Pros:**
- No code changes needed
- Verify connectivity first
- See data in ThingsBoard

**Cons:**
- Data won't appear correctly formatted
- Commands won't work through ThingsBoard UI
- Need full integration for RPC

**Steps:**
1. Configure NVS settings (above)
2. Upload firmware
3. Check ThingsBoard device page for "Last Activity"
4. Data will appear but not in optimal format

### Option B: Full ThingsBoard Integration (Recommended)

Adapt MQTT topics and payloads for ThingsBoard's API:

**Changes Required:**
1. Update publish topics to `v1/devices/me/telemetry`
2. Subscribe to `v1/devices/me/rpc/request/+`
3. Parse ThingsBoard RPC format
4. Publish attributes to `v1/devices/me/attributes`

**Benefits:**
- Full dashboard compatibility
- RPC commands work through UI
- Proper telemetry formatting
- Rule engine integration
- OTA updates via ThingsBoard

## ThingsBoard MQTT API Reference

### Publishing Telemetry

**Topic**: `v1/devices/me/telemetry`

**Format**:
```json
{
  "latitude": 37.7749,
  "longitude": -122.4194,
  "altitude": 15.5,
  "speed": 12.5,
  "satellites": 8,
  "gps_valid": true,
  "signal_strength": -75,
  "uptime": 3600000
}
```

### Publishing Attributes

**Topic**: `v1/devices/me/attributes`

**Format** (device metadata):
```json
{
  "firmware_version": "1.0.0",
  "imei": "123456789012345",
  "device_model": "StamPLC",
  "last_reboot": "2025-01-08T10:30:00Z"
}
```

### Server-Side RPC (Commands from ThingsBoard)

**Subscribe Topic**: `v1/devices/me/rpc/request/+`

**Incoming Format**:
```json
{
  "method": "get_gps",
  "params": {}
}
```

**Response Topic**: `v1/devices/me/rpc/response/{requestId}`

**Response Format**:
```json
{
  "latitude": 37.7749,
  "longitude": -122.4194,
  "satellites": 8,
  "valid": true
}
```

## Implementation Steps for Full Integration

### Step 1: Update MQTT Topic Configuration

Modify [`mqtt_task.cpp`](../src/modules/mqtt/mqtt_task.cpp) to use ThingsBoard topics:

```cpp
// In vTaskMQTT function:

// Change from:
String rpcRequestTopic = "v1/devices/me/rpc/request/+";
String rpcResponsePrefix = "v1/devices/me/rpc/response/";

// To:
String rpcRequestTopic = "v1/devices/me/rpc/request/+";
String telemetryTopic = "v1/devices/me/telemetry";
String attributesTopic = "v1/devices/me/attributes";
```

### Step 2: Update Telemetry Publishing

Combine GPS and stats into single telemetry message:

```cpp
// Current format (separate messages):
publish to "v1/devices/me/telemetry" with GPS + stats payload
publish to "v1/devices/me/attributes" once per connection

// ThingsBoard format (combined telemetry):
StaticJsonDocument<512> telemetry;
telemetry["ts"] = millis();  // Timestamp
telemetry["latitude"] = gps.latitude;
telemetry["longitude"] = gps.longitude;
telemetry["altitude"] = gps.altitude;
telemetry["speed"] = gps.speed;
telemetry["satellites"] = gps.satellites;
telemetry["gps_valid"] = gps.isValid;
telemetry["signal_strength"] = cell.signalStrength;
telemetry["operator"] = cell.operatorName;

publish to "v1/devices/me/telemetry"
```

### Step 3: Handle ThingsBoard RPC Requests

Parse ThingsBoard RPC format and respond appropriately:

```cpp
// When message received on "v1/devices/me/rpc/request/123":
{
  "method": "get_gps",
  "params": {}
}

// Extract requestId from topic (the "123" part)
// Execute command
// Respond to "v1/devices/me/rpc/response/123" with result data
```

### Step 4: Publish Device Attributes

Send device metadata on connection:

```cpp
StaticJsonDocument<256> attributes;
attributes["firmware_version"] = "1.0.0";
attributes["device_type"] = "StamPLC GPS Tracker";
attributes["imei"] = getIMEI();

publish to "v1/devices/me/attributes"
```

## Dashboard Creation in ThingsBoard

### Step 1: Create Dashboard

1. **Navigate**: Dashboards -> **"+"** -> Create new dashboard
2. **Name**: "GPS Tracker Fleet"
3. **Click "Add"**

### Step 2: Add Map Widget

1. **Click "+**" -> Create new widget
2. **Widget type**: Maps -> "OpenStreet Map"
3. **Datasource**: Your device
4. **Latitude key**: `latitude`
5. **Longitude key**: `longitude`
6. **Tooltip**: Show satellite count, speed
7. **Click "Add"**

### Step 3: Add Time-Series Charts

Add widgets for:
- **Signal Strength** over time
- **Satellite Count** over time
- **Speed** over time
- **Altitude** profile

### Step 4: Add Control Widgets

1. **RPC Button**: "Get Current GPS"
   - Method: `get_gps`
   - Shows response in popup

2. **RPC Button**: "Get Device Stats"
   - Method: `get_stats`
   
3. **RPC Button**: "Reboot Device"
   - Method: `reboot`
   - Params: `{"delay_ms": 5000}`

## Complete Configuration Example

### Device NVS Configuration

```cpp
// Add to setup() in main.cpp (ONE TIME ONLY)
Preferences prefs;
prefs.begin("stamplc", false);

// For ThingsBoard Community Edition (demo server)
prefs.putString("mHost", "demo.thingsboard.io");
prefs.putUInt("mPort", 1883);
prefs.putString("mUser", "A1B2C3D4E5F6G7H8I9J0");  // Your device token
prefs.putString("mPass", "");

// For ThingsBoard Professional/Cloud
// prefs.putString("mHost", "thingsboard.cloud");
// prefs.putUInt("mPort", 1883);
// prefs.putString("mUser", "your-access-token");
// prefs.putString("mPass", "");

// Cellular APN
prefs.putString("apn", "soracom.io");
prefs.putString("apnU", "sora");
prefs.putString("apnP", "sora");

prefs.end();
```

### Device Widget Configuration (ThingsBoard UI)

```json
{
  "name": "GPS Location Card",
  "type": "latest",
  "datasources": [
    {
      "type": "device",
      "deviceId": "your-device-id",
      "dataKeys": [
        {"name": "latitude", "label": "Latitude"},
        {"name": "longitude", "label": "Longitude"},
        {"name": "satellites", "label": "Satellites"},
        {"name": "gps_valid", "label": "GPS Fix"}
      ]
    }
  ]
}
```

## Testing ThingsBoard Connection

### Step 1: Upload Configured Firmware

```bash
# Add configuration code to main.cpp
# Then build and upload
pio run --target upload

# Monitor serial output
pio device monitor
```

### Step 2: Watch Serial Output

Look for these messages:

```
OKOKOK ThingsBoard settings saved to NVS flash! OKOKOK
[CATM_GNSS_TASK] Cellular attach succeeded
[MQTT] MQTT connected host=beam.soracom.io:1883
[MQTT] MQTT subscribed to v1/devices/me/rpc/request/+
[MQTT] MQTT publish success
```

### Step 3: Verify in ThingsBoard

1. **Go to**: Devices -> Your Device
2. **Check "Last Activity"**: Should show recent timestamp
3. **Click "Latest Telemetry"**: Should show GPS data
4. **Look for**:
   - `latitude`, `longitude`, `altitude`
   - `satellites`, `gps_valid`
   - `signal_strength`, `operator`

## Migration Path

### Phase 1: Basic Connectivity (Current State)

[x] Your current MQTT implementation will partially work with ThingsBoard:
- Device connects using access token
- Can publish data (though topics need adaptation)
- Basic telemetry visible in ThingsBoard

### Phase 2: Topic Adaptation (Recommended Next)

Modify topics to match ThingsBoard's API:
- Change publish topics to `v1/devices/me/telemetry`
- Subscribe to `v1/devices/me/rpc/request/+`
- Adapt RPC request/response format

### Phase 3: Full Integration

- Dashboard widgets configured
- RPC commands through UI
- Rule engine for alerts
- OTA updates via ThingsBoard

## Code Modifications Needed for Full Integration

### 1. Update mqtt_task.cpp Topics

```cpp
// Change these lines in mqtt_task.cpp:

// OLD:
String rpcRequestTopic = "v1/devices/me/rpc/request/+";
String rpcResponsePrefix = "v1/devices/me/rpc/response/";

// NEW:
String rpcRequestTopic = "v1/devices/me/rpc/request/+";
String telemetryTopic = "v1/devices/me/telemetry";
String attributesTopic = "v1/devices/me/attributes";
```

### 2. Adapt RPC Request Handling

```cpp
// ThingsBoard RPC format:
// Topic: v1/devices/me/rpc/request/123
// Payload: {"method": "get_gps", "params": {}}

// Extract requestId from topic
String requestId = topic.substring(topic.lastIndexOf('/') + 1);

// Parse method name
const char* method = doc["method"];
JsonObject params = doc["params"];

// Execute command
MQTTCommandResponse response;
if (strcmp(method, "get_gps") == 0) {
    handleGetGPS(cmd, response);
}

// Respond to: v1/devices/me/rpc/response/123
String responseTopic = "v1/devices/me/rpc/response/" + requestId;
publish(responseTopic, responseData);
```

### 3. Combined Telemetry Publishing

```cpp
// Publish combined telemetry every cycle:
StaticJsonDocument<512> telemetry;

// GPS data
GNSSData gps = catmGnssModule->getGNSSData();
telemetry["latitude"] = gps.latitude;
telemetry["longitude"] = gps.longitude;
telemetry["altitude"] = gps.altitude;
telemetry["speed"] = gps.speed;
telemetry["course"] = gps.course;
telemetry["satellites"] = gps.satellites;
telemetry["gps_valid"] = gps.isValid;
telemetry["hdop"] = gps.hdop;

// Cellular data
CellularData cell = catmGnssModule->getCellularData();
telemetry["signal_strength"] = cell.signalStrength;
telemetry["operator"] = cell.operatorName;
telemetry["cellular_connected"] = cell.isConnected;
telemetry["tx_bytes"] = cell.txBytes;
telemetry["rx_bytes"] = cell.rxBytes;

// System data
telemetry["uptime"] = millis();
telemetry["free_heap"] = ESP.getFreeHeap();

String payload;
serializeJson(telemetry, payload);
publish("v1/devices/me/telemetry", payload);
```

## Verification Checklist

### Device-Side (Serial Monitor)

- [ ] "OKOKOK ThingsBoard settings saved to NVS flash!"
- [ ] "[CATM_GNSS_TASK] Cellular attach succeeded"
- [ ] "[MQTT] MQTT connected host=beam.soracom.io:1883"
- [ ] "[MQTT] MQTT publish success"
- [ ] GPS coordinates being logged

### ThingsBoard-Side (Web UI)

- [ ] Device shows "Active" status
- [ ] "Last Activity" timestamp is recent
- [ ] Latest Telemetry shows GPS coordinates
- [ ] Signal strength visible
- [ ] Satellite count visible
- [ ] Can see device on map widget

## Troubleshooting

### Device Not Appearing in ThingsBoard

**Problem**: Device shows as "Inactive"

**Solution**:
1. Check serial monitor for connection errors
2. Verify access token is correct (copy-paste from ThingsBoard)
3. Check MQTT broker address (no http:// prefix!)
4. Verify cellular connection: "Cellular attach succeeded"
5. Check network firewall allows port 1883

### Telemetry Not Showing

**Problem**: Device connected but no telemetry data

**Solution**:
1. Verify GPS has valid fix (needs 4+ satellites)
2. Check publish topic matches ThingsBoard format
3. Review serial output for publish success/failure
4. Check ThingsBoard Latest Telemetry tab
5. Verify JSON payload format is correct

### RPC Commands Not Working

**Problem**: Can't control device from ThingsBoard

**Solution**:
1. Ensure device subscribes to `v1/devices/me/rpc/request/+`
2. Verify RPC parsing logic is correct
3. Check device responds to correct topic format
4. Review serial logs for command reception
5. Test with simple command first (`get_gps`)

## Next Steps

### Immediate (To Get Running)

1. [x] Complete ThingsBoard device profile creation
2. [x] Create device and get access token  
3. [x] Add NVS configuration code to main.cpp
4. [x] Upload firmware with configuration
5. [x] Verify device connects (check ThingsBoard)
6. [x] Remove configuration code and re-upload

### Short-Term (Basic Integration)

1. Verify GPS data appears in ThingsBoard
2. Create basic dashboard with map widget
3. Add time-series charts for key metrics
4. Test data flow for 24 hours

### Long-Term (Full Integration)

1. Implement ThingsBoard RPC topic format
2. Add dashboard control widgets
3. Configure alert rules
4. Set up OTA updates via ThingsBoard
5. Deploy to production fleet

## Reference Links

- **ThingsBoard Docs**: https://thingsboard.io/docs/
- **MQTT API**: https://thingsboard.io/docs/reference/mqtt-api/
- **RPC API**: https://thingsboard.io/docs/user-guide/rpc/
- **Dashboards**: https://thingsboard.io/docs/user-guide/dashboards/
- **Current MQTT Implementation**: [MQTT_BIDIRECTIONAL_COMMANDS.md](MQTT_BIDIRECTIONAL_COMMANDS.md)

## Summary

Your StamPLC is ready to connect to ThingsBoard! The main steps are:

1. **Get access token** from ThingsBoard device
2. **Configure NVS settings** using temporary setup code
3. **Upload firmware** (settings persist in flash)
4. **Verify connection** in ThingsBoard
5. **Create dashboard** to visualize data

The current MQTT implementation works with ThingsBoard for basic connectivity. For full integration (RPC commands through UI), you'll need to adapt the topics and RPC format as described above.

---

**Ready to configure! Let me know when you have your ThingsBoard access token and I can help with the next steps.**
