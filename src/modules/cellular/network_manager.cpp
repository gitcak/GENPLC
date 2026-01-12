/*
 * Network Manager Implementation
 * Enhanced network management with T-Mobile/Soracom carrier switching and roaming
 */

#include "network_manager.h"
#include "../../include/memory_pool.h"
#include "../../include/error_handler.h"
#include "../../modules/logging/log_buffer.h"
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

// ============================================================================
// GLOBAL INSTANCE
// ============================================================================
NetworkManager* NetworkManager::instance = nullptr;

// ============================================================================
// CONSTRUCTOR
// ============================================================================
NetworkManager::NetworkManager() {
    // Initialize configuration with defaults
    config.preferred_carrier = NetworkCarrier::AUTO;
    config.current_carrier = NetworkCarrier::AUTO;
    config.enable_roaming = true;
    config.enable_auto_switching = true;
    strcpy(config.primary_apn, TMOBILE_APN);
    strcpy(config.primary_user, TMOBILE_USER);
    strcpy(config.primary_pass, TMOBILE_PASS);
    strcpy(config.secondary_apn, SORACOM_APN);
    strcpy(config.secondary_user, SORACOM_USER);
    strcpy(config.secondary_pass, SORACOM_PASS);
    config.switch_rssi_threshold = -85;
    config.min_signal_quality = 10;
    config.switch_cooldown_ms = 300000;
    config.connection_timeout_ms = 30000;
    config.health_check_interval_ms = 10000;
    config.speed_test_interval_ms = 300000;
    config.min_upload_speed_kbps = 100;
    config.min_download_speed_kbps = 500;
    config.buffer_size_limit = 65536;
    config.buffer_flush_interval_ms = 5000;
    config.buffer_priority_levels = 4;
    config.enable_compression = true;
    
    // Initialize statistics
    memset(&stats, 0, sizeof(stats));
    stats.state = NetworkState::DISCONNECTED;
    stats.carrier = NetworkCarrier::AUTO;
    stats.last_switch_time = 0;
    stats.last_usage_reset = millis();
    
    // Initialize health metrics
    memset(&healthMetrics, 0, sizeof(healthMetrics));
    healthMetrics.last_quality_update = millis();
    
    // Initialize buffer
    memset(&buffer, 0, sizeof(buffer));
    buffer.capacity = config.buffer_size_limit / 64; // Approximate entries
    buffer.entries = nullptr;
    buffer.max_size = config.buffer_size_limit;
    
    // Initialize synchronization
    managerMutex = xSemaphoreCreateMutex();
    if (!managerMutex) {
        Serial.println("NetworkManager: Failed to create mutex");
    }
    
    Serial.println("NetworkManager: Initialized with multi-carrier support");
}

// ============================================================================
// DESTRUCTOR
// ============================================================================
NetworkManager::~NetworkManager() {
    if (buffer.entries) {
        for (uint16_t i = 0; i < buffer.count; i++) {
            if (buffer.entries[i].data) {
                POOL_FREE(buffer.entries[i].data);
            }
        }
        POOL_FREE(buffer.entries);
    }
    
    if (managerMutex) {
        vSemaphoreDelete(managerMutex);
    }
}

// ============================================================================
// CONFIGURATION MANAGEMENT
// ============================================================================
bool NetworkManager::setConfig(const NetworkConfig& newConfig) {
    if (!takeMutex()) return false;
    
    config = newConfig;
    
    // Reinitialize buffer if size changed
    if (buffer.max_size != config.buffer_size_limit) {
        reinitializeBuffer();
    }
    
    giveMutex();
    
    logbuf_printf("Network config updated: carrier=%s, roaming=%s, auto_switch=%s",
                 carrierToString(config.preferred_carrier),
                 config.enable_roaming ? "ON" : "OFF",
                 config.enable_auto_switching ? "ON" : "OFF");
    
    return true;
}

