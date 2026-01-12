# SD Card Logging System

**Document Version:** 1.0  
**Last Updated:** 2025-01-08  
**Status:** Active

---

## Overview

The StampPLC firmware implements a dual-tier logging system:
1. **In-Memory Ring Buffer** - Fast, recent logs visible on UI (100 lines)
2. **SD Card Persistence** - All logs written to JSON-Lines files for long-term storage

---

## Log Storage Architecture

### Directory Structure

```
/data/
├── gnss.jsonl        # GPS position, satellites, accuracy
├── cellular.jsonl    # Network status, signal, throughput
└── system.jsonl      # Boot events, errors, state changes
```

### File Format

All logs use **JSON-Lines** format (newline-delimited JSON):
```json
{"t":12345,"lat":40.7128,"lon":-74.0060,"alt":10.5,"spd":0.0,"sat":8,"valid":true}
{"t":67890,"op":"AT&T","rssi":-75,"conn":true,"tx_bps":1200,"rx_bps":0,"tx_bytes":45678,"rx_bytes":12345}
[98765] BOOT: CatM APN loaded
```

**Benefits:**
- Easy to parse (one JSON object per line)
- Grep-friendly (search for specific timestamps or events)
- Compact storage
- Streaming-friendly (append-only)

---

## Log Types

### 1. GNSS Logs (`/data/gnss.jsonl`)

**Source:** `catm_gnss_task.cpp`  
**Frequency:** Every task iteration when fix is valid  
**Format:**
```json
{
  "t": <millis>,
  "lat": <latitude>,
  "lon": <longitude>,
  "alt": <altitude_meters>,
  "spd": <speed_kmh>,
  "sat": <satellite_count>,
  "valid": <true|false>
}
```

**Use Cases:**
- Track device movement
- Verify GPS accuracy
- Debug GNSS fix issues
- Generate location history

### 2. Cellular Logs (`/data/cellular.jsonl`)

**Source:** `catm_gnss_task.cpp`  
**Frequency:** Every task iteration  
**Format (Connected):**
```json
{
  "t": <millis>,
  "op": "<operator_name>",
  "rssi": <signal_dbm>,
  "conn": true,
  "tx_bps": <bytes_per_sec>,
  "rx_bps": <bytes_per_sec>,
  "tx_bytes": <total_sent>,
  "rx_bytes": <total_received>
}
```

**Format (Disconnected):**
```json
{
  "t": <millis>,
  "conn": false,
  "tx_bps": 0,
  "rx_bps": 0,
  "tx_bytes": 0,
  "rx_bytes": 0,
  "detach": 0
}
```

**Use Cases:**
- Monitor signal strength trends
- Track data usage
- Debug connectivity issues
- Analyze network performance

### 3. System Logs (`/data/system.jsonl`)

**Source:** All modules via `log_add()` and `logbuf_printf()`  
**Frequency:** On significant events  
**Format:**
```
[<millis>] <log_message>
```

**Example Log Entries:**
```
[1234] Booting StampPLC CatM+GNSS...
[5678] BOOT: CatM APN loaded
[9012] CatM+GNSS initialized
[12345] BOOT: CatM network attached
[23456] ERROR: CatM APN attach failed
```

**Use Cases:**
- Boot sequence tracking
- Error diagnostics
- Module initialization status
- User actions and state changes

---

## Log Rotation

### Automatic Rotation

Files are automatically rotated when they exceed **512 KB**:

```
/data/gnss.jsonl       (current)
/data/gnss.jsonl.1     (backup - previous rotation)
/data/cellular.jsonl   (current)
/data/cellular.jsonl.1 (backup)
/data/system.jsonl     (current)
/data/system.jsonl.1   (backup)
```

**Rotation Process:**
1. Check file size before append
2. If > 512 KB:
   - Delete `.1` backup (if exists)
   - Rename current to `.1`
   - Create new empty file

**Storage Impact:**
- Max 1 MB per log type (current + backup)
- Total: ~3 MB for all logs
- Typical SD card: 4-32 GB (0.01-0.1% usage)

---

## Implementation Details

### Log Buffer (RAM)

**Location:** `src/modules/logging/log_buffer.cpp`

**Capacity:** 100 lines × 160 chars = 16 KB RAM

**API:**
```cpp
void log_add(const char* line);              // Add timestamped log
void logbuf_printf(const char* fmt, ...);    // Printf-style logging
size_t log_count();                          // Get line count
bool log_get_line(size_t idx, char* out, size_t sz);  // Read line
```

**Behavior:**
- Rolling buffer (oldest lines overwritten)
- Thread-safe (mutex-protected)
- Timestamped automatically
- Visible on Logs page in UI
- **NEW:** Also pushes to SD storage queue

### Storage Task

**Location:** `src/modules/storage/storage_task.cpp`

**Queue:** 64-entry FIFO for `LogRecord` structs

**Thread:** Dedicated FreeRTOS task on Core 1

**Behavior:**
- Waits for log records via queue
- Routes by type (GNSS/CELL/SYSTEM)
- Mutex-protected SD writes
- Non-blocking from logger perspective

---

## Performance Characteristics

