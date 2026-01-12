/*
 * GENPLC Cellular Manager
 * 
 * Multi-provider cellular connectivity management
 * Supports multiple APN configurations and provider strategies
 * 
 * Key features:
 * - Multi-provider SIM support (Soracom, Hologram, AT&T, etc.)
 * - Automatic APN selection and switching
 * - Connection health monitoring and recovery
 * - Graceful degradation modes
 * - Comprehensive error handling and logging
 * 
 * Created: November 2025
 * Purpose: Resolve PDP activation failures through provider diversity
 */

#ifndef CELLULAR_MANAGER_H
#define CELLULAR_MANAGER_H

#include <Arduino.h>
#include <HardwareSerial.h>
#include <TinyGsmClient.h>
#include <ArduinoJson.h>
#include "../catm_gnss/catm_gnss_module.h"

// ============================================================================
// PROVIDER CONFIGURATIONS
// ============================================================================

enum class CellularProvider {
    SORACOM,
    HOLOGRAM,
    ATT_IOT,
    TMOBILE_IOT,
    VERIZON_IOT,
    CUSTOM,
    AUTO_DETECT,
    PROVIDER_COUNT
};

struct ProviderConfig {
    CellularProvider provider;
    const char* name;
    const char* primaryApn;
    const char* secondaryApn;
    const char* tertiaryApn;
    const char* username;
    const char* password;
    bool requiresRoaming;
    uint32_t pdpTimeout;
    uint32_t retryDelay;
    uint8_t maxRetries;
};

// ============================================================================
// CONNECTION STATES
// ============================================================================

enum class CellularState {
    DISCONNECTED,
    INITIALIZING,
    SEARCHING,
    REGISTERED,
    ATTACHING,
    ACTIVATING,
    CONNECTED,
    DEGRADING,
    RECOVERING,
    FAILED
};

enum class ConnectionQuality {
    EXCELLENT,
    GOOD,
    FAIR,
    POOR,
    NONE
};

// ============================================================================
// CELLULAR MANAGER CLASS
// ============================================================================

class CellularManager {
public:
    CellularManager();
    ~CellularManager();

    // Initialization and setup
    bool begin(HardwareSerial* serial, int rxPin, int txPin, int baud = 115200);
    bool setProvider(CellularProvider provider);
    bool setCustomApn(const char* apn, const char* username = "", const char* password = "");
    void setTimeouts(uint32_t initTimeout, uint32_t connectTimeout, uint32_t pdpTimeout);

    // Connection management
    bool connect();
    bool disconnect();
    bool reconnect();
    bool isConnected() const;
    CellularState getState() const { return currentState; }
    ConnectionQuality getConnectionQuality() const { return currentQuality; }

    // Multi-provider strategies
    bool attemptAllProviders();
    bool attemptProvider(CellularProvider provider);
    bool switchProvider(CellularProvider newProvider);
    CellularProvider getBestProvider() const;
    bool autoDetectProvider();

    // Network information
    String getProviderName() const;
    String getApnName() const;
    String getIpAddress() const;
    String getOperator() const;
    int getSignalStrength() const; // RSSI in dBm
    int getSignalQuality() const; // CSQ (0-31)
    bool isRoaming() const;
    String getNetworkType() const;

    // Advanced features
    bool testConnectivity();
    bool pingHost(const char* host);
    bool makeHttpRequest(const char* url, String& response);

    // Health monitoring
    void updateHealth();
    bool isHealthy() const;
    uint32_t getUptime() const;
    uint32_t getLastSuccessTime() const;
    uint32_t getFailureCount() const;
    float getSuccessRate() const;

    // Configuration and diagnostics
    void setDebugMode(bool enabled) { debugMode = enabled; }
    void enableVerboseLogging(bool enabled) { verboseLogging = enabled; }
    String getDiagnostics() const;
    JsonObject getDiagnosticsJson(JsonDocument& doc) const;
    void printDiagnostics() const;

    // Power management
    bool powerOn();
    bool powerOff();
    bool powerCycle(uint32_t delayMs = 2000);
    bool isPoweredOn() const;

private:
    // Hardware interface
    HardwareSerial* cellularSerial;
    int rxPin, txPin, baudRate;
    CatMGNSSModule* gsmModule;

