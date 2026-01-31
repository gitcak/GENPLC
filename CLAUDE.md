# CLAUDE.md - AI Assistant Guidelines for GENPLC

This file provides comprehensive guidance for AI assistants working with the GENPLC codebase.

## Project Overview

**GENPLC** is a FreeRTOS-based firmware for the M5Stack StampPLC (ESP32-S3) with integrated CatM cellular (SIM7080G) and GNSS modules. The project provides industrial controller functionality with cellular connectivity for IoT applications.

### Key Hardware
- **MCU**: ESP32-S3FN8 (no PSRAM, limited SRAM)
- **Platform**: M5Stack StampPLC
- **Cellular**: SIM7080G (CatM1/NB-IoT) via M5Unit-CatM
- **Display**: Integrated LCD (135x240)
- **I/O**: 8 digital inputs, 4 analog channels, 2 relay outputs

### Project Status
The project has undergone a recovery/stabilization phase addressing mutex contention, memory pressure, and cellular connectivity issues. The current build uses a simplified 3-task architecture for stability.

## Repository Structure

```
GENPLC/
├── src/
│   ├── main.cpp                    # Main application entry point
│   ├── main_recovery.cpp           # Recovery build entry point
│   ├── config/
│   │   ├── system_config.h         # System-wide constants, pins, feature flags
│   │   ├── task_config.h           # FreeRTOS task priorities and stack sizes
│   │   └── FreeRTOSConfig.h        # FreeRTOS kernel configuration
│   ├── hardware/
│   │   ├── basic_stamplc.cpp/h     # Basic StampPLC hardware interface
│   │   ├── stamp_plc.cpp/h         # Advanced StampPLC features
│   │   ├── rs485_adapter.cpp/h     # RS485 communication
│   │   └── generator_*.cpp/h       # Generator monitoring features
│   ├── modules/
│   │   ├── catm_gnss/              # CatM cellular + GNSS navigation
│   │   │   ├── catm_gnss_module.cpp/h
│   │   │   ├── catm_gnss_task.cpp/h
│   │   │   ├── cell_status.cpp/h
│   │   │   ├── gnss_status.cpp/h
│   │   │   └── network_utils.cpp/h
│   │   ├── cellular/              # Multi-provider cellular management
│   │   │   ├── cellular_manager.h
│   │   │   └── network_manager.h
│   │   ├── pwrcan/                # Power CAN bus (disabled by default)
│   │   ├── logging/               # Log buffer and utilities
│   │   ├── settings/              # Settings storage
│   │   └── storage/               # SD card module
│   ├── system/
│   │   ├── crash_recovery.cpp/h   # Crash detection and recovery
│   │   ├── memory_safety.cpp/h    # Memory bounds checking
│   │   ├── rtc_manager.cpp/h      # RTC time synchronization
│   │   ├── time_utils.cpp/h       # Time utilities
│   │   ├── storage_utils.cpp/h    # Storage helpers
│   │   └── watchdog_manager.cpp/h # Watchdog monitoring
│   └── ui/
│       ├── boot_screen.cpp/h      # Boot/POST display
│       ├── theme.h                # Color definitions
│       ├── ui_constants.h         # Layout constants
│       ├── ui_types.h             # UI enums and types
│       ├── components/            # Reusable UI widgets
│       │   ├── icon_manager.cpp/h
│       │   └── ui_widgets.cpp/h
│       └── pages/                 # Display pages
│           ├── landing_page.cpp/h
│           ├── gnss_page.cpp/h
│           ├── cellular_page.cpp/h
│           ├── system_page.cpp/h
│           ├── settings_page.cpp/h
│           └── logs_page.cpp/h
├── include/
│   ├── data_structures.h          # Shared data types and enums
│   ├── debug_system.h             # Debug logging macros
│   ├── memory_monitor.h           # Memory monitoring
│   ├── memory_pool.h              # Memory pool allocator
│   ├── string_pool.h              # String pooling
│   └── ui_utils.h                 # UI utilities
├── libraries/
│   └── M5_SIM7080G/               # Custom SIM7080G library
├── docs/                          # Documentation
├── scripts/                       # Build and test scripts
├── recovery_build/                # Standalone recovery build
├── recovery_cellular_test/        # Isolated cellular test
├── platformio.ini                 # PlatformIO build configuration
├── Makefile                       # Development workflow shortcuts
└── README.md                      # Project documentation
```

## Build System

### PlatformIO Configuration
The project uses PlatformIO with multiple build environments:

| Environment | Purpose |
|-------------|---------|
| `m5stamps3-recovery` | **Default** - Recovery/production build |
| `m5stamps3-cellular-test` | Isolated cellular module testing |
| `m5stamps3-memory-test` | Memory usage testing |

### Build Commands
```bash
# Build (default recovery environment)
pio run

# Upload to device
pio run --target upload

# Monitor serial output
pio device monitor

# Run static analysis
pio check

# Clean build
pio run --target clean

# Using Makefile shortcuts
make build      # Build firmware
make flash      # Upload to device
make monitor    # Serial monitor
make all        # Build, flash, and monitor
```