void NetworkManager::getNetworkInfo(NetworkStats& outStats) const {
    if (!takeMutex()) return;
    
    outStats = stats;
    outStats.uptime_ms = millis() - (outStats.last_switch_time > 0 ? outStats.last_switch_time : 0);
    
    giveMutex();
}

// ============================================================================
// CONNECTION MANAGEMENT
// ============================================================================
bool NetworkManager::connect() {
    if (!takeMutex()) return false;
    
    if (stats.state == NetworkState::CONNECTED || stats.state == NetworkState::ROAMING) {
        giveMutex();
        return true;
    }
    
    // Determine initial carrier
    NetworkCarrier targetCarrier = selectOptimalCarrier();
    
    stats.state = NetworkState::CONNECTING;
    stats.carrier = targetCarrier;
    
    logbuf_printf("NetworkManager: Connecting to %s...", carrierToString(targetCarrier));
    
    // Get APN configuration for target carrier
    const char* apn, *user, *pass;
    getApnConfig(targetCarrier, apn, user, pass);
    
    bool success = performConnection(apn, user, pass);
    
    if (success) {
        stats.state = NetworkState::CONNECTED;
        stats.successful_connections++;
        stats.last_switch_time = millis();
        
        logbuf_printf("NetworkManager: Connected to %s (RSSI: %d)",
                     carrierToString(targetCarrier), stats.rssi);
        
        // Start health monitoring
        scheduleHealthCheck();
        scheduleSpeedTest();
        
    } else {
        stats.state = NetworkState::ERROR;
        stats.failed_connections++;
        
        // Try secondary carrier if enabled
        if (config.enable_auto_switching && targetCarrier != getSecondaryCarrier()) {
            logbuf_printf("NetworkManager: Primary failed, trying secondary carrier...");
            targetCarrier = getSecondaryCarrier();
            success = performConnection(apn, user, pass);
            
            if (success) {
                stats.state = NetworkState::CONNECTED;
                stats.carrier = targetCarrier;
                stats.successful_connections++;
                stats.last_switch_time = millis();
                
                logbuf_printf("NetworkManager: Connected to secondary %s", carrierToString(targetCarrier));
            }
        }
        
        if (!success) {
            REPORT_ERROR(ERROR_COMMUNICATION_FAILED, ErrorSeverity::ERROR, ErrorCategory::NETWORK,
                      "Failed to connect to any carrier");
        }
    }
    
    giveMutex();
    return success;
}

bool NetworkManager::disconnect() {
    if (!takeMutex()) return false;
    
    if (stats.state == NetworkState::DISCONNECTED) {
        giveMutex();
        return true;
    }
    
    logbuf_printf("NetworkManager: Disconnecting from %s", carrierToString(stats.carrier));
    
    // Perform disconnection
    bool success = performDisconnection();
    
    if (success) {
        stats.state = NetworkState::DISCONNECTED;
        logbuf_printf("NetworkManager: Disconnected successfully");
    } else {
        REPORT_ERROR(ERROR_COMMUNICATION_FAILED, ErrorSeverity::WARNING, ErrorCategory::NETWORK,
                      "Disconnection failed");
    }
    
    giveMutex();
    return success;
}

