/*
 * Network Manager Header
 * Enhanced network management with multi-carrier support and intelligent switching
 */

#ifndef NETWORK_MANAGER_H
#define NETWORK_MANAGER_H

#include "../catm_gnss/catm_gnss_module.h"
#include "../../../include/network_config.h"
#include "../../../include/memory_pool.h"
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

// ============================================================================
// NETWORK MANAGER CLASS
// ============================================================================
class NetworkManager {
private:
    NetworkConfig config;
    NetworkStats stats;
    HealthMetrics healthMetrics;
    NetworkBuffer buffer;
    
    // Synchronization
    SemaphoreHandle_t managerMutex;
    
    // Task handles
    TaskHandle_t healthCheckTask;
    TaskHandle_t speedTestTask;
    TaskHandle_t bufferFlushTask;
    
    // Internal methods
    NetworkCarrier selectOptimalCarrier() const;
    NetworkCarrier getSecondaryCarrier() const;
    void getApnConfig(NetworkCarrier carrier, const char*& apn, 
                      const char*& user, const char*& pass) const;
    bool shouldSwitchCarrier() const;
    ConnectionQuality calculateConnectionQuality() const;
    
    // Low-level operations (to be implemented in cellular module)
    bool performConnection(const char* apn, const char* user, const char* pass);
    bool performDisconnection();
    bool performPingTest();
    uint16_t measureUploadSpeed();
    uint16_t measureDownloadSpeed();
    bool transmitData(const char* data, uint16_t size);
    void updateSignalQuality();
    
    // Buffer management
    bool reinitializeBuffer();
    bool evictLowPriorityEntries(uint32_t requiredSpace);
    uint16_t findFreeBufferSlot();
    
    // Synchronization helpers
    bool takeMutex() const;
    void giveMutex() const;
    
    // Task scheduling
    void scheduleHealthCheck();
    void scheduleSpeedTest();
    void scheduleBufferFlush();
    void scheduleCarrierSwitch(NetworkCarrier carrier);
    
public:
    static NetworkManager* instance;
    
    // Constructor/Destructor
    NetworkManager();
    ~NetworkManager();
    
    // Singleton access
    static NetworkManager* getInstance() {
        if (!instance) {
            instance = new NetworkManager();
        }
        return instance;
    }
    
    // Configuration management
    bool setConfig(const NetworkConfig& newConfig);
    void getConfig(NetworkConfig& outConfig) const { outConfig = config; }
    void getNetworkInfo(NetworkStats& outStats) const;
    
    // Connection management
    bool connect();
    bool disconnect();
    bool switchCarrier(NetworkCarrier targetCarrier);
    bool reconnect();
    bool isHealthy() const { return isNetworkHealthy(stats); }
    
    // Health monitoring
    void startHealthMonitoring();
    void stopHealthMonitoring();
    void performHealthCheck();
    void performSpeedTest();
    void getHealthMetrics(HealthMetrics& outMetrics) const { outMetrics = healthMetrics; }
    
    // Data buffering
    bool bufferData(const char* data, uint16_t size, BufferPriority priority = BufferPriority::BUFFER_NORMAL);
    bool flushBuffer();
    void clearBuffer();
    void getBufferStatus(NetworkBuffer& outBuffer) const { outBuffer = buffer; }
    
    // Status methods
    NetworkState getState() const { return stats.state; }
    NetworkCarrier getCurrentCarrier() const { return stats.carrier; }
    int8_t getRSSI() const { return stats.rssi; }
    ConnectionQuality getConnectionQuality() const { return rssiToQuality(stats.rssi); }
    bool isConnected() const { 
        return (stats.state == NetworkState::CONNECTED || stats.state == NetworkState::ROAMING); 
    }
    
    // Statistics
    uint64_t getBytesSent() const { return stats.bytes_sent; }
    uint64_t getBytesReceived() const { return stats.bytes_received; }
    uint32_t getUptime() const { return stats.uptime_ms; }
    uint8_t getSwitchCount() const { return stats.switch_count; }
    float getSuccessRate() const {
        uint32_t total = stats.successful_connections + stats.failed_connections;
        return total > 0 ? (float)stats.successful_connections / total * 100.0f : 0.0f;
    }
    
    // Advanced methods
    bool testConnection(NetworkCarrier carrier);
    void scanNetworks();
    void resetStatistics();
    void optimizeForPower();
    void optimizeForPerformance();
};

// ============================================================================
// TASK FUNCTION DECLARATIONS
// ============================================================================
void vTaskNetworkHealthCheck(void* pvParameters);
void vTaskNetworkSpeedTest(void* pvParameters);
void vTaskNetworkBufferFlush(void* pvParameters);

// ============================================================================
// EXTERNAL GLOBAL ACCESS
// ============================================================================
extern NetworkManager* networkManager;

// ============================================================================
// CONVENIENCE MACROS
// ============================================================================
#define NETWORK_CONNECT() networkManager->connect()
#define NETWORK_DISCONNECT() networkManager->disconnect()
#define NETWORK_SWITCH_CARRIER(carrier) networkManager->switchCarrier(carrier)
#define NETWORK_BUFFER(data, size, priority) networkManager->bufferData(data, size, priority)
#define NETWORK_FLUSH() networkManager->flushBuffer()
#define NETWORK_IS_HEALTHY() networkManager->isHealthy()
#define NETWORK_GET_STATS() networkManager->getNetworkInfo(networkStats)

#endif // NETWORK_MANAGER_H
