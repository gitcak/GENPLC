# ESP32-S3 Boot Architecture and Storage Capabilities

**Document Version:** 1.0  
**Last Updated:** 2025-01-08  
**Target Hardware:** M5Stack StampS3 (ESP32-S3)

---

## Overview

This document explains the ESP32-S3 boot architecture, why it cannot boot from SD card (unlike Raspberry Pi), and what storage options are available for firmware and data.

---

## Boot Sequence

### ESP32-S3 Boot Process

The ESP32-S3 has a **fixed, hardcoded boot sequence** that cannot be changed:

```
1. Power On
   ↓
2. Internal ROM (burned into chip at manufacture)
   ↓
3. First Stage Bootloader (address 0x1000 in SPI flash)
   ↓
4. Second Stage Bootloader (partition table, app selection)
   ↓
5. Application Firmware (main app partition)
```

**Critical Limitation:** The internal ROM **only** knows how to read from the attached SPI flash chip. It has no code to access SD cards, USB drives, or any other external storage.

### Why ESP32-S3 Cannot Boot from SD Card

1. **ROM is Read-Only:** The boot ROM is permanently burned into the chip during manufacturing and cannot be modified.

2. **No SD Support in ROM:** The ROM bootloader only contains drivers for:
   - SPI Flash (NOR flash attached via SPI/QSPI)
   - UART (for programming via serial)
   - USB-Serial (for programming via USB)

3. **No Bootloader Extension:** Unlike some ARM processors, ESP32-S3 cannot load a secondary bootloader from external storage to then boot the main firmware.

---

## Architecture Comparison: ESP32-S3 vs Raspberry Pi

### Raspberry Pi Boot Architecture

**Raspberry Pi** is a **System-on-Chip (SoC)** with flexible boot sources:

```
1. GPU Boot ROM (VideoCore)
   ↓
2. Check boot priority order (configurable):
   - SD Card (default)
   - USB Mass Storage
   - Network Boot (PXE)
   - eMMC (Compute Module)
   ↓
3. Load bootloader from selected source
   ↓
4. Load Linux kernel
```

**Flexibility:** The Raspberry Pi GPU ROM can read from SD cards, USB drives, and even boot over the network. This is why it can run entirely from an SD card or USB drive.

### ESP32-S3 Boot Architecture

**ESP32-S3** is a **Microcontroller** with fixed boot sequence:

```
1. CPU Boot ROM (fixed in silicon)
   ↓
2. Read bootloader from SPI flash ONLY
   ↓
3. Load app from SPI flash partition
```

**Limitation:** The ESP32-S3 must have its bootloader and firmware in the 8MB SPI flash chip. It cannot boot from external storage.

---

## What IS Possible with ESP32-S3 and SD Card

While you cannot boot the ESP32-S3 from an SD card, you CAN use the SD card for:

### 1. Data Logging and Storage

**Use Case:** Store sensor data, logs, configuration files
- `/logs` directory for system logs
- `/data` directory for sensor readings
- `/config` directory for user settings

**Implementation:** Already implemented in this firmware via `SDCardModule`.

### 2. Firmware Updates (OTA from SD Card)

**Use Case:** Store firmware `.bin` files on SD card and perform Over-The-Air (OTA) updates

**How it works:**
```cpp
1. Store new_firmware.bin on SD card
2. ESP32 reads .bin file from SD card
3. ESP32 writes .bin to OTA partition in SPI flash
4. ESP32 reboots
5. Bootloader switches to new OTA partition
6. New firmware runs from SPI flash
```

**Status:** Not yet implemented. Future enhancement.

### 3. Asset Storage (Fonts, Images, Config)

**Use Case:** Store large assets that don't fit in flash
- Custom fonts
- Images/icons
- Large configuration files
- Lookup tables

**Implementation:** Load assets from SD card into RAM as needed.

### 4. Code Execution from SD Card (Advanced, Not Recommended)

**Theoretical Possibility:** Load compiled code from SD card into RAM and execute it

**Limitations:**
- Limited by available RAM (320KB on ESP32-S3)
- Complex to implement (requires custom linker scripts)
- No code can run directly from SD card (must be copied to RAM)
- Performance penalty (SD card reads are slow)
- Security risks (no code signing verification)

**Recommendation:** NOT suitable for production use. Use OTA updates instead.

