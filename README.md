# StampPLC CatM+GNSS FreeRTOS Project

## 🎯 **Project Overview**

A comprehensive FreeRTOS-based firmware for the M5Stack StampPLC with integrated CatM cellular and GNSS modules. This project demonstrates advanced embedded systems development with real-time operating systems, cellular communication, and GPS navigation.

## 🏗️ **Architecture**

### **Core Components**
- **FreeRTOS:** Real-time operating system with 6 concurrent tasks
- **M5Stack StampPLC:** ESP32-S3 based industrial controller
- **M5Unit-CatM:** SIM7080G cellular module for CatM1 connectivity
- **GNSS Module:** GPS/GNSS navigation and positioning
- **Debug System:** Comprehensive logging and monitoring

### **Module Structure**
```
src/
├── main.cpp                 # Main application entry point
├── debug_system.cpp         # Debug logging and monitoring
├── hardware/
│   ├── basic_stamplc.cpp   # Basic StampPLC interface
│   └── stamp_plc.cpp       # Advanced StampPLC hardware interface
├── modules/
│   ├── catm_gnss/          # CatM cellular + GNSS navigation
│   │   ├── catm_gnss_module.cpp
│   │   └── catm_gnss_task.cpp
│   ├── http/               # HTTP server for web interface
│   ├── pwrcan/             # Power CAN bus communication
│   ├── settings/           # Settings storage and management
│   └── storage/            # SD card and data logging
└── config/                # Configuration files
```

## 🚀 **Quick Start**

### **Prerequisites**
- PlatformIO IDE or CLI
- M5Stack StampPLC
- M5Unit-CatM module (SIM7080G)
- USB-C cable for programming

### **Build & Upload**
```bash
# Build the project
pio run

# Upload to device
pio run --target upload

# Monitor serial output
pio device monitor
```

### **Code Quality Check**
```bash
# Run static analysis
pio check

# Check high/medium severity issues
pio check --severity=high
```

## 📋 **Development Standards**

### **🔧 Code Quality Requirements**
- **Static Analysis:** All code must pass `pio check` with no high/medium severity issues
- **Memory Safety:** Use `strncpy` with bounds checking, no buffer overflows
- **Modern C++:** Use `auto*`, initialization lists, avoid C-style casting
- **Error Handling:** Comprehensive error checking and recovery

### **📝 Coding Standards**
- **Memory Safety:** Always validate buffer sizes and use proper initialization
- **Error Handling:** Check all return values and handle failures gracefully
- **Resource Management:** Use RAII principles and proper cleanup
- **Documentation:** Clear comments and consistent naming conventions

### **🔍 Pre-Commit Checklist**
- [ ] Run `pio check` - no high/medium severity issues
- [ ] Run `pio run` - successful compilation
- [ ] Test on hardware if applicable
- [ ] Verify memory usage is acceptable
- [ ] Check for proper error handling
- [ ] Ensure consistent coding style

**📖 See [DEVELOPMENT_CHECKLIST.md](DEVELOPMENT_CHECKLIST.md) for complete standards**

## 🛠️ **Hardware Configuration**

### **M5Unit-CatM Pin Mapping (Port C)**
- **G4 (White):** UART TX (GPIO4)
- **G5 (Yellow):** UART RX (GPIO5)
- **Power:** 5V/GND
- **Baud Rate:** 115200 8N1

### **UART Configuration**
- **UART1:** CatM module communication
- **UART2:** GNSS module communication
- **Serial Monitor:** 115200 baud for debugging

### **System LED (RGB)**
- **GREEN:** System running normally, no faults
- **ORANGE:** System booting/initializing connections
- **BLINKING RED:** Fault condition detected

## 📊 **System Features**

### **Display Interface**
- **Modern Industrial Dark Theme** - Consistent color palette across all pages
- **Full 320px width** utilization with optimized rendering
- **Multi-page navigation** with launcher interface
- **Primitive-drawn vector icons** - Satellite, GPS, Cellular, Gear, Log icons
- **Interactive Settings Page** - Adjustable brightness, sleep timeout, and system info
- **Per-page scroll support** - Dynamic content height calculation for smooth scrolling
- **Memory-efficient rendering** - Static buffers, no heap allocations during draw

### **Page Navigation**
- **Landing Page:** System overview with status icons, signal bars, and navigation hints
- **GNSS Page:** GPS/GNSS data (position, satellites, speed, UTC time)
- **Cellular Page:** Network status (operator, signal strength, connection stats)
- **System Page:** Hardware status, memory usage, module readiness, sensors
- **Settings Page:** Interactive controls for display brightness, sleep settings, firmware info
- **Logs Page:** System log viewer with scrollable content

### **Button Controls**
- **A Button:** Back/Exit (returns to landing page)
- **B Button:** Navigate up/previous item (short press) or decrease value (long press on Settings)
- **C Button:** Navigate down/next item (short press) or increase value (long press on Settings)
- **Long Press:** Adjust values on Settings page (brightness, sleep timeout)

### **Debug System**
- **Comprehensive logging** to serial monitor
- **UART I/O monitoring** for CatM+GNSS communication
- **Error tracking** and statistics
- **Display integration** for real-time status

### **Cellular Communication**
- **AT Command Interface** for SIM7080G
- **HTTP/HTTPS Support** for data transmission
- **Network Registration** and APN configuration
- **Error Recovery** and connection management

### **GNSS Navigation**
- **Multi-satellite Support** with TinyGPSPlus
- **Real-time Positioning** and navigation
- **Data Validation** and quality assessment
- **Performance Monitoring** and statistics

