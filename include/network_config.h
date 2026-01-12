/*
 * Network Configuration Header
 * Enhanced network management with multi-carrier support
 * T-Mobile and Soracom APN configurations with roaming
 */

#ifndef NETWORK_CONFIG_H
#define NETWORK_CONFIG_H

#include <Arduino.h>
#include <cstdint>

// ============================================================================
// CARRIER CONFIGURATIONS
// ============================================================================
#define TMOBILE_APN "fast.t-mobile.com"
#define TMOBILE_USER ""
#define TMOBILE_PASS ""
#define TMOBILE_MCC "310"
#define TMOBILE_MNC "260"

#define SORACOM_APN "soracom.io"
#define SORACOM_USER "sora"
#define SORACOM_PASS "sora"
#define SORACOM_MCC "440"
#define SORACOM_MNC "01"

// ============================================================================
// NETWORK CONFIGURATION STRUCTURES
// ============================================================================
enum class NetworkCarrier : uint8_t {
    AUTO = 0,
    TMOBILE = 1,
    SORACOM = 2
};

enum class NetworkState : uint8_t {
    DISCONNECTED = 0,
    CONNECTING = 1,
    AUTHENTICATING = 2,
    CONNECTED = 3,
    ROAMING = 4,
    ERROR = 5,
    SWITCHING = 6
};

enum class ConnectionQuality : uint8_t {
    POOR = 0,
    FAIR = 1,
    GOOD = 2,
    EXCELLENT = 3
};

struct NetworkConfig {
    // Primary carrier settings
    NetworkCarrier preferred_carrier;
    NetworkCarrier current_carrier;
    bool enable_roaming;
    bool enable_auto_switching;
    
    // APN configurations
    char primary_apn[48];
    char primary_user[32];
    char primary_pass[32];
    char secondary_apn[48];
    char secondary_user[32];
    char secondary_pass[32];
    
    // Switching thresholds
    int8_t switch_rssi_threshold;      // RSSI threshold for carrier switch
    uint8_t min_signal_quality;        // Minimum acceptable signal quality
    uint32_t switch_cooldown_ms;        // Minimum time between switches
    uint32_t connection_timeout_ms;       // Connection attempt timeout
    
    // Health monitoring
    uint32_t health_check_interval_ms;   // Health check frequency
    uint32_t speed_test_interval_ms;     // Network speed test frequency
    uint16_t min_upload_speed_kbps;     // Minimum acceptable upload speed
    uint16_t min_download_speed_kbps;   // Minimum acceptable download speed
    
    // Data buffering
    uint32_t buffer_size_limit;          // Maximum buffer size in bytes
    uint32_t buffer_flush_interval_ms;    // Auto-flush interval
    uint8_t buffer_priority_levels;       // Number of priority levels
    bool enable_compression;              // Enable data compression
};

struct NetworkStats {
    // Current connection info
    NetworkState state;
    NetworkCarrier carrier;
    bool is_roaming;
    int8_t rssi;
    uint8_t signal_quality;
    uint32_t signal_bars;
    
    // Performance metrics
    uint32_t uptime_ms;
    uint32_t last_switch_time;
    uint8_t switch_count;
    uint32_t failed_connections;
    uint32_t successful_connections;
    
    // Speed test results
    uint16_t upload_speed_kbps;
    uint16_t download_speed_kbps;
    uint32_t last_speed_test;
    uint32_t latency_ms;
    
    // Data usage
    uint64_t bytes_sent;
    uint64_t bytes_received;
    uint64_t last_usage_reset;
    
    // Network identifiers
    char network_name[32];
    char mcc[4];
    char mnc[4];
    char cell_id[16];
    char lac[8];
};

// ============================================================================
// BUFFER MANAGEMENT
// ============================================================================
enum class BufferPriority : uint8_t {
    BUFFER_CRITICAL = 0,
    BUFFER_HIGH = 1,
    BUFFER_NORMAL = 2,
    BUFFER_LOW = 3
};

