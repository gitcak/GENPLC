# Code Migration Status - Monolithic to Modular Architecture

## Overview
This document tracks the migration from monolithic `main.cpp` to a modular, containerized codebase structure.

**Migration Date:** January 2025
**Status:** In Progress

---

## Migration Progress

### ✅ Completed Migrations (Latest Update)

#### Boot Screen (Moved to `src/ui/`)
- ✅ `drawBootScreen()` → `ui/boot_screen.cpp` (NEW)
- ✅ `BootLogEntry` struct → `ui/boot_screen.cpp` (internal)

#### UI Widgets (Moved to `src/ui/components/`)
- ✅ `drawCardBox()` → `ui/components/ui_widgets.cpp` (NEW)

#### RTC/Time Management (Moved to `src/system/`)
- ✅ `setRTCFromCellular()` → `system/rtc_manager.cpp` (NEW)
- ✅ `setRTCFromGPS()` → `system/rtc_manager.cpp` (NEW)
- ✅ `setRTCFromBuildTimestamp()` → `system/rtc_manager.cpp` (NEW)
- ✅ `getRTCTime()` → `system/rtc_manager.cpp` (NEW)

#### Network Utilities (Moved to `src/modules/catm_gnss/`)
- ✅ `getNetworkType()` → `modules/catm_gnss/network_utils.cpp` (NEW)

### ✅ Previously Completed Migrations

#### UI Pages (Moved to `src/ui/pages/`)
- ✅ `drawLandingPage()` → `ui/pages/landing_page.cpp`
- ✅ `drawGNSSPage()` → `ui/pages/gnss_page.cpp`
- ✅ `drawCellularPage()` → `ui/pages/cellular_page.cpp`
- ✅ `drawSystemPage()` → `ui/pages/system_page.cpp`
- ✅ `drawSettingsPage()` → `ui/pages/settings_page.cpp`
- ✅ `drawLogsPage()` → `ui/pages/logs_page.cpp`

#### UI Widgets (Moved to `src/ui/components/`)
- ✅ `drawButtonIndicators()` → `ui/components/ui_widgets.cpp`
- ✅ `drawSDInfo()` → `ui/components/ui_widgets.cpp`
- ✅ `drawCompactSignalBar()` → `ui/components/ui_widgets.cpp`
- ✅ `drawSignalBar()` → `ui/components/ui_widgets.cpp`
- ✅ `drawWiFiBar()` → `ui/components/ui_widgets.cpp`
- ✅ `drawWrappedText()` → `ui/components/ui_widgets.cpp`

#### UI Constants (Moved to `src/ui/`)
- ✅ `clampScroll()` → `ui/ui_constants.h`
- ✅ Layout constants → `ui/ui_constants.h`

---

### 🔄 Pending Migrations

#### UI Functions (Target: `src/ui/`)
- ✅ `drawBootScreen()` → `ui/boot_screen.cpp` (DONE)
- ✅ `drawCardBox()` → `ui/components/ui_widgets.cpp` (DONE)
- ⏳ `drawStatusBar()` → `ui/components/ui_widgets.cpp` (or remove if unused)
- ✅ `initializeIcons()` → `ui/components/icon_manager.cpp` (DONE)
- ✅ `cleanupContentSprite()` → `ui/components/icon_manager.cpp` (DONE)
- ✅ `cleanupStatusSprite()` → `ui/components/icon_manager.cpp` (DONE)
- ✅ `cleanupIconSprites()` → `ui/components/icon_manager.cpp` (DONE)
- ✅ `cleanupAllSprites()` → `ui/components/icon_manager.cpp` (DONE)
- ⏳ `setCustomFont()` → `ui/fonts.cpp` or `ui/components/ui_widgets.cpp`
- ⏳ `drawModalOverlay()` → `ui/components/modal.cpp` (NEW)

#### Time Utilities (Moved to `src/system/`)
- ✅ `syncRTCFromAvailableSources()` → `system/time_utils.cpp` (NEW)
- ✅ `fetchNtpTimeViaCellular()` → `system/time_utils.cpp` (NEW)
- ✅ `ensureNtpConfigured()` → `system/time_utils.cpp` (NEW)
- ✅ `maybeUpdateTimeZoneFromCellular()` → `system/time_utils.cpp` (NEW)
- ✅ `formatLocalFromUTC()` → `system/time_utils.cpp` (NEW)

