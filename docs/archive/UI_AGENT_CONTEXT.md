# UI Agent Context (GENPLC)

This doc is **Claude-facing context** for working in `src/ui/` (and the UI plumbing in `src/main.cpp`). It maps what exists today and lists **everything worth updating/refining** for better code quality and a better on-device UI.

## Goals + constraints (do not ignore)

- **Hard constraint**: ESP32‑S3FN8, **no PSRAM**, tight SRAM. UI code must be stingy with stack/heap.
- **Prefer static buffers** (`snprintf`) over `String` and per-frame allocations.
- **Avoid deep call stacks** inside tasks; watch task watermarks.
- **UI must remain responsive**: button->visual feedback should feel instant (target \<200ms).

## Current UI architecture (what exists)

### Tasks and responsibilities

- **`vTaskButton`** (`src/main.cpp`)
  - Reads hardware buttons, routes events into `g_uiQueue`.
  - Uses **short press vs long press** mapping.
  - Intercepts inputs when a **modal** is active (`g_modalActive`).
- **`vTaskDisplay`** (`src/main.cpp`)
  - Drains `g_uiQueue` and updates shared UI state under `g_uiStateMutex`.
  - Full redraw on `pageChanged` (plus some scroll actions force redraw).
  - Handles display sleep/wake (2 min idle default).
- **`vTaskStatusBar`** (`src/main.cpp`)
  - Currently functions as a **system monitor** (RTC sync / sensor checks / memory monitor).
  - **UI status bar rendering is effectively disabled** (`drawStatusBar()` is blank).

### UI state + event flow

- **Event queue**: `g_uiQueue` of `UIEvent { UIEventType type; }`
  - `UIEventType`: `GoLanding`, `GoGNSS`, `GoCELL`, `GoSYS`, `GoSETTINGS`, `ScrollUp/Down`, `PrevPage/NextPage`, `Launcher*`, `Redraw`.
  - `g_uiEventDrops` tracks queue overflow (currently just logs occasionally).
- **Page selection**: global `volatile DisplayPage currentPage`
  - Enum lives in `src/ui/ui_types.h`.
- **Scroll state**: globals `scrollGNSS`, `scrollCELL`, `scrollSYS`, `scrollSETTINGS`, `scrollLOGS`
  - Updated in `vTaskDisplay` based on `ScrollUp/ScrollDown`.
  - Uses `clampScroll()` from `src/ui/ui_constants.h`.
- **Modal overlay**:
  - `g_modalActive`, `g_modalType` (currently `NO_COMM_UNIT`), `g_commFailureDescription`.
  - `drawModalOverlay()` draws an opaque content-area overlay (currently **not themed**).
  - `tryInitCatMIfAbsent()` supports CatM hot-attach retry (C button).

### Rendering model

- **Primary rendering**: pages draw directly to `M5StamPLC.Display` (no retained UI tree).
- **Theme**: `src/ui/theme.h/.cpp` defines a single dark theme + gradient card colors.
- **Reusable components**:
  - `src/ui/components.h/.cpp`: `ui::drawCard`, `ui::drawCardCentered`, `ui::drawKV`, `ui::drawBar`
  - `src/ui/components/ui_widgets.h/.cpp`: signal bar, wifi bar, SD info, button indicators, wrapped text, `drawCardBox`
- **Icons**:
  - `src/ui/components/icon_manager.*` creates sprites (16x16 and 32x32) but currently fills them with placeholder gray blocks.
  - Icons live as **globals in `src/main.cpp`** and are referenced via `extern` in the icon manager.
- **Fonts**:
  - `src/ui/fonts.*` loads `.vlw` fonts from SD under `/fonts/*.vlw`.
  - The font system is **not consistently applied** by pages; `setCustomFont()` is a stub in `main.cpp`.

## UI pages (what they do today)

All page functions are free functions with implicit dependencies via `extern` globals.

- **Landing** (`src/ui/pages/landing_page.cpp`)
  - “System Overview”: RTC time, cell summary, GNSS summary, SD status, free heap.
  - Uses `ui::theme()` correctly.
  - Uses `String` for carrier/operator; some formatting is `snprintf`.
