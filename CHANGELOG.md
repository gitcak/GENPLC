# Changelog

All notable changes to the GENPLC project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.2.0] - 2026-02-01

### Added
- Modern industrial dark theme unified across all UI pages
- Primitive-drawn vector icons (Satellite, GPS, Cellular, Gear, Log)
- Interactive Settings page with display controls
- Per-page scroll height calculation functions for smooth scrolling
- NVS persistence for display settings (brightness, sleep timeout)
- Direct icon drawing functions for inline use

### Changed
- **Memory Usage**: Reduced RAM usage from 16.0% to 7.7% (25,304 bytes)
- **Flash Usage**: Increased to 46.5% (609,353 bytes) due to icon drawing code
- Replaced all String allocations in render paths with static buffers
- Removed dead code and unused functions from UI components
- Settings page now fully functional with interactive controls
- All pages use unified theme colors instead of hard-coded values

### Fixed
- DISPLAY_WIDTH macro conflict resolved using UI_DISPLAY_W/UI_DISPLAY_H
- Memory leaks from String usage in UI rendering
- Scroll clamping using magic numbers replaced with dynamic functions
- Settings page now actually controls brightness and sleep settings

### Removed
- Dead functions: `drawWrappedText`, `drawCompactSignalBar`, `drawCardBox`, `drawCard`, `drawCardCentered`, `drawKV`, `drawBar`, `lerp565`
- Unused String-based lambdas in GNSS page
- Placeholder gray square icons

## [1.1.0] - 2025-01-XX

### Added
- Recovery build system for emergency stabilization
- Multi-provider cellular management strategy
- Comprehensive crash recovery and watchdog systems
- Isolated cellular and memory test environments
- Emergency memory management and monitoring

### Changed
- Simplified task architecture from 6+ tasks to 3 essential tasks
- Reduced memory pressure through conservative allocation
- Improved cellular connectivity reliability

### Fixed
- Mutex contention issues
- Memory fragmentation and stack overflows
- Cellular PDP activation failures
- Task starvation and resource contention

## [1.0.0] - Initial Release

### Added
- FreeRTOS-based firmware for M5Stack StampPLC
- CatM cellular connectivity via SIM7080G
- GNSS navigation with TinyGPSPlus
- Multi-page UI system with navigation
- SD card logging and storage
- Comprehensive debug system