#### Storage Utilities (Moved to `src/system/`)
- ✅ `formatSDCard()` → `system/storage_utils.cpp` (NEW)
- ✅ `deleteDirectory()` → `system/storage_utils.cpp` (NEW)

#### Icon Management (Moved to `src/ui/components/`)
- ✅ `initializeIcons()` → `ui/components/icon_manager.cpp` (NEW)
- ✅ `cleanupContentSprite()` → `ui/components/icon_manager.cpp` (NEW)
- ✅ `cleanupStatusSprite()` → `ui/components/icon_manager.cpp` (NEW)
- ✅ `cleanupIconSprites()` → `ui/components/icon_manager.cpp` (NEW)
- ✅ `cleanupAllSprites()` → `ui/components/icon_manager.cpp` (NEW)

#### Network Utilities (Target: `src/modules/catm_gnss/`)
- ✅ `getNetworkType()` → `modules/catm_gnss/network_utils.cpp` (DONE)
- ⏳ `tryInitCatMIfAbsent()` → `modules/catm_gnss/catm_gnss_module.cpp` or task file


#### LED Management (Target: `src/hardware/` or `src/system/`)
- ⏳ `updateSystemLED()` → `hardware/led_manager.cpp` (NEW) or `system/led_control.cpp`

#### Crash Recovery (Verify duplicates)
- ⏳ `reduceDisplayRefreshRate()` → Check if in `system/crash_recovery.cpp`
- ⏳ `reduceLoggingLevel()` → Check if in `system/crash_recovery.cpp`

---

## Current `main.cpp` Structure

### What Should Remain in `main.cpp`
1. **Arduino entry points**: `setup()`, `loop()`
2. **Task functions**: `vTaskButton()`, `vTaskDisplay()`, `vTaskStatusBar()`, `vTaskStampPLC()`
3. **Module initialization**: Creating module instances, starting tasks
4. **Global variable declarations**: Module pointers, task handles
5. **Forward declarations**: Only for functions that must be declared before use

### What Should Be Moved
- All UI rendering functions (except task functions)
- All utility functions (RTC, time, network, storage)
- Helper functions that don't need direct access to `setup()` context

---

## Migration Guidelines

### Module Organization
```
src/
├── ui/
│   ├── boot_screen.cpp (NEW)
│   ├── components/
│   │   ├── ui_widgets.cpp (existing)
│   │   ├── icons.cpp (NEW)
│   │   ├── sprite_utils.cpp (NEW)
│   │   └── modal.cpp (NEW)
│   └── pages/ (existing)
├── system/
│   ├── rtc_manager.cpp (NEW)
│   ├── time_utils.cpp (NEW) ✅
│   ├── storage_utils.cpp (NEW) ✅
│   └── crash_recovery.cpp (existing)
├── modules/
│   ├── catm_gnss/
│   │   └── network_utils.cpp (NEW)
│   └── storage/
│       └── sd_utils.cpp (NEW)
└── hardware/
    └── led_manager.cpp (NEW)
```

### UI Components Organization
```
src/ui/components/
├── ui_widgets.cpp (existing)
├── icon_manager.cpp (NEW) ✅
└── modal.cpp (NEW)
```

### Dependencies
- **UI modules** depend on: `M5StamPLC.h`, UI constants, UI types
- **System modules** depend on: Hardware modules, system config
- **Module utilities** depend on: Their respective module headers

---

## Next Steps

1. ✅ Create migration status document (this file)
2. ⏳ Move UI functions to appropriate modules
3. ⏳ Move RTC/time functions to system modules
4. ⏳ Move utility functions to appropriate modules
5. ⏳ Update includes and forward declarations
6. ⏳ Test build after each migration
7. ⏳ Verify no regressions
8. ⏳ Final cleanup of `main.cpp`

---

## Notes

- **Boot screen** is critical for POST - ensure it's accessible early
- **RTC functions** may have dependencies on module initialization order
- **Sprite cleanup** functions are used by crash recovery - verify dependencies
- **Modal overlay** may need access to global state (keep externs if needed)

---

## Version History

- **2025-01-XX**: Initial migration status document created
- **2025-01-XX**: UI pages migration completed
- **2025-01-XX**: UI widgets migration completed
- **2025-01-XX**: Optional steps completed - time utilities, storage utilities, and icon management migrated