- **GNSS** (`src/ui/pages/gnss_page.cpp`)
  - GNSS fix fields, sats, UTC time.
  - **Hard-coded colors** (`WHITE/YELLOW/RED/CYAN`) instead of theme.
  - Contains `String`-heavy lambdas for formatting that appear unused (likely dead weight).
- **Cellular** (`src/ui/pages/cellular_page.cpp`)
  - Operator, connected flag, IMEI, tx/rx stats, signal bar.
  - Uses theme mostly; uses `snprintf` for summaries.
- **System** (`src/ui/pages/system_page.cpp`)
  - Heap usage, CPU freq, module readiness, sensors, WiFi bar.
  - Uses theme; uses scroll offset `scrollSYS`.
  - Calls `M5StamPLC.RX8130.begin()` during draw (side-effect inside render).
- **Settings** (`src/ui/pages/settings_page.cpp`)
  - Mostly SD status/info.
  - **Hard-coded colors** and large text size; not theme-aligned.
  - Not actually “settings” yet (brightness/sleep/etc not wired).
- **Logs** (`src/ui/pages/logs_page.cpp`)
  - Dumps `log_buffer` lines.
  - Uses hard-coded colors; does not wrap long lines; draws only when y in view.

## File map (where to change things)

- **Core UI/task glue**: `src/main.cpp`
  - `UIEventType`, `g_uiQueue`, `g_uiStateMutex`, `currentPage`, scroll globals, sleep/brightness, modal overlay, CatM hot-attach.
- **Theme**: `src/ui/theme.h`, `src/ui/theme.cpp`
- **Layout constants**: `src/ui/ui_constants.h`
- **Page enum**: `src/ui/ui_types.h`
- **Reusable drawing**: `src/ui/components.h`, `src/ui/components.cpp`
- **Widgets/utilities**: `src/ui/components/ui_widgets.h`, `src/ui/components/ui_widgets.cpp`
- **Icons + sprite cleanup**: `src/ui/components/icon_manager.h`, `src/ui/components/icon_manager.cpp`
- **Fonts**: `src/ui/fonts.h`, `src/ui/fonts.cpp`
- **Pages**: `src/ui/pages/*.cpp`, `src/ui/pages/*.h`
- **Boot POST**: `src/ui/boot_screen.*`

## What should be updated/refined (code + UI)

### 1) Make styling consistent (theme everywhere)

- **Problem**: GNSS/Settings/Logs/Modal still use raw `WHITE/YELLOW/RED/CYAN` + ad-hoc sizing.
- **Fix**:
  - Convert all pages to use `ui::theme()` colors + consistent typography rules.
  - Replace the modal palette with theme colors (or define a `Theme::modalBg`, `Theme::warning`, etc).
  - Centralize “section header” style (color, spacing).

### 2) Kill `extern`-driven spaghetti (explicit UIContext)

- **Problem**: pages pull data via `extern` pointers (`catmGnssModule`, `sdModule`, `stampPLC`) and global scroll vars.
- **Fix**:
  - Create a lightweight `UIContext` struct (pointers to modules + immutable config + references to UI state).
  - Change each page renderer signature to accept context:
    - e.g. `void drawSystemPage(const UIContext& ctx, int16_t scrollY);`
  - Keep globals only as a transitional shim if needed.

### 3) Stop allocating during draw (heap/stack safety)

- **Problem**: `String` usage inside render paths (and `drawWrappedText` uses `String::substring` repeatedly).
- **Fix**:
  - Replace per-frame `String` building with bounded `snprintf` into static/stack buffers.
  - Rework `drawWrappedText()` to accept `const char*` + length and avoid substring allocations.
  - Eliminate dead lambdas in `gnss_page.cpp` (or move to a shared formatting helper if truly needed).

### 4) Replace magic scroll heights with derived values

- **Problem**: `vTaskDisplay` clamps scroll with hard-coded content heights (e.g. `122`, `LINE_H2 + LINE_H1 * 16`, etc).
- **Fix**:
  - Give each page a `contentHeightPx()` helper (or constants) and use it in scroll clamping.
  - For logs, compute height based on `log_count()` (already partially done).