bool NetworkManager::switchCarrier(NetworkCarrier targetCarrier) {
    if (!config.enable_auto_switching) {
        return false;
    }
    
    // Check cooldown period
    uint32_t currentTime = millis();
    if (currentTime - stats.last_switch_time < config.switch_cooldown_ms) {
        logbuf_printf("NetworkManager: Switch cooldown active (%ums remaining)",
                     config.switch_cooldown_ms - (currentTime - stats.last_switch_time));
        return false;
    }
    
    if (!takeMutex()) return false;
    
    logbuf_printf("NetworkManager: Switching from %s to %s",
                 carrierToString(stats.carrier), carrierToString(targetCarrier));
    
    stats.state = NetworkState::SWITCHING;
    
    // Disconnect from current carrier
    performDisconnection();
    
    // Connect to new carrier
    const char* apn, *user, *pass;
    getApnConfig(targetCarrier, apn, user, pass);
    
    bool success = performConnection(apn, user, pass);
    
    if (success) {
        stats.carrier = targetCarrier;
        stats.state = NetworkState::CONNECTED;
        stats.switch_count++;
        stats.last_switch_time = currentTime;
        
        logbuf_printf("NetworkManager: Successfully switched to %s", carrierToString(targetCarrier));
    } else {
        stats.state = NetworkState::ERROR;
        stats.failed_connections++;
        
        REPORT_ERROR(ERROR_COMMUNICATION_FAILED, ErrorSeverity::ERROR, ErrorCategory::NETWORK,
                      "Carrier switch failed");
        
        // Try to reconnect to previous carrier
        NetworkCarrier previousCarrier = getSecondaryCarrier();
        getApnConfig(previousCarrier, apn, user, pass);
        performConnection(apn, user, pass);
        stats.carrier = previousCarrier;
        stats.state = NetworkState::CONNECTED;
    }
    
    giveMutex();
    return success;
}

// ============================================================================
// HEALTH MONITORING
// ============================================================================
void NetworkManager::performHealthCheck() {
    if (!takeMutex()) return;
    
    if (stats.state != NetworkState::CONNECTED && stats.state != NetworkState::ROAMING) {
        giveMutex();
        return;
    }
    
    uint32_t startTime = millis();
    bool pingSuccess = performPingTest();
    uint32_t latency = millis() - startTime;
    
    // Update health metrics
    healthMetrics.last_ping_time = startTime;
    
    if (pingSuccess) {
        healthMetrics.ping_success_count++;
        healthMetrics.average_latency_ms = 
            (healthMetrics.average_latency_ms * (healthMetrics.ping_success_count - 1) + latency) /
            healthMetrics.ping_success_count;
        
        if (latency > healthMetrics.max_latency_ms) {
            healthMetrics.max_latency_ms = latency;
        }
    } else {
        healthMetrics.ping_failure_count++;
    }
    
    // Calculate packet loss rate
    uint32_t totalPings = healthMetrics.ping_success_count + healthMetrics.ping_failure_count;
    if (totalPings > 0) {
        healthMetrics.packet_loss_rate = 
            (float)healthMetrics.ping_failure_count / totalPings * 100.0f;
    }
    
    // Update connection quality
    healthMetrics.quality = calculateConnectionQuality();
    healthMetrics.last_quality_update = millis();
    
    // Check if carrier switch is needed
    if (config.enable_auto_switching && shouldSwitchCarrier()) {
        NetworkCarrier betterCarrier = selectOptimalCarrier();
        if (betterCarrier != stats.carrier) {
            logbuf_printf("NetworkManager: Health check suggests switching to %s",
                         carrierToString(betterCarrier));
            // Schedule carrier switch (don't block health check)
            scheduleCarrierSwitch(betterCarrier);
        }
    }
    
    // Update signal quality
    updateSignalQuality();
    
    giveMutex();
    
    logbuf_printf("Health check: latency=%ums, loss=%.1f%%, quality=%s",
                 latency, healthMetrics.packet_loss_rate, qualityToString(healthMetrics.quality));
}

ConnectionQuality NetworkManager::calculateConnectionQuality() const {
    // Factor in signal strength, latency, and packet loss
    ConnectionQuality signalQuality = rssiToQuality(stats.rssi);
    
    // Penalty for high latency
    if (healthMetrics.average_latency_ms > 5000) {
        signalQuality = (ConnectionQuality)max(0, (int)signalQuality - 2);
    } else if (healthMetrics.average_latency_ms > 2000) {
        signalQuality = (ConnectionQuality)max(0, (int)signalQuality - 1);
    }
    
    // Penalty for packet loss
    if (healthMetrics.packet_loss_rate > 10.0f) {
        signalQuality = (ConnectionQuality)max(0, (int)signalQuality - 2);
    } else if (healthMetrics.packet_loss_rate > 5.0f) {
        signalQuality = (ConnectionQuality)max(0, (int)signalQuality - 1);
    }
    
    return signalQuality;
}

