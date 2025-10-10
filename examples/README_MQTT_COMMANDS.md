# MQTT Command Examples

This directory contains example client applications for interacting with the StamPLC GPS tracker's MQTT bidirectional command system.

## Available Examples

### Python MQTT Client
**File**: [`mqtt_command_client.py`](mqtt_command_client.py)

A complete Python command-line client for sending commands and monitoring device responses.

**Features**:
- All command types supported (GPS, stats, logs, config, OTA, reboot)
- Live telemetry subscriptions (GPS, stats, status, logs)
- Request/response tracking
- Live monitoring mode
- Configurable broker settings
- Command-line interface

**Requirements**:
```bash
pip install paho-mqtt
```

**Quick Start**:
```bash
# Get your device ID from serial output (e.g., 3c6105abcd12)
DEVICE_ID="3c6105abcd12"

# Get current GPS position
python mqtt_command_client.py $DEVICE_ID gps

# Get device statistics
python mqtt_command_client.py $DEVICE_ID stats

# Monitor all device messages
python mqtt_command_client.py $DEVICE_ID monitor

# Remote reboot
python mqtt_command_client.py $DEVICE_ID reboot --delay 5000

# Update configuration
python mqtt_command_client.py $DEVICE_ID config --apn soracom.io
```

> Defaults: `thingsboard.vardanetworks.com:1883` with username `lfew095hy4q1adoig02i`. Override with `--broker`, `--username`, or `--password` if you need different credentials.

**Help**:
```bash
python mqtt_command_client.py --help
python mqtt_command_client.py <device_id> <command> --help
```

## Example Use Cases

### 1. GPS Tracking Dashboard

Monitor device location in real-time:

```python
from mqtt_command_client import StamPLCClient
import time

client = StamPLCClient("3c6105abcd12", "thingsboard.vardanetworks.com")
client.connect()

while True:
    response = client.get_gps()
    if response and response['status'] == 'success':
        data = response['data']
        print(f"Location: {data['latitude']}, {data['longitude']}")
        print(f"Satellites: {data['satellites']}, Valid: {data['valid']}")
    time.sleep(30)
```

### 2. Fleet Management

Query multiple devices:

```python
devices = ["3c6105abcd12", "3c6105abcd34", "3c6105abcd56"]

for device_id in devices:
    client = StamPLCClient(device_id, "thingsboard.vardanetworks.com")
    client.connect()
    
    stats = client.get_stats()
    gps = client.get_gps()
    
    print(f"Device {device_id}:")
    print(f"  Signal: {stats['data']['signal_strength']} dBm")
    print(f"  GPS Fix: {gps['data']['valid']}")
    print(f"  Location: {gps['data']['latitude']}, {gps['data']['longitude']}")
    
    client.disconnect()
```

### 3. Remote Configuration Management

Update device settings remotely:

```python
client = StamPLCClient("3c6105abcd12", "thingsboard.vardanetworks.com")
client.connect()

# Update multiple settings
config = {
    "gps_interval": 60000,  # 60 seconds
    "mqtt_host": "new-thingsboard.vardanetworks.com",
    "apn": "new-apn.carrier.com"
}

response = client.update_config(config)
if response['data']['restart_required']:
    print("Restarting device to apply changes...")
    client.reboot(delay_ms=5000)
```

### 4. Automated Firmware Updates

Deploy OTA updates to devices:

```python
firmware_url = "https://firmware.example.com/stamplc-v1.1.0.bin"
firmware_version = "1.1.0"
firmware_md5 = "a1b2c3d4e5f6..."

client = StamPLCClient("3c6105abcd12", "thingsboard.vardanetworks.com")
client.connect()

print(f"Starting OTA update to version {firmware_version}...")
response = client.ota_update(firmware_url, firmware_version, firmware_md5)

if response and response['status'] == 'success':
    print("Firmware updated successfully!")
    print("Device will reboot automatically...")
else:
    print(f"OTA update failed: {response.get('error', 'Unknown error')}")
```

### 5. Health Monitoring

Continuous device health monitoring:

```python
import time
from datetime import datetime

client = StamPLCClient("3c6105abcd12", "thingsboard.vardanetworks.com")
client.connect()

while True:
    stats = client.get_stats()
    
    if stats and stats['status'] == 'success':
        data = stats['data']
        timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        
        print(f"[{timestamp}] Device Health:")
        print(f"  Uptime: {data['uptime_ms'] / 1000 / 60:.1f} minutes")
        print(f"  Free Heap: {data['free_heap'] / 1024:.1f} KB")
        print(f"  Cellular: {'Connected' if data['cellular_connected'] else 'Disconnected'}")
        print(f"  Signal: {data['signal_strength']} dBm")
        print(f"  GPS Fix: {'Yes' if data['gps_fix'] else 'No'}")
        print(f"  Satellites: {data['gps_satellites']}")
        
        # Alert on low signal
        if data['signal_strength'] < -95:
            print("  WARNING: Low cellular signal!")
        
        # Alert on low memory
        if data['free_heap'] < 100000:
            print("  WARNING: Low memory!")
    
    time.sleep(60)  # Check every minute
```

## Command Reference

### Available Commands