struct BufferEntry {
    uint32_t timestamp;
    BufferPriority priority;
    uint16_t size;
    char* data;
    bool is_compressed;
};

struct NetworkBuffer {
    BufferEntry* entries;
    uint16_t capacity;
    uint16_t count;
    uint32_t total_size;
    uint32_t max_size;
    uint8_t priority_levels[4];  // Count per priority level
    bool overflow;
};

// ============================================================================
// CONNECTION HEALTH MONITORING
// ============================================================================
struct HealthMetrics {
    uint32_t last_ping_time;
    uint32_t ping_success_count;
    uint32_t ping_failure_count;
    uint32_t average_latency_ms;
    uint32_t max_latency_ms;
    
    uint32_t connection_drops;
    uint32_t auto_reconnects;
    uint32_t manual_reconnects;
    
    float packet_loss_rate;
    uint32_t last_quality_update;
    ConnectionQuality quality;
    
    uint32_t uptime_percentage;
    uint32_t downtime_duration_ms;
    uint32_t last_downtime_start;
};

// ============================================================================
// DEFAULT CONFIGURATIONS
// ============================================================================
#define DEFAULT_NETWORK_CONFIG { \
    .preferred_carrier = NetworkCarrier::AUTO, \
    .current_carrier = NetworkCarrier::AUTO, \
    .enable_roaming = true, \
    .enable_auto_switching = true, \
    .primary_apn = TMOBILE_APN, \
    .primary_user = TMOBILE_USER, \
    .primary_pass = TMOBILE_PASS, \
    .secondary_apn = SORACOM_APN, \
    .secondary_user = SORACOM_USER, \
    .secondary_pass = SORACOM_PASS, \
    .switch_rssi_threshold = -85, \
    .min_signal_quality = 10, \
    .switch_cooldown_ms = 300000, \
    .connection_timeout_ms = 30000, \
    .health_check_interval_ms = 10000, \
    .speed_test_interval_ms = 300000, \
    .min_upload_speed_kbps = 100, \
    .min_download_speed_kbps = 500, \
    .buffer_size_limit = 65536, \
    .buffer_flush_interval_ms = 5000, \
    .buffer_priority_levels = 4, \
    .enable_compression = true \
}

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================
inline const char* carrierToString(NetworkCarrier carrier) {
    switch (carrier) {
        case NetworkCarrier::TMOBILE: return "T-Mobile";
        case NetworkCarrier::SORACOM: return "Soracom";
        case NetworkCarrier::AUTO: return "Auto";
        default: return "Unknown";
    }
}

inline const char* stateToString(NetworkState state) {
    switch (state) {
        case NetworkState::DISCONNECTED: return "Disconnected";
        case NetworkState::CONNECTING: return "Connecting";
        case NetworkState::AUTHENTICATING: return "Authenticating";
        case NetworkState::CONNECTED: return "Connected";
        case NetworkState::ROAMING: return "Roaming";
        case NetworkState::ERROR: return "Error";
        case NetworkState::SWITCHING: return "Switching";
        default: return "Unknown";
    }
}

inline const char* qualityToString(ConnectionQuality quality) {
    switch (quality) {
        case ConnectionQuality::POOR: return "Poor";
        case ConnectionQuality::FAIR: return "Fair";
        case ConnectionQuality::GOOD: return "Good";
        case ConnectionQuality::EXCELLENT: return "Excellent";
        default: return "Unknown";
    }
}

inline ConnectionQuality rssiToQuality(int8_t rssi) {
    if (rssi >= -70) return ConnectionQuality::EXCELLENT;
    if (rssi >= -80) return ConnectionQuality::GOOD;
    if (rssi >= -90) return ConnectionQuality::FAIR;
    return ConnectionQuality::POOR;
}

inline bool isNetworkHealthy(const NetworkStats& stats) {
    return (stats.state == NetworkState::CONNECTED || stats.state == NetworkState::ROAMING) &&
           stats.signal_quality >= 10 &&
           rssiToQuality(stats.rssi) >= ConnectionQuality::FAIR;
}

#endif // NETWORK_CONFIG_H