### 5) Clarify navigation UX (discoverability)

- **Problem**: Landing requires **long press** to open pages; button hints exist but are easy to miss.
- **Fix ideas** (pick minimal complexity first):
  - Add a short “hold to open” hint on landing cards, or animate highlight on long-press threshold.
  - Consider making landing use **short press** to open and long press for alternate actions (if any).
  - Ensure `drawButtonIndicators()` matches actual behavior (including modal state).

### 6) Real icons (stop the gray squares)

- **Problem**: `icon_manager.cpp` draws placeholder blocks.
- **Fix options** (in order of practicality on no‑PSRAM):
  - Draw vector-ish icons with `LGFX` primitives into sprites at init time.
  - Use small 1‑bit or RLE bitmaps in flash (PROGMEM) and blit into sprites.
  - Keep both 16x16 and 32x32 variants, but ensure they share a single source.

### 7) Fonts: either commit or delete

- **Problem**: SD-loaded fonts exist but aren’t consistently applied; `setCustomFont()` is a no-op.
- **Fix**:
  - Decide: **always default font** (simplest) vs **optional SD font** (cool).
  - If keeping SD fonts: call `ui::fonts::applyToDisplay()` once (or per sprite) and document fallbacks.
  - Avoid per-frame font loads.

### 8) Status bar: either implement or remove the ghosts

- **Problem**: status bar task runs, but `drawStatusBar()` is blank and `STATUS_BAR_H` implies a bar.
- **Fix**:
  - Option A (useful): implement a very small status strip (time, cell/GNSS icons, heap).
  - Option B (lean): remove the rendering concept entirely and reclaim content space; keep status task only if needed for monitoring.

### 9) Settings page should control actual settings

Targets already present in `main.cpp`:

- `displaySleepEnabled`
- `DISPLAY_SLEEP_TIMEOUT_MS` (constant today)
- `displayBrightness`

Refine into Settings UI:

- Toggle sleep enable/disable.
- Adjust brightness (coarse steps).
- Show firmware version/build, SD info, maybe WiFi AP/STA status.
- Persist to NVS (`Preferences`) if that’s already in use elsewhere.

### 10) Logs page UX + safety

- Add wrapping or truncation with ellipsis.
- Color by severity (if log lines are tagged) or by substring match (`ERROR`, `WARNING`).
- Provide “clear logs” action (guarded) if useful.

### 11) Rendering performance improvements (cheap wins)

- Avoid full-screen clears if only a small area changes.
- Consider a light “dirty region” model:
  - Always redraw button bar.
  - Redraw content only on changes/scroll.
- If sprites are used: keep one `contentSprite` sized to content area (already exists as global) and render pages into it to prevent flicker.

### 12) Concurrency correctness (don’t lie to yourself with `volatile`)

- Pages read module state without locks. That’s often OK if the producers are atomic-ish, but:
  - Prefer copying module snapshots into local structs early in the draw call.
  - Avoid calling `.begin()` or other init methods inside page draw (e.g. `RX8130.begin()` in `system_page.cpp`).

## “Do this first” refactor sequence (fast + low-risk)

- **Unify theme usage** across all pages + modal.
- **Remove dead code / unused lambdas** in `gnss_page.cpp`.
- **Replace `drawWrappedText(String)`** with a non-allocating variant (and update modal/log usage).
- **Replace magic scroll clamp heights** with per-page constants.
- **Make Settings actually control** brightness/sleep (no persistence at first; add later).
- **Upgrade icons** (primitive-drawn glyphs are the quickest).

## Quick test checklist (when changing UI)

- Navigation: landing -> each page -> home -> prev/next -> scroll.
- Modal: unplug CatM unit, confirm overlay; retry works; dismissal works.
- Sleep: leave idle 2 minutes, wake on button press, no stuck black screen.
- Memory: watch `ESP.getFreeHeap()` and task stack HWM after running for 10+ minutes.

