# MQTT Bidirectional Commands - Quick Start Guide

## Overview

This guide will help you quickly set up and test the MQTT bidirectional command system on your StamPLC GPS tracker.

## Prerequisites

- StamPLC with SIM7080G CatM+GNSS module installed
- Active cellular SIM card with data plan
- MQTT broker (can use public test broker for testing)
- Python 3.7+ with `paho-mqtt` library (for testing)

## 1. Hardware Setup

1. **Insert SIM card** into SIM7080G module
2. **Connect antennas**:
   - 4G/LTE antenna to cellular port
   - GPS antenna to GNSS port
3. **Power on** StamPLC device

## 2. Configure Device Settings

**IMPORTANT**: Settings are stored in ESP32's **NVS (Non-Volatile Storage)** flash memory using the Preferences library, **not in a JSON file**.

### Configuration Method: Add Temporary Setup Code

Add this code temporarily to [`src/main.cpp`](../src/main.cpp) inside the `setup()` function (before other initialization):

```cpp
#include <Preferences.h>

void setup() {
    // *** TEMPORARY CONFIGURATION CODE - Remove after first upload ***
    Preferences prefs;
    prefs.begin("stamplc", false);
    
    prefs.putString("mHost", "beam.soracom.io");  // Soracom Beam endpoint
    prefs.putUInt("mPort", 1883);                             // MQTT port
    prefs.putString("mUser", "k-0000");            // Soracom Beam device key ID
    prefs.putString("mPass", "HbC8wO+i1/hl4ogngeWABA==");   // Soracom Beam device key secret
    prefs.putString("apn", "soracom.io");                     // Your cellular APN
    prefs.putString("apnU", "sora");                          // APN username (optional)
    prefs.putString("apnP", "sora");                          // APN password (optional)
    
    prefs.end();
    Serial.println("OK Settings configured and saved to NVS flash!");
    // *** END OF TEMPORARY CODE - Remove after first upload ***
    
    // ... rest of your existing setup() code ...
}
```

**Configuration Steps:**
1. Replace placeholders with your actual values (MQTT broker, credentials, APN)
2. Upload firmware **once** with this code
3. Settings are permanently saved to ESP32's flash memory
4. **Remove the configuration code** from setup()
5. Upload again - settings will persist in flash!

**Alternative**: After initial MQTT connection, use the `config_update` command to change settings remotely.

## 3. Build and Upload Firmware

```bash
# Navigate to project directory
cd /path/to/GENPLC

# Build and upload (use repo's env)
pio run -e m5stack-stamps3-freertos -t upload

# Monitor serial output
pio device monitor
```

## 4. Verify Device Connection

Watch serial output for these messages:

```
[CATM_GNSS_TASK] Cellular attach succeeded
[MQTT] MQTT connected host=thingsboard.vardanetworks.com:1883
[MQTT] MQTT subscribed to v1/devices/me/rpc/request/+
```

Note your device ID (MAC address) from the subscription message.

## 5. Test Commands

### Method 1: Using Python Client

```bash
# Install dependencies
pip install paho-mqtt

# Get device ID from serial output (e.g., 3c6105abcd12)
DEVICE_ID="3c6105abcd12"

# Test GPS command
python examples/mqtt_command_client.py $DEVICE_ID --broker thingsboard.vardanetworks.com --username lfew095hy4q1adoig02i gps

# Test stats command
python examples/mqtt_command_client.py $DEVICE_ID --broker thingsboard.vardanetworks.com --username lfew095hy4q1adoig02i stats

# Monitor all messages
python examples/mqtt_command_client.py $DEVICE_ID --broker thingsboard.vardanetworks.com --username lfew095hy4q1adoig02i monitor
```

### Method 2: Using mosquitto_pub/sub

```bash
# Set device ID
DEVICE_ID="3c6105abcd12"
BROKER="thingsboard.vardanetworks.com"

# Subscribe to responses and telemetry
mosquitto_sub -h $BROKER -t "v1/devices/me/rpc/response/#" &
mosquitto_sub -h $BROKER -t "v1/devices/me/telemetry" &
mosquitto_sub -h $BROKER -t "v1/devices/me/attributes" &

# Send GPS request
mosquitto_pub -h $BROKER -t "v1/devices/me/rpc/request/test-001" \
  -m '{"method":"get_gps","params":{}}'

# Send stats request
mosquitto_pub -h $BROKER -t "v1/devices/me/rpc/request/test-002" \
  -m '{"method":"get_stats","params":{}}'
```

The quick commands above produce telemetry on `v1/devices/me/telemetry`, while static attributes are pushed once per connection on `v1/devices/me/attributes`.

## 6. Expected Behavior

### GPS Command Response

```json
{
  "cmd": "get_gps",
  "id": "test-001",
  "status": "success",
  "data": {
    "timestamp": 1704672000000,
    "valid": true,
    "latitude": 37.7749,
    "longitude": -122.4194,
    "altitude": 15.5,
    "satellites": 8,
    "hdop": 1.2
  }
}
```

### Device Stats Response

