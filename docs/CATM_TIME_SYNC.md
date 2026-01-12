# CatM Network Time Synchronization

**Document Version:** 1.0  
**Last Updated:** 2025-01-08  
**Applicable Firmware:** StampPLC CatM+GNSS Module

---

## Overview

The StampPLC firmware implements automatic time synchronization via the SIM7080G CatM module using Soracom's NTP infrastructure. This provides accurate UTC time without requiring WiFi connectivity or external NTP servers.

## Architecture

### Time Sync Flow

```
1. Module Init (begin())
   └─> applyBaselineConfig()
       └─> Configure AT+CLTS=1 (network time latch)
       └─> Configure AT+CNTPCID=1 (bind NTP to PDP context 1)
       └─> Configure AT+CNTP="ntp.soracom.io",0 (Soracom NTP server)
   └─> configureNetworkTime()
       └─> Retry each command 3x with 500ms backoff

2. Network Attach
   └─> Task monitors isNetworkConnected()
   └─> On PDP activation (connect flip):
       └─> syncNetworkTime(utcTime, 65000ms)
           └─> Clear UART buffer
           └─> Send AT+CNTPSTART
           └─> Block for +CNTP: URC (up to 65s)
           └─> Parse UTC timestamp
           └─> Cache in cellularData.lastUpdate

3. High-Level RTC Sync
   └─> fetchNtpTimeViaCellular()
       └─> PRIMARY: Try CNTP sync (fast, ~5-15s)
       └─> FALLBACK: HTTP worldtimeapi.org (slow, ~30s+)
   └─> maybeUpdateTimeZoneFromCellular()
       └─> Only triggers after successful CNTP sync
       └─> Reads +CCLK? timezone offset
```

## Hardware Configuration

**Grove Port C on StampPLC:**
- **Pin 4 (White/G4):** UART RX ← CatM TX
- **Pin 5 (Yellow/G5):** UART TX → CatM RX
- **Baud Rate:** 115200 8N1
- **Module:** M5Unit-CatM+GNSS (SIM7080G)

**Important:** All CatM/GNSS traffic uses Grove Port C exclusively.

## AT Command Sequence

### Initialization (Module Begin)

```
AT+CMEE=2           // Enable verbose error codes
AT+CFUN=1           // Set full functionality
AT+CMNB=1           // Prefer CatM
AT+CNMP=38          // LTE-M/NB-IoT
AT+COPS=0           // Auto operator selection

AT+CLTS=1           // Enable network time latch (NITZ)
AT+CNTPCID=1        // Bind CNTP to PDP context 1
AT+CNTP="ntp.soracom.io",0  // Configure Soracom NTP, GMT offset 0
```

### Runtime Sync (After PDP Attach)

```
AT+CNTPSTART        // Trigger NTP sync

// URC Response (async, ~5-15 seconds):
+CNTP: 1,"2025/01/08,18:45:32"  // Success (result code 1)
+CNTP: 2,"..."                   // Network error
+CNTP: 3,"..."                   // DNS resolution error
+CNTP: 4,"..."                   // Timeout
```

### Timezone Query (Optional)

```
AT+CCLK?
+CCLK: "25/01/08,18:45:32-20"   // -20 = UTC-5:00 (quarters)
```

## Result Codes

| Code | Meaning | Action |
|------|---------|--------|
| 1 | Success | Parse timestamp, update RTC |
| 2 | Network error | Retry after backoff or use HTTP fallback |
| 3 | DNS resolution failed | Check APN/DNS config, use HTTP fallback |
| 4 | NTP timeout | Retry or use HTTP fallback |

## Field Deployment Notes

### Critical Requirements

1. **SIM Card:** Must be activated and registered with Soracom (or compatible CatM carrier)
2. **Antenna:** External CatM antenna must be connected
3. **Registration Time:** Allow 30-90 seconds for initial network registration
4. **PDP Activation:** Time sync triggers automatically after first successful PDP attach
5. **NTP Timeout:** Allow up to 65 seconds for CNTP response (default timeout)

### Troubleshooting

**No +CNTP Response:**
- Check `isNetworkConnected()` status
- Verify PDP context is active: `AT+CNACT?`
- Confirm CNTP config: `AT+CNTP?`
- Check Soracom SIM data allowance

**Result Code 2 (Network Error):**
- Weak signal (check RSSI with `AT+CSQ`)
- PDP context dropped during sync
- Carrier blocking NTP port 123

**Result Code 3 (DNS Error):**
- APN DNS configuration issue
- Try alternate NTP: `AT+CNTP="time.nist.gov",0`

**Result Code 4 (Timeout):**
- Network congestion
- Increase timeout in `syncNetworkTime()` call
- Fall back to HTTP method

### Serial Monitor Indicators