| Metric | Value | Notes |
|--------|-------|-------|
| RAM Usage | 16 KB | In-memory ring buffer |
| SD Write Frequency | ~1-5 Hz | Depends on GPS fix rate |
| Queue Depth | 64 entries | ~16 KB queue capacity |
| Max Log Line | 256 bytes | Truncated if longer |
| Rotation Threshold | 512 KB | Per-file limit |
| Max SD Usage | ~3 MB | With rotation (6 files) |

---

## Usage Examples

### Adding System Logs

```cpp
// Simple text log
log_add("System initialized");

// Printf-style log
logbuf_printf("Temperature: %.1f C", temp);
```

**Result:**
- Appears in RAM buffer (visible on Logs page)
- Written to `/data/system.jsonl` on SD card

### Adding Telemetry Logs

```cpp
// GPS telemetry (from catm_gnss_task.cpp)
LogRecord rec;
rec.type = LogRecord::GNSS;
StaticJsonDocument<256> doc;
doc["t"] = millis();
doc["lat"] = gnssData.latitude;
doc["lon"] = gnssData.longitude;
serializeJson(doc, rec.line, sizeof(rec.line));
xQueueSend(g_storageQ, &rec, pdMS_TO_TICKS(50));
```

**Result:**
- Written to `/data/gnss.jsonl`
- Not added to RAM buffer (telemetry-only)

---

## Reading Logs

### From SD Card (USB)

1. Power off device
2. Remove SD card
3. Insert into computer
4. Navigate to `/data/` directory
5. Open `.jsonl` files with text editor or JSON viewer

### From UI (Logs Page)

- Shows last 100 system log entries
- Scrollable with B/C buttons
- Real-time updates

### Via Serial Monitor

All logs are also printed to serial output (`Serial.printf()`):
```
[12345] Booting StampPLC CatM+GNSS...
CatM+GNSS: Configuring Grove Port C (RX:4 TX:5) for UART
CatM+GNSS: AT OK on Grove Port C
```

---

## Troubleshooting

### Logs Not Written to SD Card

**Check:**
1. SD card inserted and mounted
2. `/data/` directory exists
3. Storage task running: `HWM words | Storage:<value>`
4. Queue not full: Increase queue depth if drops occur

**Debug:**
```cpp
Serial.printf("Storage queue: %d/%d\n", 
              uxQueueMessagesWaiting(g_storageQ), 64);
```

### SD Card Full

**Symptoms:** Write test fails, logs stop updating

**Solutions:**
1. Reduce rotation threshold (512 KB → 256 KB)
2. Increase rotation (keep `.1`, `.2`, `.3` backups)
3. Use larger SD card (8 GB minimum recommended)

### Log Corruption

**Cause:** SD card removed during write operation

**Prevention:**
- Always power off before removing SD card
- Mutex ensures atomic writes per line
- No partial JSON objects written

---

## Configuration Options

### Log Buffer Size

**Location:** `include/log_buffer.h`

```cpp
#define LOG_BUFFER_CAPACITY 100   // Number of lines in RAM
#define LOG_BUFFER_LINE_LEN 160   // Max chars per line
```

**Trade-offs:**
- Increase capacity → more RAM usage, more history visible
- Decrease capacity → less RAM usage, less history visible

### Storage Queue Size

**Location:** `src/modules/storage/storage_task.cpp:40`

```cpp
g_storageQ = xQueueCreate(64, sizeof(LogRecord));
```

**Trade-offs:**
- Increase queue → more buffering during SD write delays
- Decrease queue → lower RAM usage, more dropped logs

### Rotation Threshold

**Location:** `src/modules/storage/storage_task.cpp:28`

```cpp
sd_rotate_if_big(path, 512 * 1024);  // 512 KB
```

**Recommendations:**
- Small SD cards: 256 KB
- Standard usage: 512 KB (current)
- Long-term logging: 1 MB

---

## Integration Status

| Module | Logging Status | File |
|--------|----------------|------|
| GNSS | ✅ Active | `/data/gnss.jsonl` |
| Cellular | ✅ Active | `/data/cellular.jsonl` |
| System Events | ✅ Active | `/data/system.jsonl` |
| MQTT | ⚠️ Not yet implemented | TBD |
| Sensors | ⚠️ Not yet implemented | TBD |
| CAN/PWRCAN | ⚠️ Not yet implemented | TBD |

---

## Future Enhancements

1. **Compression:** gzip old log files to save space
2. **Upload:** Send logs to cloud via MQTT on cellular connect
3. **Analytics:** Parse logs for statistics (average signal, GPS accuracy)
4. **Filtering:** Configurable log levels (DEBUG/INFO/WARN/ERROR)
5. **Remote Access:** Download logs via web interface

---

## Summary

The StampPLC firmware now provides comprehensive logging:
- **RAM Buffer:** Fast, UI-visible, 100 recent lines
- **SD Card:** Persistent, unlimited history (with rotation)
- **Three Streams:** GNSS telemetry, cellular telemetry, system events
- **Auto-Format:** SD card initialized on first boot
- **No User Action:** Logging happens automatically in background

All `log_add()` calls are now persisted to `/data/system.jsonl` for post-analysis.

---

**For log analysis tools or questions, contact the firmware team.**
