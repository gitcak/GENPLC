# MQTT Bidirectional Command System

## Overview

This document describes the implementation of a bidirectional MQTT command system for the StamPLC GPS tracker using the SIM7080G CatM+GNSS module. The system allows remote control and data retrieval from the device while maintaining optimal GPS tracking performance.

## Architecture

### System Components

```
+-------------------------------------------------------------+
|                      MQTT Broker                             |
|                  (AWS IoT, HiveMQ, etc.)                     |
+--------------+-------------------------+--------------------+
               |                         |
               | Subscribe               | Publish
               | (Commands)              | (Data/Responses)
               |                         |
+--------------v-------------------------v--------------------+
|                    StamPLC Device                            |
|  +------------------------------------------------------+   |
|  |              GPS Module (SIM7080G)                   |   |
|  |  - Continuous GPS tracking (25-28s windows)          |   |
|  |  - Brief pause during cellular operations (2-5s)     |   |
|  +------------------------------------------------------+   |
|  +------------------------------------------------------+   |
|  |         Cellular Module (SIM7080G)                   |   |
|  |  - MQTT publish (GPS data, stats, logs)              |   |
|  |  - MQTT subscribe (receive commands)                 |   |
|  |  - Command execution                                 |   |
|  +------------------------------------------------------+   |
|  +------------------------------------------------------+   |
|  |         Command Processor                            |   |
|  |  - Parse incoming commands                           |   |
|  |  - Queue execution                                   |   |
|  |  - Send responses                                    |   |
|  +------------------------------------------------------+   |
+--------------------------------------------------------------+
```

### Topic Structure

**Device publishes to:**
- `v1/devices/me/telemetry` – Streaming telemetry (GPS position, health metrics)
- `v1/devices/me/attributes` – Static attributes (firmware version, hardware info)

**Device subscribes to:**
- `v1/devices/me/rpc/request/+` – Incoming RPC commands

**Device responds on:**
- `v1/devices/me/rpc/response/{requestId}` – Command responses

Telemetry payloads pack the latest GPS sample and device analytics into compact JSON objects, for example:

```json
{
  "lat": 37.7749,
  "lon": -122.4194,
  "alt": 15.5,
  "speed": 12.5,
  "gps_valid": true,
  "gps_satellites": 8,
  "cellular_connected": true,
  "signal_strength": -78,
  "uptime_ms": 1704672000000
}
```

Periodic stats reuse the same telemetry topic (roughly every 60 seconds) and include extended metrics such as cumulative TX/RX bytes and operator name. Attributes are published once per connection and include firmware version, chip model, and SDK version.

## GPS/Cellular Time-Multiplexing Strategy

### Operating Cycle

The device operates in a time-multiplexed fashion to work around the SIM7080G hardware limitation (GPS and cellular cannot run simultaneously):

```
+-------------------------------------------------------------+
|              30-Second Operation Cycle                       |
+-------------------------------------------------------------+
|                                                              |
|  [GPS Active: 25-28s]        [Cellular Active: 2-5s]        |
|  +-------------------------+ +----------------------+       |
|  | - Continuous tracking   | | - Publish GPS data   |       |
|  | - Update position       | | - Check for commands |       |
|  | - Buffer data          | | - Execute commands   |       |
|  | - Maintain fix         | | - Send responses     |       |
|  +-------------------------+ +----------------------+       |
|                                                              |
|  GPS uptime: ~90-93%         Command latency: <30s          |
+-------------------------------------------------------------+
```

### Timing Breakdown

| Phase | Duration | Activities |
|-------|----------|-----------|
| GPS Collection | 25-28s | Continuous GPS tracking, position updates |
| GPS Pause | 0.5s | Graceful GPS shutdown |
| Cellular TX | 1-2s | Publish GPS data to MQTT |
| Command Check | 0.5-1s | Check for incoming commands on subscribed topic |
| Command Execute | 0-2s | Execute quick commands (longer for OTA) |
| GPS Resume | 0.5s | Re-enable GPS module |

**Total Cycle:** ~30 seconds  
**GPS Uptime:** ~90-93%  
**Command Response Latency:** Average 15s (worst-case 30s)