### Key Build Flags
```ini
-DRECOVERY_MODE=1              # Enable recovery mode features
-DENABLE_RECOVERY_BUILD=1       # Use simplified task architecture
-DMAX_RETRY_ATTEMPTS=3          # Connection retry limit
-DMEMORY_EMERGENCY_THRESHOLD=1024  # Low memory threshold (words)
-DWATCHDOG_TIMEOUT_MS=30000     # Watchdog timeout
```

## Architecture Guidelines

### FreeRTOS Task Architecture

**Task Priorities** (higher = more important):
- Priority 4: Industrial I/O, Sensor tasks
- Priority 3: System Monitor, GNSS, Cellular
- Priority 2: Display, Data Transmission
- Priority 1: Button Handler

**Stack Sizes** (words, 4 bytes each):
- System Monitor: 8192 words (32KB)
- Display: 3072 words (12KB)
- Button Handler: 4096 words (16KB)
- CatM+GNSS: 5120 words (20KB)

**Critical**: The ESP32-S3FN8 has NO PSRAM. Memory is severely constrained.

### Memory Management

**IMPORTANT**: This system runs without PSRAM. Follow these guidelines:

1. **Avoid dynamic allocation** - Use static buffers where possible
2. **Monitor heap usage** - Use `g_memoryMonitor.getStatus()`
3. **Check stack watermarks** - Use `uxTaskGetStackHighWaterMark()`
4. **Emergency thresholds**:
   - Warning: < 8KB free heap
   - Critical: < 4KB free heap

### Module Communication

Modules communicate via:
1. **Event Groups** - `xEventGroupSystemStatus` for status flags
2. **Queues** - UI events via `g_uiQueue`
3. **Global state** - Protected by `g_uiStateMutex`
4. **Direct function calls** - For tightly coupled modules

### UI System

The UI uses M5GFX (LGFX) directly on the display framebuffer:
- **Pages**: LANDING, GNSS, CELLULAR, SYSTEM, SETTINGS, LOGS
- **Navigation**: Button A=Home/Back, B=Scroll Up/Prev, C=Scroll Down/Next
- **Status indicators**: RGB LED indicates system state (Green=OK, Orange=Boot, Red=Fault)

## Code Conventions

### Naming Conventions
```cpp
// Global variables: g_ prefix
volatile bool g_cellularUp = false;
SemaphoreHandle_t g_uiStateMutex = nullptr;

// Constants: UPPER_SNAKE_CASE
#define CATM_UART_BAUD 115200
#define TASK_STACK_SIZE_DISPLAY 3072

// Classes: PascalCase
class CatMGNSSModule { ... };

// Functions: camelCase
void drawBootScreen(const char* status, int progress);

// Enums: PascalCase with scoped enums
enum class DisplayPage { LANDING_PAGE, GNSS_PAGE, ... };
```

### Memory Safety Patterns
```cpp
// Always use bounded string operations
strncpy(dest, src, sizeof(dest) - 1);
dest[sizeof(dest) - 1] = '\0';

// Check allocations
auto* module = new CatMGNSSModule();
if (!module) {
    Serial.println("ERROR: Allocation failed");
    return;
}

// Use static buffers for formatting
static char logBuf[64];  // Static to avoid stack pressure
snprintf(logBuf, sizeof(logBuf), "Value: %d", value);
```

### Error Handling
```cpp
// Always check return values
if (!module->begin()) {
    Serial.printf("Error: %s\n", module->getLastError().c_str());
    delete module;
    module = nullptr;
    return false;
}

// Use descriptive error messages
Serial.println("WARNING: Thermal sensor (LM75B) I2C communication failed");
```

### FreeRTOS Patterns
```cpp
// Task creation with error checking
BaseType_t result = xTaskCreatePinnedToCore(
    vTaskFunction,
    "TaskName",
    TASK_STACK_SIZE,
    NULL,
    TASK_PRIORITY,
    &taskHandle,
    0  // Core 0
);
if (result != pdPASS) {
    Serial.println("ERROR: Failed to create task");
    return;
}

// Queue operations with timeouts
UIEvent ev{type};
if (xQueueSend(g_uiQueue, &ev, pdMS_TO_TICKS(25)) != pdTRUE) {
    // Handle queue full
}

// Mutex with timeout
if (xSemaphoreTake(g_uiStateMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
    // Critical section
    xSemaphoreGive(g_uiStateMutex);
}
```

## Configuration Files

### system_config.h
Central configuration for:
- Pin definitions (I2C, SPI, UART)
- CatM/GNSS settings (APN, timeouts)
- Feature flags (`ENABLE_SD`, `ENABLE_PWRCAN`)
- Display settings
- Sensor limits