| Command | Description | Example |
|---------|-------------|---------|
| `gps` | Get current GPS position | `python mqtt_command_client.py <id> gps` |
| `stats` | Get device statistics | `python mqtt_command_client.py <id> stats` |
| `logs` | Get recent log entries | `python mqtt_command_client.py <id> logs --lines 50` |
| `config` | Update configuration | `python mqtt_command_client.py <id> config --apn soracom.io` |
| `ota` | Perform firmware update | `python mqtt_command_client.py <id> ota <url> <version>` |
| `reboot` | Reboot device | `python mqtt_command_client.py <id> reboot --delay 5000` |
| `interval` | Set reporting interval | `python mqtt_command_client.py <id> interval gps_publish 60000` |
| `monitor` | Monitor all messages | `python mqtt_command_client.py <id> monitor` |

### Command Options

#### GPS Command
```bash
python mqtt_command_client.py <device_id> gps
```
No additional options.

#### Stats Command
```bash
python mqtt_command_client.py <device_id> stats
```
No additional options.

#### Logs Command
```bash
python mqtt_command_client.py <device_id> logs [--lines N]
```
- `--lines N`: Number of log lines to retrieve (default: 50)

#### Config Command
```bash
python mqtt_command_client.py <device_id> config \
  [--gps-interval MS] \
  [--mqtt-host HOST] \
  [--mqtt-port PORT] \
  [--apn APN]
```
- `--gps-interval`: GPS reporting interval in milliseconds
- `--mqtt-host`: MQTT broker hostname
- `--mqtt-port`: MQTT broker port
- `--apn`: Cellular APN

#### OTA Command
```bash
python mqtt_command_client.py <device_id> ota <url> <version> [--md5 HASH]
```
- `url`: Firmware binary URL
- `version`: Firmware version string
- `--md5`: Optional MD5 hash for validation

#### Reboot Command
```bash
python mqtt_command_client.py <device_id> reboot [--delay MS]
```
- `--delay`: Delay before reboot in milliseconds (default: 5000)

#### Interval Command
```bash
python mqtt_command_client.py <device_id> interval <type> <value>
```
- `type`: Interval type (`gps_publish` or `stats_publish`)
- `value`: Interval value in milliseconds

#### Monitor Command
```bash
python mqtt_command_client.py <device_id> monitor
```
Continuously monitors all device topics. Press Ctrl+C to stop.

## Creating Your Own Client

### Basic Structure

```python
from mqtt_command_client import StamPLCClient

# Create client
client = StamPLCClient(
    device_id="3c6105abcd12",
    broker="thingsboard.vardanetworks.com",
    port=1883,
    username="optional_username",
    password="optional_password"
)

# Connect to broker
if client.connect():
    # Send command
    response = client.send_command("get_gps")
    
    # Handle response
    if response and response['status'] == 'success':
        print(f"GPS Data: {response['data']}")
    
    # Disconnect
    client.disconnect()
```

### Custom Command Handler

```python
def custom_command_handler(client, cmd_type, params):
    """Send custom command and handle response"""
    response = client.send_command(cmd_type, params, wait_response=True, timeout=30)
    
    if not response:
        print("Command timeout or failed")
        return None
    
    if response['status'] == 'success':
        print(f"Command succeeded: {response['data']}")
        return response['data']
    else:
        print(f"Command failed: {response.get('error', 'Unknown error')}")
        return None
```

## Testing

### Local Testing with Mosquitto

```bash
# Start local mosquitto broker
mosquitto -v

# In terminal 1: Subscribe to RPC responses
mosquitto_sub -h localhost -t "v1/devices/me/rpc/response/#" -v

# In terminal 2: Send command
python mqtt_command_client.py <device_id> --broker localhost gps
```

### Production Testing

```bash
# Test against production broker
python mqtt_command_client.py <device_id> \
  --broker thingsboard.vardanetworks.com \
  --username prod_user \
  --password prod_pass \
  stats
```

## Troubleshooting

### Connection Issues

**Problem**: Client fails to connect to broker
```
Connection failed with code: 5
```

**Solution**:
- Verify broker address and port
- Check username/password if authentication enabled
- Ensure broker allows connections from your IP
- Check firewall settings

### Command Timeout

**Problem**: Commands timeout without response
```
[TIMEOUT] No response received for req-12345
```

**Solution**:
- Device may be offline - check cellular connection
- Commands take 15-30 seconds - increase timeout if needed
- Verify device is subscribed to command topic
- Check broker logs for message delivery

### JSON Parse Errors

**Problem**: Failed to parse command response
```
[ERROR] Failed to parse JSON
```

**Solution**:
- Check MQTT broker message format
- Verify device firmware is up to date
- Enable verbose logging to see raw messages

## Documentation

- **Full Documentation**: [`../docs/MQTT_BIDIRECTIONAL_COMMANDS.md`](../docs/MQTT_BIDIRECTIONAL_COMMANDS.md)
- **Quick Start Guide**: [`../docs/MQTT_COMMANDS_QUICK_START.md`](../docs/MQTT_COMMANDS_QUICK_START.md)
- **Implementation Summary**: [`../docs/MQTT_COMMANDS_IMPLEMENTATION_SUMMARY.md`](../docs/MQTT_COMMANDS_IMPLEMENTATION_SUMMARY.md)

## Contributing

To add new example clients:

1. Create new file in `examples/` directory
2. Document usage in this README
3. Follow existing code style and patterns
4. Include error handling and timeouts
5. Add example use cases

---

**Need help?** Check the [troubleshooting guide](../docs/MQTT_COMMANDS_QUICK_START.md#troubleshooting) or review the [full documentation](../docs/MQTT_BIDIRECTIONAL_COMMANDS.md).
