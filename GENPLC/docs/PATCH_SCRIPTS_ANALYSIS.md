# Patch Scripts Analysis

## Overview
This document analyzes the necessity of build-time patch scripts in the StampPLC project.

## Current Scripts

### 1. `scripts/patch_m5stamplc_ina226.py`
**Status: ✅ NECESSARY**

**Purpose:** Patches M5StamPLC INA226 API calls for M5Unified 0.2.x compatibility

**Changes:**
- `INA226.readBusVoltage()` → `INA226.getBusVoltage()`
- `INA226.readShuntCurrent()` → `INA226.getShuntCurrent()`
- Replaces deprecated `configure()`/`calibrate()` pattern with config struct

**Rationale:** M5StamPLC library uses deprecated INA226 API that's incompatible with M5Unified 0.2.x. This patch ensures compatibility without waiting for upstream fixes.

**TODO:** Remove when upstream M5StamPLC releases updated API usage (M5Unified 0.2.x compatibility)

### 2. `scripts/fix_lvgl_arm.py`
**Status: ❌ REMOVED (REDUNDANT)**

**Purpose:** ~~Removes ARM assembly files that cause compilation issues~~

**Files That Would Be Deleted:**
- `src/draw/sw/blend/helium/lv_blend_helium.S`
- `src/draw/sw/blend/neon/lv_blend_neon.S`

**Build Flags Already Disable:**
- `-DLV_USE_ARM_NEON=0`
- `-DLV_USE_ARM_HELIUM=0`

**Analysis:** The build flags prevent compilation of these files, making the deletion redundant.

**Action Taken:** ✅ **REMOVED** - Relying on build flags instead of file deletion.

### 3. `scripts/add_squareline_ui_sources.py`
**Status: ✅ NECESSARY**

**Purpose:** Integrates SquareLine-generated UI sources into MCU build

**Functionality:**
- Adds UI source paths to compiler
- Builds SquareLine-generated C sources
- Links LVGL headers

**Rationale:** This is legitimate build integration, not a workaround. SquareLine generates UI code that needs to be compiled into the firmware.

## Recommendations

### Immediate Actions
1. **Document rationale** in `platformio.ini` ✅ (Done)
2. **Keep INA226 patch** - Still necessary for API compatibility ✅ (Done)
3. **Keep SquareLine script** - Legitimate build integration ✅ (Done)
4. **Remove ARM fix** - Build flags are sufficient ✅ (Done)

### Future Improvements
1. **Upstream fixes:** Submit patches to M5StamPLC for INA226 API
2. **Monitor LVGL updates:** Check if ARM assembly issues are resolved
3. **Test builds:** Verify ARM optimization flags work correctly
4. **Remove INA226 patch:** When M5StamPLC releases M5Unified 0.2.x compatibility

## Build Flags Documentation

```ini
-DLV_USE_ARM_NEON=0    ; Disable ARM NEON optimizations (ESP32-S3 compatibility)
-DLV_USE_ARM_HELIUM=0  ; Disable ARM Helium optimizations (ESP32-S3 compatibility)
```

These flags should prevent compilation of ARM assembly files, potentially making the file deletion redundant.