void NetworkManager::performSpeedTest() {
    if (!takeMutex()) return;
    
    if (stats.state != NetworkState::CONNECTED && stats.state != NetworkState::ROAMING) {
        giveMutex();
        return;
    }
    
    logbuf_printf("NetworkManager: Performing speed test...");
    
    uint32_t startTime = millis();
    uint16_t uploadSpeed = measureUploadSpeed();
    uint32_t uploadTime = millis() - startTime;
    
    startTime = millis();
    uint16_t downloadSpeed = measureDownloadSpeed();
    uint32_t downloadTime = millis() - startTime;
    
    stats.upload_speed_kbps = uploadSpeed;
    stats.download_speed_kbps = downloadSpeed;
    stats.last_speed_test = millis();
    
    // Check if speeds meet minimum requirements
    bool speedAdequate = (uploadSpeed >= config.min_upload_speed_kbps) &&
                         (downloadSpeed >= config.min_download_speed_kbps);
    
    if (!speedAdequate && config.enable_auto_switching) {
        logbuf_printf("Speed test inadequate: UL=%ukbps, DL=%ukbps (min: %u/%u)",
                     uploadSpeed, downloadSpeed,
                     config.min_upload_speed_kbps, config.min_download_speed_kbps);
        
        NetworkCarrier betterCarrier = selectOptimalCarrier();
        if (betterCarrier != stats.carrier) {
            scheduleCarrierSwitch(betterCarrier);
        }
    }
    
    giveMutex();
    
    logbuf_printf("Speed test: UL=%ukbps (%ums), DL=%ukbps (%ums)",
                 uploadSpeed, uploadTime, downloadSpeed, downloadTime);
}

// ============================================================================
// DATA BUFFERING
// ============================================================================
bool NetworkManager::bufferData(const char* data, uint16_t size, BufferPriority priority) {
    if (!takeMutex()) return false;
    
    // Check buffer capacity
    if (buffer.total_size + size > buffer.max_size) {
        // Remove low priority entries to make space
        if (!evictLowPriorityEntries(size)) {
            buffer.overflow = true;
            giveMutex();
            
            REPORT_ERROR(ERROR_MEMORY_FAULT, ErrorSeverity::WARNING, ErrorCategory::NETWORK,
                          "Network buffer overflow");
            return false;
        }
    }
    
    // Allocate new entry
    if (!buffer.entries) {
        buffer.entries = (BufferEntry*)POOL_ALLOC(buffer.capacity * sizeof(BufferEntry));
        if (!buffer.entries) {
            giveMutex();
            return false;
        }
        memset(buffer.entries, 0, buffer.capacity * sizeof(BufferEntry));
    }
    
    // Find free slot
    uint16_t slot = findFreeBufferSlot();
    if (slot == 0xFFFF) {
        buffer.overflow = true;
        giveMutex();
        return false;
    }
    
    // Fill entry
    BufferEntry& entry = buffer.entries[slot];
    entry.timestamp = millis();
    entry.priority = priority;
    entry.size = size;
    entry.data = (char*)POOL_ALLOC(size);
    entry.is_compressed = false;
    
    if (!entry.data) {
        giveMutex();
        return false;
    }
    
    memcpy(entry.data, data, size);
    
    // Update buffer statistics
    buffer.count++;
    buffer.total_size += size;
    buffer.priority_levels[(int)priority]++;
    
    giveMutex();
    
    logbuf_printf("Data buffered: %u bytes, priority=%u (total: %u/%u)",
                 size, (int)priority, buffer.total_size, buffer.max_size);
    
    return true;
}