```json
{
  "cmd": "get_stats",
  "id": "test-002",
  "status": "success",
  "data": {
    "uptime_ms": 3600000,
    "free_heap": 180000,
    "cellular_connected": true,
    "signal_strength": -75,
    "gps_fix": true,
    "gps_satellites": 8,
    "firmware_version": "1.0.0"
  }
}
```

## 7. Verify GPS/Cellular Switching

Monitor serial output during operation:

```
[CATM_GNSS_TASK] GNSS enabled successfully
[CATM_GNSS_TASK] Valid fix - Lat: 37.774900, Lon: -122.419400, Sats: 8
[CATM_GNSS_TASK] Disabling GNSS before network attach
[MQTT] MQTT publish success
[MQTT] Command received: get_gps
[MQTT] Command response sent: success
[CATM_GNSS_TASK] GNSS resumed after attach attempt
```

**Key observations:**
- GPS runs continuously (~25-28 seconds)
- Brief pause during MQTT operations (~2-5 seconds)
- GPS resumes immediately after cellular window
- Commands processed during cellular window

## 8. Test Advanced Commands

### Update Configuration

```bash
python examples/mqtt_command_client.py $DEVICE_ID config \
  --gps-interval 60000 \
  --mqtt-host thingsboard.vardanetworks.com
```

### Remote Reboot

```bash
python examples/mqtt_command_client.py $DEVICE_ID reboot --delay 5000
```

### OTA Firmware Update

```bash
python examples/mqtt_command_client.py $DEVICE_ID ota \
  https://firmware.example.com/stamplc-v1.1.0.bin \
  1.1.0 \
  --md5 a1b2c3d4e5f6...
```

## Troubleshooting

### Device Not Connecting

1. **Check SIM card**:
   - Ensure SIM is inserted correctly
   - Verify data plan is active
   - Check APN settings match carrier

2. **Check cellular signal**:
   ```
   [CATM_GNSS_TASK] Cellular: Connected, Signal: -XX dBm
   ```
   - Signal should be > -100 dBm
   - Move to location with better coverage if needed

3. **Verify MQTT broker**:
   - Test broker connectivity: `mosquitto_sub -h thingsboard.vardanetworks.com -t test`
   - Check username/password if required

### Commands Not Received

1. **Check subscription**:
   ```
   [MQTT] MQTT subscribed to v1/devices/me/rpc/request/+
   ```
   
2. **Verify topic names**:
   - Command topic: `v1/devices/me/rpc/request/{requestId}`
   - Response topic: `v1/devices/me/rpc/response/{requestId}`
   - Use correct device ID/token in device credentials

3. **Check cellular window timing**:
   - Commands only received during cellular window (every ~30s)
   - Wait up to 30 seconds for response

### GPS Fix Not Acquired

1. **Check antenna**:
   - GPS antenna must be connected
   - Place antenna outdoors or near window
   - Wait 30-60 seconds for initial fix

2. **Monitor satellite count**:
   ```
   [CATM_GNSS_TASK] GNSS: Satellites: X
   ```
   - Need 4+ satellites for valid fix
   - More satellites = better accuracy

### High Command Latency

1. **Normal behavior**: 15-30 second latency is expected
2. **Caused by**: GPS/cellular time-multiplexing
3. **Optimization**: Commands queued and executed during next cellular window

## Next Steps

1. **Read full documentation**: [`MQTT_BIDIRECTIONAL_COMMANDS.md`](MQTT_BIDIRECTIONAL_COMMANDS.md)
2. **Implement server integration**: Use provided Python client as template
3. **Configure production broker**: Set up secure MQTT broker (TLS/SSL)
4. **Add custom commands**: Extend command handlers for your use case
5. **Monitor in production**: Set up logging and alerting

## Support Resources

- **Documentation**: [`docs/MQTT_BIDIRECTIONAL_COMMANDS.md`](MQTT_BIDIRECTIONAL_COMMANDS.md)
- **Example Client**: [`examples/mqtt_command_client.py`](../examples/mqtt_command_client.py)
- **SIM7080G Notes**: [`docs/SIM7080G-notes.md`](SIM7080G-notes.md)

## Quick Reference

### Common Commands

```bash
# Get GPS position
{"cmd":"get_gps","id":"req-001"}

# Get device stats
{"cmd":"get_stats","id":"req-002"}

# Get logs (50 lines)
{"cmd":"get_logs","id":"req-003","params":{"lines":50}}

# Update GPS interval
{"cmd":"set_interval","id":"req-004","params":{"type":"gps_publish","value_ms":60000}}

# Reboot device
{"cmd":"reboot","id":"req-005","params":{"delay_ms":5000}}
```

### Topic Structure

```
Device publishes to:
  v1/devices/me/telemetry       - GPS + analytics telemetry
  v1/devices/me/attributes      - Static attributes (firmware, hardware)

Device subscribes to:
  v1/devices/me/rpc/request/+   - Incoming RPC commands
```

### Timing Expectations

- **Command latency**: 15-30 seconds (average 15s)
- **GPS uptime**: 90-93%
- **Cellular window**: 2-5 seconds every 30 seconds
- **GPS recovery**: 1-3 seconds after cellular window

---

**Happy tracking! satellitepin**