## Command Types

All remote actions use ThingsBoard-style RPC. Publish requests to `v1/devices/me/rpc/request/{requestId}` with the JSON body:

```json
{
  "method": "<command>",
  "params": { ... }
}
```

Responses are published by the device to `v1/devices/me/rpc/response/{requestId}` and include `method`, `id`, `status`, and either `data` or `error`.

| Method          | Description                                   | Example Params                          |
|-----------------|-----------------------------------------------|------------------------------------------|
| `get_gps`       | Returns current GNSS fix                      | `{}`                                     |
| `get_stats`     | Returns system metrics (heap, radio, GPS)     | `{}`                                     |
| `get_logs`      | Returns recent log lines                      | `{ "lines": 50, "type": "all" }`       |
| `config_update` | Updates application settings (APN, intervals) | `{ "gps_interval": 30000 }`             |
| `config_mqtt`   | Updates MQTT credentials/APN in NVS           | `{ "host": "mqtt.example.com" }`       |
| `ota_update`    | Starts OTA from given URL/version             | `{ "url": "https://...", "version": "1.1.0" }` |
| `reboot`        | Schedules a device reboot                     | `{ "delay_ms": 5000 }`                  |
| `set_interval`  | Adjusts reporting intervals                   | `{ "type": "gps_publish", "value_ms": 30000 }` |

**Example Response (`get_stats`):**

```json
{
  "method": "get_stats",
  "id": "req-12345",
  "status": "success",
  "data": {
    "uptime_ms": 3600000,
    "free_heap": 180000,
    "cellular_connected": true,
    "signal_strength": -75,
    "operator": "Soracom",
    "gps_valid": true,
    "gps_satellites": 8,
    "tx_bytes": 1048576,
    "rx_bytes": 524288,
    "firmware_version": "1.0.0"
  }
}
```

```python
# Example Python request
client.publish(
    "v1/devices/me/rpc/request/req-168930",
    json.dumps({"method": "get_logs", "params": {"lines": 100}})
)
```
## Performance Metrics

### Expected Performance
- **GPS Uptime:** 90-93%
- **Command Latency:** 15-30 seconds (average 15s)
- **GPS Fix Recovery:** 1-3 seconds after cellular window
- **MQTT Publish Success Rate:** >99%
- **Command Processing Success Rate:** >95%

### Monitoring
- Track GPS uptime percentage
- Monitor command response times
- Log cellular data usage
- Track failed command attempts

## Troubleshooting

### Commands Not Being Received
1. Check MQTT broker connection
2. Verify subscription topic matches device ID
3. Check cellular connection status
4. Review MQTT broker logs

### GPS Fix Loss
1. Verify cellular window duration (<5s)
2. Check GPS antenna connection
3. Monitor satellite count before/after cellular window
4. Increase GPS collection window if needed

### High Latency
1. Reduce cellular window interval
2. Check MQTT QoS settings
3. Verify broker network connectivity
4. Review cellular signal strength

### OTA Update Failures
1. Verify firmware URL accessibility
2. Check available flash space
3. Ensure stable cellular connection
4. Validate firmware signature/MD5

## Future Enhancements

### Planned Features
- [ ] Command batching for efficiency
- [ ] Compressed log uploads
- [ ] Geofencing alerts
- [ ] Low-power mode commands
- [ ] Diagnostic commands (signal strength, network info)
- [ ] Remote debug log level control
- [ ] GPS track recording/playback

### Performance Optimizations
- [ ] Adaptive cellular window sizing
- [ ] Priority command queue
- [ ] Background task scheduling
- [ ] Delta compression for GPS data

## References

- [SIM7080G AT Command Manual](https://www.simcom.com/product/SIM7080G.html)
- [MQTT Protocol Specification](http://docs.oasis-open.org/mqtt/mqtt/v3.1.1/mqtt-v3.1.1.html)
- [ESP32 OTA Updates](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/ota.html)

## Version History

| Version | Date | Changes |
|---------|------|---------|
| 1.0.0 | 2025-01-08 | Initial implementation |
