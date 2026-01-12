# GENPLC Recovery Deployment Guide

## Overview

This guide provides step-by-step instructions for deploying the GENPLC recovery build to resolve critical system issues and restore production functionality.

## Recovery Build Purpose

The recovery build addresses critical issues that halted development and production:

### 🔴 Issues Resolved
1. **Cellular PDP Activation Failure**: Multi-provider strategy with automatic APN switching
2. **Mutex Contention**: Simplified task architecture eliminating resource conflicts
3. **Memory Pressure**: Emergency memory management and monitoring
4. **Task Starvation**: Reduced from 6+ tasks to 3 essential tasks
5. **System Instability**: Comprehensive error handling and recovery mechanisms

### ✅ New Features
- **Multi-Provider Cellular Support**: Soracom, Hologram, AT&T IoT, and custom APNs
- **Robust Connection Management**: Automatic retry with exponential backoff
- **Health Monitoring**: Real-time system and connection health tracking
- **Emergency Recovery**: Automatic system recovery and watchdog protection
- **Simplified UI**: Essential pages only with responsive navigation

## Quick Start

### Prerequisites
- M5Stack StampPLC with ESP32-S3
- M5Unit-CatM SIM7080G module
- Active cellular SIM (Soracom recommended)
- USB-C cable for programming
- PlatformIO IDE or CLI

### Emergency Deployment (15 minutes)

1. **Build Recovery Firmware**
   ```bash
   # Use recovery configuration
   cp platformio_recovery.ini platformio.ini
   
   # Build recovery firmware
   pio run -e m5stamps3-recovery
   ```

2. **Upload to Device**
   ```bash
   # Upload recovery firmware
   pio run -e m5stamps3-recovery -t upload
   
   # Monitor serial output
   pio device monitor
   ```

3. **Verify System Status**
   - Look for "GENPLC Recovery Mode" on display
   - Check for "System Running" in serial monitor
   - Verify cellular status on Cellular page

## Detailed Deployment

### Phase 1: Build Preparation

#### 1.1 Environment Setup
```bash
# Navigate to project directory
cd /path/to/GENPLC

# Backup current configuration
cp platformio.ini platformio_original.ini
cp platformio_recovery.ini platformio.ini

# Install dependencies
pio lib install
```

#### 1.2 Cellular Module Testing
```bash
# Test cellular connectivity (optional but recommended)
cd recovery_cellular_test
pio run -t upload
pio device monitor

# Look for:
# - Module communication established
# - SIM status: READY
# - Network registration: successful
# - PDP activation: successful
# - IP address assigned (not 0.0.0.0)
```

#### 1.3 Build Validation
```bash
# Run comprehensive tests
python3 scripts/test_recovery_build.py all

# Check test results:
# - BUILD: PASS - Recovery firmware compiles
# - MEMORY: PASS - Memory usage acceptable
# - STABILITY: PASS - All files present and valid
# - CELLULAR: PASS - Basic connectivity works
```

### Phase 2: Cellular Provider Configuration

#### 2.1 Soracom Configuration (Recommended)
```cpp
// In main_recovery.cpp or via settings
#define CELLULAR_PROVIDER SORACOM
#define PRIMARY_APN "soracom.io"
#define SECONDARY_APN "iot.soracom.io"
#define TERTIARY_APN "du.soracom.io"
```

#### 2.2 Alternative Providers
```cpp
// Hologram
#define CELLULAR_PROVIDER HOLOGRAM
#define PRIMARY_APN "hologram"

// AT&T IoT
#define CELLULAR_PROVIDER ATT_IOT
#define PRIMARY_APN "broadband"

// Custom APN
#define CELLULAR_PROVIDER CUSTOM
#define CUSTOM_APN "your.apn.here"
#define CUSTOM_USERNAME "your_username"
#define CUSTOM_PASSWORD "your_password"
```

#### 2.3 Provider Selection Strategy
1. **Primary**: Soracom (best IoT features)
2. **Fallback 1**: Hologram (global coverage)
3. **Fallback 2**: AT&T IoT (direct carrier)
4. **Emergency**: WiFi (if available)

### Phase 3: System Configuration

#### 3.1 Memory Management
```cpp
// Emergency thresholds (adjust as needed)
#define MEMORY_EMERGENCY_THRESHOLD 1024    // 1KB free words
#define WATCHDOG_TIMEOUT_MS 30000           // 30 second watchdog
#define MAX_RETRY_ATTEMPTS 3                // Connection retries
```

#### 3.2 Task Configuration
```cpp
// Simplified task priorities
#define MAIN_TASK_PRIORITY    2    // UI and system management
#define COMM_TASK_PRIORITY    3    // Cellular and GNSS (highest)
#define WATCHDOG_PRIORITY     4    // System monitoring (highest)
```

#### 3.3 Debug Configuration
```cpp
// Enable comprehensive logging
#define DEBUG_LEVEL 3                      // Verbose logging
#define CELLULAR_DEBUG_MODE true           // AT command logging
#define MEMORY_MONITORING_ENABLED true     // Memory usage tracking
```

### Phase 4: Deployment Verification

#### 4.1 System Boot Check
```bash
# Monitor boot sequence
pio device monitor

# Expected boot log:
# === GENPLC Recovery Mode ===
# Emergency stabilization build
# StampPLC initialized
# CatM+GNSS initialized
# Recovery tasks created successfully
# System Running
```

#### 4.2 UI Verification
- **Display Shows**: "GENPLC Recovery Mode"
- **Navigation**: A=Home, B=Previous, C=Next
- **Pages Available**: Landing, GPS, Cellular, System
- **Status Bar**: State, Cellular, Memory indicators

