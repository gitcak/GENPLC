# GENPLC Project Refresh Plan

## Executive Summary

This document outlines the complete refresh strategy for the GENPLC project to resolve critical issues that have halted development and production.

## Current Critical Issues

### 🔴 Blocking Issues
1. **Cellular PDP Activation Failure**: SIM7080G connects to AT&T but PDP context never assigns IP (stays 0.0.0.0)
2. **Mutex Contention**: Frequent "Failed to take mutex" errors causing system timeouts
3. **Memory Pressure**: Tasks showing critically low stack margins (< 2KB free)
4. **Task Starvation**: Multiple FreeRTOS tasks competing for limited resources
5. **Hardware Compatibility**: I2C device initialization failures and library conflicts

### 📊 System Status
- **Flash Usage**: 79.7% (1,045,097 / 1,310,720 bytes) - Approaching limits
- **RAM Usage**: 16.8% (55,024 / 327,680 bytes) - Showing fragmentation
- **Tasks**: 6+ concurrent FreeRTOS tasks causing resource contention
- **Dependencies**: Complex library chain with potential conflicts

## Refresh Strategy

### Phase 1: Emergency Stabilization (Week 1)
**Goal**: Restore basic functionality and stop the bleeding

#### 1.1 Immediate Actions (First 48 Hours)
- [ ] Create recovery build with minimal functionality
- [ ] Isolate cellular module testing from main system
- [ ] Fix critical mutex contention issues
- [ ] Implement emergency memory management
- [ ] Create standalone cellular test sketch

#### 1.2 System Stabilization
- [ ] Simplify task architecture (6+ → 3 tasks)
- [ ] Fix memory leaks and fragmentation
- [ ] Implement robust error handling
- [ ] Add comprehensive logging for debugging
- [ ] Create backup and recovery procedures

### Phase 2: Architecture Redesign (Week 2)
**Goal**: Build a maintainable and scalable foundation

#### 2.1 Task Architecture Simplification
```
Current Architecture:
├── Button Task (Core 0)
├── Display Task (Core 1)
├── Status Bar Task (Core 1)
├── StampPLC Task (Core 0)
├── CatMGNSS Task (Core 0)
├── Storage Task (Core 1)
└── PWRCAN Task (if enabled)

New Architecture:
├── Main Task (Core 1) - UI, Buttons, Status, StampPLC
├── Communication Task (Core 0) - Cellular, GNSS, Storage
└── Monitor Task (Core 0) - System monitoring, watchdog
```

#### 2.2 Communication Layer Refactor
- [ ] Single communication task for all I/O operations
- [ ] Non-blocking command queue with timeout handling
- [ ] State machine for connection management
- [ ] Automatic retry with exponential backoff
- [ ] Graceful degradation modes

#### 2.3 UI Simplification
- [ ] Essential pages only (Status, GPS, Cellular, Settings)
- [ ] Lightweight graphics rendering
- [ ] Event-driven updates instead of periodic redraws
- [ ] Remove complex navigation and animations

### Phase 3: Cellular Solution Implementation (Week 3)
**Goal**: Resolve connectivity issues and provide reliable communication

#### 3.1 Multi-Provider Strategy
- [ ] **Primary**: Soracom SIM (resolve current issues)
  - [ ] Verify SIM activation and data quota
  - [ ] Test multiple APN configurations
  - [ ] Contact Soracom support with detailed logs
- [ ] **Fallback 1**: Hologram SIM (testing)
- [ ] **Fallback 2**: Native AT&T IoT SIM
- [ ] **Backup**: ESP32 WiFi for testing and fallback

#### 3.2 Connection Robustness
- [ ] Smart APN selection and automatic switching
- [ ] Network health monitoring and proactive reconnection
- [ ] Graceful degradation based on connection quality
- [ ] OTA update capability over multiple transport methods

#### 3.3 Hardware Validation
- [ ] Test with alternative SIM7080G modules
- [ ] Validate power supply and antenna configuration
- [ ] Test different UART configurations
- [ ] Create hardware diagnostic suite

### Phase 4: Production Hardening (Week 4)
**Goal**: Prepare for reliable production deployment

#### 4.1 Reliability Features
- [ ] Hardware and software watchdog implementation
- [ ] Crash detection and automatic recovery
- [ ] Comprehensive offline data storage
- [ ] Remote device management and monitoring

#### 4.2 Performance Optimization
- [ ] Memory management optimization
- [ ] Power consumption optimization
- [ ] Thermal management and protection
- [ ] Boot time optimization

#### 4.3 Production Readiness
- [ ] Automated testing pipeline
- [ ] Documentation update and review
- [ ] Deployment procedures and tools
- [ ] Monitoring and alerting setup

## Implementation Timeline

### Week 1: Emergency Response
- **Day 1-2**: Create recovery build and isolate cellular issues
- **Day 3-4**: Fix mutex and memory issues
- **Day 5-7**: Stabilize system and create backup procedures

### Week 2: Architecture Refactor
- **Day 8-10**: Implement simplified task architecture
- **Day 11-12**: Refactor communication layer
- **Day 13-14**: Simplify UI and test integration

### Week 3: Cellular Solution
- **Day 15-17**: Implement multi-provider strategy
- **Day 18-19**: Connection robustness features
- **Day 20-21**: Hardware validation and testing

### Week 4: Production Hardening
- **Day 22-24**: Reliability and monitoring features
- **Day 25-26**: Performance optimization
- **Day 27-28**: Production readiness and deployment

## Risk Mitigation

### High-Risk Areas
1. **Cellular Module Dependency**
   - Risk: Single point of failure
   - Mitigation: Multiple providers and fallback options

2. **Memory Constraints**
   - Risk: System instability due to memory pressure
   - Mitigation: Aggressive optimization and monitoring

3. **Task Scheduling**
   - Risk: Resource contention and starvation
   - Mitigation: Simplified architecture and priority tuning

4. **Hardware Reliability**
   - Risk: Component failures in production
   - Mitigation: Enhanced error handling and diagnostics

### Backup Plans
- **Alternative Hardware**: Ready backup SIM7080G modules and development boards
- **Different Communication**: LoRaWAN or satellite communication options
- **Simplified Deployment**: Reduced feature set if needed for rapid deployment
- **Manual Recovery**: Local programming and configuration tools

## Success Metrics

### Technical Metrics
- **System Uptime**: >99% continuous operation
- **Connectivity Success**: >95% successful data transmission
- **Memory Usage**: <80% RAM utilization with stable fragmentation
- **Boot Time**: <30 seconds to full operational status

### Business Metrics
- **Production Readiness**: Device deployment ready in 4 weeks
- **Remote Management**: Full cloud control and monitoring capability
- **Scalability**: Support for 100+ devices with centralized management
- **Maintainability**: Clear documentation and automated update procedures

## Deliverables

1. **Recovery Build**: Minimal functional firmware for immediate deployment
2. **Refreshed Architecture**: Simplified, maintainable codebase
3. **Cellular Solution**: Reliable connectivity with multiple fallback options
4. **Production Package**: Complete deployment and management solution
5. **Documentation**: Updated technical and user documentation
6. **Testing Suite**: Automated tests for validation and regression

## Next Steps

1. **Immediate**: Create recovery build and isolate cellular issues
2. **Short Term**: Stabilize system and simplify architecture
3. **Medium Term**: Implement robust cellular solution
4. **Long Term**: Production hardening and deployment

This refresh plan addresses both the immediate technical blockers and provides a robust foundation for long-term production deployment.