**Successful Flow:**
```
CatM+GNSS: Configuring Grove Port C (RX:4 TX:5) for UART
CatM+GNSS: AT OK on Grove Port C
CatM+GNSS: Baseline config...
CatM+GNSS: Network time latch enabled (AT+CLTS=1)
CatM+GNSS: CNTP bound to PDP context 1 (AT+CNTPCID=1)
CatM+GNSS: Soracom NTP server configured (AT+CNTP)
CatM+GNSS: Network time configuration complete
CatM+GNSS: Module initialized successfully

[CATM_GNSS_TASK] PDP reattached
[CATM_GNSS_TASK] Network connected, syncing NTP time...
CatM+GNSS: Starting NTP sync (timeout 65000 ms)...
CatM+GNSS: >>> AT+CNTPSTART
CatM+GNSS: <<< +CNTP: 1,"2025/01/08,18:45:32"
CatM+GNSS: NTP sync success - 2025/01/08 18:45:32 UTC
[CATM_GNSS_TASK] NTP sync success, UTC epoch: 1736361932

NTP: Attempting CNTP sync via Soracom NTP...
NTP: CNTP sync success - 2025-01-08 18:45:32 UTC
```

**Failure + Fallback Flow:**
```
CatM+GNSS: Starting NTP sync (timeout 65000 ms)...
CatM+GNSS: >>> AT+CNTPSTART
CatM+GNSS: <<< +CNTP: 2
CatM+GNSS: ERROR - NTP sync failed - network error (result code 2)
[CATM_GNSS_TASK] NTP sync failed: NTP sync failed - network error (result code 2)

NTP: Attempting CNTP sync via Soracom NTP...
NTP: CNTP sync failed (NTP sync failed - network error (result code 2)), falling back to HTTP...
NTP: Fetched UTC time via HTTP fallback: 2025-01-08 18:45:40
```

## Performance Characteristics

| Method | Typical Time | Data Usage | Reliability |
|--------|--------------|------------|-------------|
| CNTP (Primary) | 5-15 seconds | ~256 bytes | High (direct NTP) |
| HTTP Fallback | 20-40 seconds | ~2-4 KB | Medium (API dependent) |
| NITZ (Passive) | 0-30 seconds | 0 bytes | Variable (carrier dependent) |

## Code Integration Points

### Module Files
- `src/modules/catm_gnss/catm_gnss_module.h` - Method declarations
- `src/modules/catm_gnss/catm_gnss_module.cpp`:
  - `applyBaselineConfig()` - Initial CNTP setup (line ~236)
  - `configureNetworkTime()` - Reconfigurable CNTP setup (line ~1557)
  - `syncNetworkTime()` - Execute sync and parse response (line ~1616)
  - `taskFunction()` - Automatic sync on PDP attach (line ~1835)

### Main Application
- `src/main.cpp`:
  - `fetchNtpTimeViaCellular()` - High-level time fetch with fallback (line ~250)
  - `maybeUpdateTimeZoneFromCellular()` - TZ update gated by CNTP success (line ~368)
  - `g_cntpSyncedThisSession` - Flag tracking successful CNTP sync (line ~236)

## Maintenance Notes

### String Pool Usage
All AT response checks use `POOL_STRING("OK")` to maintain flat RAM usage.

### Error Propagation
All failures populate `lastError_` string accessible via `getLastError()`.

### State Caching
- UTC timestamp cached in `cellularData.lastUpdate` as epoch seconds
- Accessible via `getCellularData().lastUpdate`
- Updated only on successful CNTP sync

### Retry Logic
- Initial config: 3 retries per command with 500ms backoff
- Runtime sync: Single attempt (65s timeout), falls back to HTTP if failed
- PDP reconnect triggers fresh sync attempt

## Best Practices

1. **Allow Registration:** Don't power-cycle the modem within 2 minutes of boot
2. **Monitor RSSI:** Weak signal (<-100 dBm) may cause CNTP timeouts
3. **Check Logs:** Serial output shows full AT command flow for debugging
4. **Test Fallback:** Verify HTTP fallback works in CNTP-restricted networks
5. **Time Accuracy:** CNTP typically accurate to ±50ms vs GPS PPS
6. **Timezone Handling:** TZ updates require successful CNTP sync first (prevents stale data)

## Future Enhancements

- [ ] Periodic CNTP refresh (hourly) to correct RTC drift
- [ ] Prefer GPS time when valid fix available (higher accuracy)
- [ ] Cache last known good time across reboots (NVS)
- [ ] Add AT+CNTPSTOP support for power optimization
- [ ] Implement NITZ (+CTZV URC) passive time sync

## References

- SIM7080G AT Command Manual v1.08 - Section 15.7 (NTP)
- Soracom NTP Service: `ntp.soracom.io`
- POSIX Time Functions: `mktime()`, `localtime()`, `gmtime()`
- ESP32-S3 RTC: `settimeofday()`, `gettimeofday()`

---

**For questions or issues, contact the firmware team or consult the SIM7080G-notes.md document.**