#### 4.3 Cellular Connectivity Test
1. Navigate to Cellular page (press C twice from home)
2. Check connection status
3. Verify signal strength (> -90 dBm acceptable)
4. Confirm IP address assignment (not 0.0.0.0)
5. Test connectivity if available

#### 4.4 Memory Health Check
1. Navigate to System page
2. Check memory usage indicators
3. Verify no "LOW MEMORY" warnings
4. Monitor stack usage in serial logs

## Troubleshooting

### Common Issues

#### Build Failures
```bash
# Clean and rebuild
pio run -t clean
pio run -e m5stamps3-recovery

# Check dependencies
pio lib list

# Update libraries
pio lib update
```

#### Cellular Issues
```bash
# Test with standalone cellular test
cd recovery_cellular_test
pio run -t upload && pio device monitor

# Common solutions:
# - Check SIM card insertion
# - Verify APN configuration
# - Test different provider
# - Check antenna connection
```

#### Memory Issues
```bash
# Monitor memory usage
# Look for "EMERGENCY: Low memory detected" in logs

# Solutions:
# - Reduce task stack sizes in task_config.h
# - Disable optional features
# - Check for memory leaks
```

#### Stability Issues
```bash
# Check watchdog resets
# Look for "Watchdog: Forced system reset" in logs

# Solutions:
# - Increase timeout values
# - Optimize task performance
# - Check for infinite loops
```

### Emergency Recovery

#### Complete System Reset
```cpp
// In recovery mode, system auto-recovers from:
// - Memory exhaustion
// - Task starvation  
// - Cellular failures
// - Watchdog timeouts

// Manual reset:
ESP.restart();  // Software reset
```

#### Factory Reset
```bash
# Clear all settings and restart
# (Implementation depends on storage system)

# Clear NVS/preferences
# Reset cellular module
# Restart with default configuration
```

## Production Deployment

### Rollout Strategy

#### Phase 1: Pilot Deployment (1-2 devices)
1. Deploy recovery build to pilot devices
2. Monitor for 24-48 hours
3. Verify cellular connectivity stability
4. Check memory usage and system health
5. Validate UI functionality

#### Phase 2: Limited Rollout (5-10 devices)
1. Deploy to additional devices
2. Monitor collective performance
3. Track success metrics
4. Address any issues discovered

#### Phase 3: Full Deployment (All devices)
1. Deploy to remaining devices
2. Implement monitoring and alerting
3. Establish update procedures
4. Document deployment status

### Success Metrics

#### Technical Metrics
- **System Uptime**: >99% continuous operation
- **Cellular Success**: >95% successful connections
- **Memory Usage**: <80% heap utilization
- **Boot Time**: <30 seconds to operational
- **Recovery Success**: >90% automatic recovery

#### Business Metrics
- **Device Availability**: >99% of devices operational
- **Data Transmission**: >95% successful data uploads
- **User Satisfaction**: Minimal user complaints
- **Maintenance Reduction**: Fewer manual interventions

### Monitoring and Maintenance

#### Real-time Monitoring
```cpp
// Key indicators to monitor
- Cellular connection status
- Signal strength (RSSI)
- Memory usage percentage
- Task stack watermarks
- System error counts
- Recovery attempt frequency
```

#### Alert Thresholds
```cpp
// Alert on these conditions
- Cellular disconnected > 5 minutes
- Memory usage > 80%
- Task stack < 1KB free
- System errors > 5 per hour
- Recovery attempts > 3 per hour
```

#### Maintenance Tasks
- **Daily**: Review system health logs
- **Weekly**: Check cellular provider performance
- **Monthly**: Update firmware and configurations
- **Quarterly**: Review and optimize deployment strategy

## Support and Escalation

### Level 1 Support (Basic Issues)
- Check serial logs for error messages
- Verify cellular signal and APN settings
- Restart device and re-test
- Check basic hardware connections

### Level 2 Support (Advanced Issues)
- Analyze detailed system diagnostics
- Test alternative cellular providers
- Review memory usage and task performance
- Update firmware and configurations

### Level 3 Support (Critical Issues)
- Hardware diagnostics and replacement
- Cellular provider escalation
- System architecture review
- Emergency procedures and workarounds

## Appendix

### A. Recovery Build File Structure
```
GENPLC/
├── src/main_recovery.cpp          # Simplified main application
├── platformio_recovery.ini       # Recovery build configuration
├── recovery_cellular_test/        # Standalone cellular test
├── src/modules/cellular/         # Multi-provider cellular manager
├── scripts/test_recovery_build.py  # Comprehensive testing script
└── docs/RECOVERY_DEPLOYMENT_GUIDE.md
```

### B. Configuration Comparison
| Feature | Original Build | Recovery Build |
|----------|----------------|----------------|
| Tasks | 6+ concurrent | 3 essential |
| Memory Management | Basic | Emergency monitoring |
| Cellular | Single provider | Multi-provider strategy |
| Error Handling | Limited | Comprehensive recovery |
| UI | Complex navigation | Simplified essential pages |
| Debugging | Basic logging | Verbose with diagnostics |

### C. Emergency Contacts
- **Cellular Provider Support**: Contact your SIM provider
- **Hardware Support**: M5Stack technical support
- **Software Issues**: Development team escalation
- **Critical Outages**: Emergency response procedures

---

**Version**: 1.0  
**Created**: November 2025  
**Purpose**: Emergency deployment guide for GENPLC recovery build  
**Status**: Production Ready