### task_config.h
FreeRTOS configuration:
- Task priorities
- Stack sizes
- Queue sizes
- Event group bits
- Timing constants

### Feature Flags
```cpp
#define ENABLE_PWRCAN false      // CAN bus (disabled)
#define ENABLE_WEB_SERVER false  // Web server (disabled)
#define ENABLE_SD 1              // SD card support
```

## Important Modules

### CatM+GNSS Module (`src/modules/catm_gnss/`)
Handles cellular connectivity and GPS:
- AT command interface to SIM7080G
- Network registration and PDP context
- GNSS data parsing with TinyGPSPlus
- Multi-provider APN support

### UI System (`src/ui/`)
Display management:
- Page rendering via M5GFX sprites
- Button navigation via event queue
- Status indicators (signal bars, memory)
- Modal dialogs for errors

### System Utilities (`src/system/`)
Core infrastructure:
- Crash recovery and watchdog
- RTC synchronization (from cellular/GPS)
- Memory safety checks
- Storage utilities

## Common Tasks

### Adding a New Display Page
1. Create `src/ui/pages/new_page.cpp` and `new_page.h`
2. Add `DisplayPage::NEW_PAGE` to `ui_types.h`
3. Add case in `vTaskDisplay()` switch statement
4. Add navigation event handling in `vTaskButton()`

### Adding a New Module
1. Create directory under `src/modules/`
2. Define module class with `begin()`, `update()` methods
3. Add feature flag in `system_config.h`
4. Create FreeRTOS task if needed
5. Update `build_src_filter` in `platformio.ini`

### Modifying Task Stack Sizes
1. Edit `src/config/task_config.h`
2. Look for `TASK_STACK_SIZE_*` defines
3. Values are in **words** (multiply by 4 for bytes)
4. Monitor with `uxTaskGetStackHighWaterMark()`

### Testing Cellular Connectivity
1. Use `recovery_cellular_test/` for isolated testing
2. Check APN configuration in `system_config.h`
3. Monitor AT command responses in serial output
4. Verify PDP activation and IP assignment

## Debugging

### Serial Monitor
```bash
pio device monitor
# or
make monitor
```

Key log prefixes:
- `[Button]` - Button task status
- `[CatM]` - Cellular module
- `[UI]` - UI events
- `WARNING:` - Non-critical issues
- `ERROR:` - Critical failures

### Memory Debugging
```cpp
// In serial output, look for:
// "HWM words | Button:X Display:X Status:X..."
// Shows stack high water marks (free words)

// Heap status
g_memoryMonitor.update();
MemoryStatus status = g_memoryMonitor.getStatus();
```

### Static Analysis
```bash
pio check                    # Full analysis
pio check --severity=high    # High severity only
```

## Gotchas and Pitfalls

1. **No PSRAM**: The ESP32-S3FN8 has limited SRAM. Avoid large buffers.

2. **Mutex Contention**: The recovery build simplified task architecture to avoid deadlocks. Be careful adding new synchronization.

3. **Serial Output During Boot**: Use `Serial.flush()` after critical messages to ensure they're sent before potential crashes.

4. **I2C Bus Conflicts**: M5StamPLC has multiple I2C devices. Initialize in correct order and handle failures gracefully.

5. **Cellular Module Hot-Plug**: The system supports detecting CatM module connection/disconnection at runtime.

6. **Display Sleep**: Display automatically sleeps after 2 minutes of inactivity. Any button press wakes it.

7. **String Usage**: Avoid Arduino String class in time-critical code. Use static char buffers with snprintf.

8. **Stack Overflow**: Most common crash cause. Always check task stack watermarks.

## Quick Reference

### Key Files to Modify
| Task | File(s) |
|------|---------|
| Add feature flag | `src/config/system_config.h` |
| Change task priority | `src/config/task_config.h` |
| Modify UI | `src/ui/pages/*.cpp` |
| Change pin assignments | `src/config/system_config.h` |
| Cellular settings | `src/config/system_config.h` |
| Build configuration | `platformio.ini` |

### Memory Budget (Approximate)
- Flash: ~1.2MB used of 1.3MB (88%)
- RAM: ~52KB used of 328KB (16%)
- Task stacks: ~100KB total

### Pin Assignments
| Pin | Function |
|-----|----------|
| GPIO4 | CatM UART RX |
| GPIO5 | CatM UART TX |
| GPIO13 | I2C SDA |
| GPIO15 | I2C SCL |
| GPIO42 | PWRCAN TX (disabled) |
| GPIO43 | PWRCAN RX (disabled) |

## Related Documentation
- `README.md` - Project overview and quick start
- `docs/RECOVERY_DEPLOYMENT_GUIDE.md` - Deployment procedures
- `docs/MEMORY_MANAGEMENT_ANALYSIS.md` - Memory optimization details
- `docs/TASK_PRIORITY_OPTIMIZATION.md` - FreeRTOS task analysis
- `FINAL_PROJECT_STATUS.md` - Recovery status report