---

## ESP32-S3 Flash Memory Partitions

The firmware uses a **partition table** to divide the 8MB SPI flash:

### Default Partition Scheme

```
Address   Size      Label          Type        Description
--------  --------  -------------  ----------  ---------------------------
0x1000    ~28KB     bootloader     -           First/Second stage bootloader
0x8000    ~3KB      nvs            data        Non-Volatile Storage (WiFi, settings)
0x9000    ~16KB     otadata        data        OTA selection data
0xE000    ~8KB      phy_init       data        PHY calibration data
0x10000   ~1.3MB    factory        app         Main application (this firmware)
0x150000  ~1.3MB    ota_0          app         OTA update partition 1
0x290000  ~1.3MB    ota_1          app         OTA update partition 2
0x3D0000  ~3.2MB    spiffs         data        File system (optional, unused)
```

**Key Points:**
- All executable code must be in SPI flash
- OTA partitions allow firmware updates without recompiling
- SD card is completely separate and used only for data

---

## Auto-Format Behavior (This Firmware)

To simplify SD card setup, this firmware implements **auto-format on first boot**:

### First Boot Detection

The firmware checks for `/format_marker.txt` on the SD card:
- **If found:** SD card already initialized, proceed normally
- **If not found:** Check for `/logs` and `/data` directories
  - **If missing:** Auto-create directories and marker file
  - **If present:** SD card already has structure, create marker only

### Auto-Created Structure

On first boot, the firmware creates:
```
/
├── format_marker.txt    # Marker file (prevents repeated initialization)
├── logs/                # System logs directory
├── data/                # Data storage directory
└── config/              # Configuration directory
```

### Manual Format Removed

The manual "Format SD Card" button has been **removed** to prevent accidental data loss. SD cards are automatically initialized once on first use.

---

## Future Enhancements

### OTA Updates from SD Card

Implement a firmware update mechanism that:
1. Reads `.bin` firmware files from SD card
2. Validates firmware signature (optional but recommended)
3. Writes firmware to OTA partition
4. Reboots to new firmware

**Benefits:**
- Field updates without network connectivity
- Useful for remote installations
- No need for JTAG or serial connection

**Implementation Notes:**
- Use `esp_ota_begin()`, `esp_ota_write()`, `esp_ota_end()` APIs
- Store firmware files as `/firmware/app.bin` on SD card
- Add CRC/SHA256 verification before flashing
- Implement rollback on boot failure

### Asset Loading from SD Card

For applications with large assets:
- Store custom fonts in `/fonts/`
- Store images in `/images/`
- Load into PSRAM or RAM as needed
- Reduces flash usage for application code

---

## Summary Table

| Feature | Raspberry Pi | ESP32-S3 | Notes |
|---------|--------------|----------|-------|
| Boot from SD Card | ✅ Yes | ❌ No | ESP32 ROM cannot read SD cards |
| Boot from USB | ✅ Yes (Pi 3B+, 4, 5) | ❌ No | ESP32 ROM has no USB mass storage |
| Boot from Flash | N/A | ✅ Yes | Only boot source for ESP32 |
| Data Storage on SD | ✅ Yes | ✅ Yes | Both support SD card data access |
| OTA Updates | N/A | ✅ Yes | ESP32 has OTA partitions in flash |
| Run OS | ✅ Linux | ❌ No | ESP32 runs FreeRTOS (RTOS, not OS) |

---

## Conclusion

**The ESP32-S3 cannot boot from an SD card** due to its fixed internal ROM bootloader. However, SD cards are fully supported for:
- Data logging
- Configuration storage
- Asset storage
- Source for OTA firmware updates (future)

The firmware implements **auto-format on first boot** to simplify SD card setup and eliminate the need for manual formatting.

---

## References

- [ESP32-S3 Technical Reference Manual](https://www.espressif.com/sites/default/files/documentation/esp32-s3_technical_reference_manual_en.pdf) - Chapter 2: System and Memory
- [ESP-IDF OTA Documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/system/ota.html)
- [ESP32 Boot Process](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-guides/bootloader.html)
- StampS3 Partition Table: `platformio.ini` line 18 (`board_build.partitions = default.csv`)

---

**For questions about SD card storage or OTA updates, contact the firmware team.**