bool NetworkManager::flushBuffer() {
    if (!takeMutex()) return false;
    
    bool success = true;
    uint16_t flushedCount = 0;
    uint16_t totalBytes = 0;
    
    // Flush entries by priority (CRITICAL first)
    for (int priority = 0; priority < 4 && buffer.count > 0; priority++) {
        for (uint16_t i = 0; i < buffer.capacity && flushedCount < buffer.count; i++) {
            BufferEntry& entry = buffer.entries[i];
            
            if (entry.data && entry.priority == (BufferPriority)priority) {
                // Try to send data
                bool sendSuccess = transmitData(entry.data, entry.size);
                
                if (sendSuccess) {
                    totalBytes += entry.size;
                    flushedCount++;
                    
                    // Free entry
                    POOL_FREE(entry.data);
                    entry.data = nullptr;
                    buffer.count--;
                    buffer.total_size -= entry.size;
                    buffer.priority_levels[priority]--;
                } else {
                    success = false;
                    break; // Stop flushing on first failure
                }
            }
        }
    }
    
    giveMutex();
    
    if (flushedCount > 0) {
        logbuf_printf("Buffer flush: %u entries, %u bytes, success=%s",
                     flushedCount, totalBytes, success ? "YES" : "NO");
    }
    
    return success;
}

// ============================================================================
// UTILITY METHODS
// ============================================================================
NetworkCarrier NetworkManager::selectOptimalCarrier() const {
    if (config.preferred_carrier != NetworkCarrier::AUTO) {
        return config.preferred_carrier;
    }
    
    // Auto-select based on current conditions
    if (stats.rssi < config.switch_rssi_threshold) {
        return getSecondaryCarrier(); // Use secondary for better coverage
    }
    
    return (NetworkCarrier)1; // Default to primary (T-Mobile)
}

NetworkCarrier NetworkManager::getSecondaryCarrier() const {
    return (config.preferred_carrier == NetworkCarrier::TMOBILE) ? 
           NetworkCarrier::SORACOM : NetworkCarrier::TMOBILE;
}

void NetworkManager::getApnConfig(NetworkCarrier carrier, const char*& apn, 
                               const char*& user, const char*& pass) const {
    bool isPrimary = (carrier == config.preferred_carrier) || 
                    (config.preferred_carrier == NetworkCarrier::AUTO && carrier == NetworkCarrier::TMOBILE);
    
    if (isPrimary) {
        apn = config.primary_apn;
        user = config.primary_user;
        pass = config.primary_pass;
    } else {
        apn = config.secondary_apn;
        user = config.secondary_user;
        pass = config.secondary_pass;
    }
}

bool NetworkManager::shouldSwitchCarrier() const {
    // Check various conditions for carrier switching
    
    // Poor signal quality
    if (rssiToQuality(stats.rssi) < ConnectionQuality::FAIR) {
        return true;
    }
    
    // High packet loss
    if (healthMetrics.packet_loss_rate > 5.0f) {
        return true;
    }
    
    // Poor performance
    if (stats.upload_speed_kbps < config.min_upload_speed_kbps ||
        stats.download_speed_kbps < config.min_download_speed_kbps) {
        return true;
    }
    
    return false;
}

// ============================================================================
// SYNCHRONIZATION HELPERS
// ============================================================================
bool NetworkManager::takeMutex() const {
    return (managerMutex && xSemaphoreTake(managerMutex, pdMS_TO_TICKS(1000)) == pdTRUE);
}

void NetworkManager::giveMutex() const {
    if (managerMutex) {
        xSemaphoreGive(managerMutex);
    }
}

// ============================================================================
// TASK SCHEDULING
// ============================================================================
void NetworkManager::scheduleCarrierSwitch(NetworkCarrier carrier) {
    // Simple implementation - just switch immediately
    // In a full implementation, this would schedule a task to avoid blocking
    logbuf_printf("NetworkManager: Scheduling switch to %s", carrierToString(carrier));
    switchCarrier(carrier);
}