    // Provider management
    CellularProvider currentProvider;
    ProviderConfig providerConfigs[(int)CellularProvider::PROVIDER_COUNT];
    int providerAttempts[(int)CellularProvider::PROVIDER_COUNT];
    uint32_t providerSuccessTimes[(int)CellularProvider::PROVIDER_COUNT];
    bool customApnSet;
    String customApn, customUsername, customPassword;

    // Connection state
    CellularState currentState;
    ConnectionQuality currentQuality;
    String ipAddress;
    String operatorName;
    String networkType;
    bool roamingStatus;
    uint32_t lastStateChange;

    // Health monitoring
    uint32_t connectionStartTime;
    uint32_t lastSuccessTime;
    uint32_t lastHealthCheck;
    uint32_t failureCount;
    uint32_t successCount;
    uint32_t totalAttempts;
    uint32_t lastPingTime;
    bool lastPingResult;

    // Configuration
    uint32_t initTimeoutMs;
    uint32_t connectTimeoutMs;
    uint32_t pdpTimeoutMs;
    uint32_t healthCheckInterval;
    uint32_t pingInterval;
    const char* pingHost;

    // Debug and logging
    bool debugMode;
    bool verboseLogging;

    // Private methods
    void initializeProviderConfigs();
    const ProviderConfig* getProviderConfig(CellularProvider provider) const;
    bool initializeProvider(CellularProvider provider);
    bool attemptRegistration();
    bool attemptAttachment();
    bool attemptPdpActivation(const ProviderConfig* config);
    bool verifyConnection();
    void updateConnectionQuality();
    ConnectionQuality assessConnectionQuality(int rssi, int ber) const;
    void logEvent(const char* event, const char* details = "") const;
    void logError(const char* error, const char* details = "") const;
    void logDebug(const char* message, const char* details = "") const;
    void logVerbose(const char* message, const char* details = "") const;

    // AT command helpers
    bool sendAtCommand(const char* command, String& response, uint32_t timeout = 5000);
    bool sendAtCommandAndWait(const char* command, const char* expectedResponse, uint32_t timeout = 5000);
    void clearSerialBuffer();
    bool waitForResponse(const char* expected, uint32_t timeout = 5000);

    // Utility methods
    String stateToString(CellularState state) const;
    String qualityToString(ConnectionQuality quality) const;
    String providerToString(CellularProvider provider) const;
    int rssiToDbm(int csq) const;
    bool isValidIp(const String& ip) const;
    void resetStatistics();
    void updateProviderStatistics(CellularProvider provider, bool success);
};

// ============================================================================
// GLOBAL INSTANCE AND MACROS
// ============================================================================

extern CellularManager* g_cellularManager;

// Convenience macros
#define CELLULAR_CONNECTED() (g_cellularManager && g_cellularManager->isConnected())
#define CELLULAR_IP() (g_cellularManager ? g_cellularManager->getIpAddress() : "0.0.0.0")
#define CELLULAR_SIGNAL() (g_cellularManager ? g_cellularManager->getSignalStrength() : -999)
#define CELLULAR_PROVIDER() (g_cellularManager ? g_cellularManager->getProviderName() : "Unknown")

// ============================================================================
// STATIC PROVIDER CONFIGURATIONS
// ============================================================================

// Soracom configurations
#define SORACOM_PRIMARY_APN "soracom.io"
#define SORACOM_SECONDARY_APN "iot.soracom.io"
#define SORACOM_TERTIARY_APN "du.soracom.io"

// Hologram configurations
#define HOLOGRAM_PRIMARY_APN "hologram"
#define HOLOGRAM_USERNAME ""
#define HOLOGRAM_PASSWORD ""

// AT&T IoT configurations
#define ATT_PRIMARY_APN " Broadband"
#define ATT_SECONDARY_APN "broadband"
#define ATT_USERNAME ""
#define ATT_PASSWORD ""

// Default timeouts and retries
#define DEFAULT_INIT_TIMEOUT 30000
#define DEFAULT_CONNECT_TIMEOUT 60000
#define DEFAULT_PDP_TIMEOUT 45000
#define DEFAULT_RETRY_DELAY 5000
#define DEFAULT_MAX_RETRIES 3
#define DEFAULT_HEALTH_CHECK_INTERVAL 30000
#define DEFAULT_PING_INTERVAL 60000

#endif // CELLULAR_MANAGER_H