### **Settings & Configuration**
- **Display Settings:** Brightness control (10-255, 25-step increments)
- **Sleep Management:** Auto-sleep toggle and timeout configuration (30s-10min)
- **System Information:** Firmware version, free heap, SD card status, uptime
- **Persistent Storage:** All settings saved to NVS (Non-Volatile Storage)
- **System LED Control:** Automatic status indication (Green=OK, Orange=Boot, Red=Fault)

## 🔧 **Configuration**

### **PlatformIO Configuration**
```ini
[env:m5stack-stamps3-freertos]
platform = espressif32
board = m5stack-stamps3
framework = arduino
build_type = debug
```

### **Key Libraries**
- **M5Unified:** M5Stack hardware abstraction
- **M5GFX:** Graphics and display library
- **M5StamPLC:** StampPLC-specific hardware interface
- **TinyGSM:** Cellular modem interface
- **TinyGPSPlus:** GNSS data parsing
- **ArduinoJson:** JSON serialization
- **Simple QR Code:** QR code generation for settings

## 📈 **Performance Metrics**

### **Memory Usage**
- **RAM:** 7.7% (25,304 bytes / 327,680 bytes) - Optimized after UI refactoring
- **Flash:** 46.5% (609,353 bytes / 1,310,720 bytes) - Well within limits
- **Stack:** Optimized for FreeRTOS tasks with conservative allocation

### **UI Refactoring Improvements**
- **Theme Unification:** Consistent dark industrial theme across all pages
- **Memory Safety:** Eliminated String allocations, using static buffers
- **Dead Code Removal:** Removed unused functions and lambdas
- **Icon System:** Primitive-drawn vector icons replacing placeholder blocks
- **Settings Integration:** Fully functional display controls with NVS persistence

### **Task Priorities**
- **Display Task:** High priority for UI responsiveness
- **Button Task:** High priority for user input
- **Cellular Task:** Medium priority for communication
- **GNSS Task:** Medium priority for positioning
- **Status Task:** Low priority for system monitoring

## 🐛 **Debugging & Troubleshooting**

### **Serial Monitor Commands**
- **Button A:** Navigate back/exit current page
- **Button B:** Navigate up/previous item
- **Button C:** Navigate down/next item
- **Long Press C:** Enter edit mode (where applicable)

### **System LED Status**
- **GREEN:** Normal operation, all systems OK
- **ORANGE:** Boot sequence, establishing connections
- **BLINKING RED:** Fault detected (module not initialized, connection lost, etc.)

### **Common Issues**
- **Upload Failures:** Try manual download mode
- **CatM Communication:** Verify pin assignments and baud rate
- **Display Issues:** Check display initialization and timing
- **Memory Issues:** Monitor RAM usage and optimize
- **QR Code Not Scanning:** Verify QR library installation and generation

### **Debug Output**
- **Verbose Logging:** All CatM+GNSS communication logged
- **Error Tracking:** Comprehensive error counters and status
- **Performance Monitoring:** Real-time system metrics
- **Hardware Status:** Pin states and communication status

## 📚 **Documentation**

### **Key Documents**
- **[CLAUDE.md](CLAUDE.md):** Comprehensive AI assistant guidelines and project overview
- **[CHANGELOG.md](CHANGELOG.md):** Version history and changes
- **[docs/UI_REFACTORING_CHANGELOG.md](docs/UI_REFACTORING_CHANGELOG.md):** Complete UI refactoring details
- **[docs/RECOVERY_DEPLOYMENT_GUIDE.md](docs/RECOVERY_DEPLOYMENT_GUIDE.md):** Deployment procedures
- **[FINAL_PROJECT_STATUS.md](FINAL_PROJECT_STATUS.md):** Project status and recovery information
- **[platformio.ini](platformio.ini):** Build configuration with multiple environments

### **Archived Documentation**
Historical planning documents are archived in `docs/archive/`:
- Project refresh planning documents
- Completed UI refactoring planning context

### **Hardware Documentation**
- **M5Stack StampPLC:** [Official Documentation](https://docs.m5stack.com/)
- **M5Unit-CatM:** [SIM7080G AT Command Manual](https://files.waveshare.com/upload/0/02/SIM7070_SIM7080_SIM7090_Series_AT_Command_Manual_V1.03.pdf)
- **7080G Documentation:** Port C pin mapping and specifications

## 🤝 **Contributing**

### **Development Workflow**
1. **Follow Standards:** Adhere to [DEVELOPMENT_CHECKLIST.md](DEVELOPMENT_CHECKLIST.md)
2. **Code Quality:** Run `pio check` before commits
3. **Testing:** Test on hardware before submitting
4. **Documentation:** Update docs with code changes

### **Quality Assurance**
- **Static Analysis:** All code must pass cppcheck
- **Memory Safety:** No buffer overflows or memory leaks
- **Error Handling:** Comprehensive error recovery
- **Performance:** Monitor memory and CPU usage

## 📄 **License**

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## 🙏 **Acknowledgments**

- **M5Stack:** Hardware platform and libraries
- **FreeRTOS:** Real-time operating system
- **Arduino:** Development framework
- **PlatformIO:** Build system and toolchain

---

**Last Updated:** February 2026  
**Version:** 1.2.0  
**Status:** Production Ready ✅

### Recent Updates
- **UI Refactoring Complete:** Modern theme, vector icons, interactive settings
- **Memory Optimization:** Reduced RAM usage to 7.7% through static buffer usage
- **Settings Integration:** Fully functional display controls with NVS persistence
