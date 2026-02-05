# UI Refactoring Changelog

This document summarizes the UI refactoring changes completed following the prioritized sequence from `UI_AGENT_CONTEXT.md`.

## Completed Steps

### Step 1: Theme Unification (Previously Completed)
- Expanded `theme.h` with comprehensive color palette
- Rewrote `theme.cpp` with modern industrial dark theme
- Applied consistent theming across all pages
- Fixed DISPLAY_WIDTH macro conflict using UI_DISPLAY_W/UI_DISPLAY_H

### Step 2: Dead Code Removal + String Fixes
**Removed Dead Functions:**
- `src/ui/components/ui_widgets.cpp`: Removed `drawWrappedText`, `drawCompactSignalBar`, `drawCardBox`
- `src/ui/components.cpp`: Cleared `drawCard`, `drawCardCentered`, `drawKV`, `drawBar`, `lerp565`

**String Usage Fixes (Heap Avoidance):**
- `landing_page.cpp`: Changed `String carrier` to `static char carrier[32]`
- `boot_screen.cpp`: Changed `BootLogEntry::text` from `String` to `char[48]`
- `fonts.cpp`: Changed `static String s_fontPath` to `static char s_fontPath[64]`

### Step 3: Per-Page Scroll Content Heights
**Added Content Height Functions:**
Each page now has a `*PageContentHeight()` function that returns the dynamic content height for scroll calculations:

| Page | Function | Notes |
|------|----------|-------|
| GNSS | `gnssPageContentHeight()` | 9 content rows + padding |
| Cellular | `cellularPageContentHeight()` | 12 content rows + padding |
| System | `systemPageContentHeight()` | 16 content rows + padding |
| Settings | `settingsPageContentHeight()` | 7 rows + section gaps |
| Logs | `logsPageContentHeight()` | Dynamic based on `log_count()` |

**Main.cpp Updates:**
- Replaced magic numbers in scroll clamping with function calls
- Added Settings page scroll support (was previously missing)

### Step 4: Interactive Settings Page
**New Display Settings in NVS:**
- `displayBrightness` (0-255, default 100)
- `displaySleepEnabled` (bool, default true)
- `displaySleepTimeoutMs` (30000-600000ms, default 120000)

**Settings Page Controls:**
- Brightness: Adjustable in 25-step increments (10-255)
- Auto Sleep: Toggle ON/OFF
- Sleep Timeout: Adjustable in 30s steps (30s - 10min)

**Button Navigation on Settings Page:**
- B/C short press: Cycle through settings items
- B/C long press: Adjust selected value (decrease/increase)
- All changes persist immediately to NVS

**System Info Section:**
- Firmware version (from STAMPLC_VERSION)
- Free heap with color-coded health indicator
- SD card status and free space
- Uptime display

**Files Modified:**
- `src/modules/settings/settings_store.h`: Added display settings fields and functions
- `src/modules/settings/settings_store.cpp`: Added `displaySettingsLoad()` / `displaySettingsSave()`
- `src/ui/pages/settings_page.h`: Added `SettingsItem` enum and control functions
- `src/ui/pages/settings_page.cpp`: Complete rewrite with interactive controls
- `src/ui/theme.h`: Added `accentDim` color
- `src/ui/theme.cpp`: Added `accentDim` value (0x1293)
- `src/main.cpp`:
  - Changed display variables to global (for extern access)
  - Added settings load at startup
  - Added Settings page button handling

### Step 5: Upgraded Icons (Primitive-Drawn Glyphs)
**Replaced Placeholder Gray Squares with Vector Icons:**

The old `icon_manager.cpp` filled sprites with gray (`0x4A4A4A`). Now all icons are drawn using LGFX primitives:

| Icon | Description | Used In |
|------|-------------|---------|
| Satellite | Body + solar panels + antenna | GNSS status |
| GPS Pin | Circle with point marker | GNSS page header |
| Cellular Bars | 4 ascending bars | Cellular status |
| Gear | Cogwheel with 6 teeth | System/Settings pages |
| Log/Document | Rectangle with corner fold + text lines | Logs page |
| Tower | Tapered tower with antenna | (Alternative cellular) |

**Direct Drawing Functions Added:**
For inline use without sprite overhead:
- `drawIconSatelliteDirect(x, y, size, color)`
- `drawIconGPSDirect(x, y, size, color)`
- `drawIconCellularDirect(x, y, size, color)`
- `drawIconGearDirect(x, y, size, color)`
- `drawIconLogDirect(x, y, size, color)`
- `drawIconSettingsDirect(x, y, size, color)`

**Landing Page Enhanced:**
- Added icons next to each status row (Cellular, GPS, SD, Memory)
- Added signal bar visualization on right side
- Added navigation hint at bottom

**Page Headers Updated:**
All pages now show a small icon (14px) next to the title:
- GNSS: GPS pin icon
- Cellular: Signal bars icon
- System: Gear icon
- Settings: Gear icon (settings alias)
- Logs: Document icon

**Files Modified:**
- `src/ui/components/icon_manager.h`: Added direct drawing function declarations
- `src/ui/components/icon_manager.cpp`: Complete rewrite with primitive drawing
- `src/ui/pages/landing_page.cpp`: Added icons to status rows
- `src/ui/pages/gnss_page.cpp`: Added icon to header
- `src/ui/pages/cellular_page.cpp`: Added icon to header
- `src/ui/pages/system_page.cpp`: Added icon to header
- `src/ui/pages/settings_page.cpp`: Added icon to header
- `src/ui/pages/logs_page.cpp`: Added icon to header

## Build Results

| Metric | Before | After Step 5 |
|--------|--------|--------------|
| RAM | 7.7% | 7.7% (25,304 bytes) |
| Flash | 46.0% | 46.5% (609,353 bytes) |

The +0.5% Flash increase is from the new icon drawing code (worth it for visual improvement).

## Remaining Items from UI_AGENT_CONTEXT.md

The following items from the "Do This First" sequence are now complete:
1. ✅ Unify theme usage across all pages + modal
2. ✅ Remove dead code / unused lambdas + String fixes
3. ✅ Replace magic scroll clamp heights with per-page constants
4. ✅ Make Settings actually control brightness/sleep
5. ✅ Upgrade icons (primitive-drawn glyphs)

### Future Improvements (Not Yet Implemented)
From the context document, these remain as future work:
- UIContext struct to eliminate extern-driven spaghetti
- Dirty-region rendering optimization
- Page transition animations
- Status strip (time, cell/GNSS icons, heap)
- Logs page auto-scroll and filtering

## Theme Color Reference

The current theme (`src/ui/theme.cpp`) uses a modern industrial dark palette:

```
Background:     0x0841 (#0C1021) - Deep charcoal navy
Card:           0x1082 (#182838) - Slightly lighter
Accent:         0x4E7C (#4CCCE0) - Teal
AccentDim:      0x1293 (#102838) - Selection background
Text:           0xFFFF (White)
TextSecondary:  0xB5B6 (#B0B0B0)
Green:          0x47EA (#44FD50) - Success
Yellow:         0xFE60 (#FFD000) - Warning
Red:            0xF8A2 (#FF1414) - Error
Cyan:           0x07FF (#00FFFF) - Info/Action
```

## File Summary

### New Files
- `docs/UI_REFACTORING_CHANGELOG.md` (this file)

### Modified Files (Steps 4-5)
- `src/modules/settings/settings_store.h`
- `src/modules/settings/settings_store.cpp`
- `src/ui/theme.h`
- `src/ui/theme.cpp`
- `src/ui/components/icon_manager.h`
- `src/ui/components/icon_manager.cpp`
- `src/ui/pages/landing_page.cpp`
- `src/ui/pages/gnss_page.cpp`
- `src/ui/pages/cellular_page.cpp`
- `src/ui/pages/system_page.cpp`
- `src/ui/pages/settings_page.h`
- `src/ui/pages/settings_page.cpp`
- `src/ui/pages/logs_page.cpp`
- `src/main.cpp`
