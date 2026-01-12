#ifndef CATM_GNSS_MODULE_H
#define CATM_GNSS_MODULE_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <ArduinoJson.h>
#include <time.h>
#include "cell_status.h"
#include "gnss_status.h"

// ============================================================================
// CATM+GNSS MODULE STATES
// ============================================================================
enum class CatMGNSSState {
    INITIALIZING,
    READY,
    CONNECTED,
    ERROR
};

// ============================================================================
// GNSS DATA STRUCTURE
// ============================================================================
struct GNSSData {
    // Position
    double latitude;
    double longitude;
    double altitude;
    float speed;
    float course;
    
    // Time
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    uint8_t day;
    uint8_t month;
    uint16_t year;
    
    // Status
    uint8_t satellites;
    bool isValid;
    uint32_t lastUpdate;
    // Dilution of precision / accuracies (if available)
    float hdop;   // Horizontal DOP
    float pdop;   // Position DOP
    float vdop;   // Vertical DOP
    float hAcc;   // Horizontal Position Accuracy (meters)
    float vAcc;   // Vertical Position Accuracy (meters)
};

// ============================================================================
// CELLULAR DATA STRUCTURE
// ============================================================================
struct CellularData {
    // Connection info
    bool isConnected;
    String operatorName;
    int8_t signalStrength;
    String imei;
    String ipAddress;
    String apn;
    
    // Status
    bool isRegistered;
    uint8_t registrationState;
    uint32_t lastUpdate;
    bool isValid;
    bool hasIpAddress;
    
    // Error / diagnostics
    uint32_t errorCount;
    String lastDetachReason;

    // Throughput metrics (bytes cumulative since attach)
    uint64_t txBytes;
    uint64_t rxBytes;
    // Instantaneous throughput estimates (bytes per second)
    uint32_t txBps;
    uint32_t rxBps;
};

// ============================================================================
// M5UNIT-CATM+GNSS PIN CONFIGURATION (Grove Port C on StamPLC)
// ============================================================================
// ** ALL CATM/GNSS TRAFFIC GOES OVER GROVE PORT C ON THE STAMPLC **
// M5Unit-CatM+GNSS uses UART on Port C
// Port C: G5 (Yellow) = RX, G4 (White) = TX, 5V/GND for power
// Note: Unit pin map shows UART_RX (Yellow) and UART_TX (White) from the UNIT's perspective.
// Host RX must connect to UNIT TX (White), and host TX must connect to UNIT RX (Yellow).
// Therefore: Host RX=G4 (White), Host TX=G5 (Yellow).
#define CATM_GNSS_RX_PIN                 4        // Host RX <- Unit TX (White / G4) [Grove Port C]
#define CATM_GNSS_TX_PIN                 5        // Host TX -> Unit RX (Yellow / G5) [Grove Port C]
#define CATM_GNSS_BAUD_RATE              115200   // SIM7080G baud rate (8N1)

// ============================================================================
// AT COMMAND RESPONSES
// ============================================================================
#define AT_OK                           "OK"
#define AT_ERROR                        "ERROR"
#define AT_READY                        "READY"
#define AT_CME_ERROR                    "+CME ERROR"

#ifndef CATM_GNSS_ENABLE_PORTA_PROBE
#define CATM_GNSS_ENABLE_PORTA_PROBE 0
#endif

// ============================================================================
// CATM+GNSS MODULE CLASS
// ============================================================================
class CatMGNSSModule {
private:
    // Hardware interface
    HardwareSerial* serialModule;
    
    // FreeRTOS components
    SemaphoreHandle_t serialMutex;
    
    // Module state
    CatMGNSSState state;
    GNSSData gnssData;
    CellularData cellularData;
    bool isInitialized;

    String lastError_;
    int lastProbeRx_ = -1;
    int lastProbeTx_ = -1;
    
    // AT command handling
    bool sendATCommand(const String& command, String& response, uint32_t timeout = 1000);
    bool sendATCommand(const char* command, char* response, size_t responseSize, uint32_t timeout = 1000);
    bool waitForResponse(String& response, uint32_t timeout = 1000);
    bool waitForResponse(char* response, size_t responseSize, uint32_t timeout = 1000);
    
    // Internal methods
    bool powerOnGNSS();
    bool powerOffGNSS();
    bool isGnssPowered(bool& powered);
    bool parseGNSSData(const String& data);
    bool parseGNSSData(const char* data);
    void updateState();
    // PDP attach helpers
    bool configureAPN();
    bool activatePDP(uint32_t timeoutMs);
    bool ensureRegistered(uint32_t maxWaitMs);
    bool applyBaselineConfig();

    void updateRegistrationState(uint8_t state);
    bool parseCNACTResponse(const String& resp, bool& anyActive, String& ipOut);
    void refreshDetachReason(const String& resp);
    bool parseNetDevStatus(const String& resp, uint64_t& txBytes, uint64_t& rxBytes, uint32_t& txBps, uint32_t& rxBps);
    bool updateNetworkStats();
    void resetNetworkStats();

public:
    const String& getLastError() const { return lastError_; }
    int getLastProbeRxPin() const { return lastProbeRx_; }
    int getLastProbeTxPin() const { return lastProbeTx_; }

    CatMGNSSModule();
    ~CatMGNSSModule();
    
    // Initialization
    bool begin();
    void shutdown();
    
    // GNSS functions
    bool enableGNSS();
    bool disableGNSS();
    bool updateGNSSData();
    GNSSData getGNSSData();
    bool hasValidFix();
    uint8_t getSatellites();
    
    // Cellular functions
    bool connectNetwork(const String& apn);
    bool connectNetwork(const String& apn, const String& user, const String& pass);
    bool disconnectNetwork();
    bool isNetworkConnected();
    int8_t getSignalStrength();
    String getOperatorName();
    String getIMEI();
    bool softReset();
    CellularData getCellularData();
    
    // Data transmission
    bool sendSMS(const String& number, const String& message);
    bool sendHTTP(const String& url, const String& data, String& response);
    bool sendJSON(const String& url, JsonDocument& json, String& response);
    
    // MQTT over SIM7080 AT (AT+SM*)
    bool mqttConfig(const String& host, uint16_t port, const String& user, const String& pass, const String& clientId);
    bool mqttConnect(uint32_t timeoutMs = 15000);
    bool mqttPublish(const String& topic, const String& payload, int qos = 0, bool retain = false);
    bool mqttSubscribe(const String& topic, int qos = 1);
    bool mqttUnsubscribe(const String& topic);
    bool mqttCheckIncoming(String& topic, String& payload, uint32_t timeoutMs = 100);
    bool mqttDisconnect();
    
    // Status
    CatMGNSSState getState() { return state; }
    bool isModuleInitialized() { return isInitialized; }
    
    // Enhanced DTO accessors
    CellStatus getCellStatus();
    GnssStatus getGnssStatus();

    // Diagnostics
    bool testAT();
    void printStatus();
    // Network time zone (from +CCLK? offset). Returns true and sets tzQuarters (15-min units) if available
    bool getNetworkTimeZoneQuarters(int& tzQuarters);
    // Get network time from cellular module (returns true if successful, sets timeInfo)
    bool getNetworkTime(struct tm& timeInfo);
    bool configureNetworkTime();
    bool syncNetworkTime(struct tm& utcOut, uint32_t timeoutMs = 65000);
    bool isNetworkTimeConfigured() const { return networkTimeConfigured_; }
    bool isNetworkTimeSynced() const { return networkTimeSynced_; }
    uint32_t getLastNetworkTimeSyncMs() const { return lastNetworkTimeSyncMs_; }
    const struct tm& getLastNetworkUtc() const { return lastNetworkUtc_; }
    // APN credentials management
    void setApnCredentials(const String& apn, const String& user, const String& pass);
    String getApn() const { return apn_; }
    String getApnUser() const { return apnUser_; }
    String getApnPass() const { return apnPass_; }
    
    // Task function for FreeRTOS
    static void taskFunction(void* pvParameters);

private:
    // Stored APN credentials
    String apn_;
    String apnUser_;
    String apnPass_;

    // Cached throughput sampling
    uint64_t lastTxBytesSample_ = 0;
    uint64_t lastRxBytesSample_ = 0;
    uint32_t lastStatsSampleMs_ = 0;

    // MQTT state
    bool mqttConfigured_ = false;

    // Network time state
    bool networkTimeConfigured_ = false;
    bool networkTimeSynced_ = false;
    uint32_t lastNetworkTimeSyncMs_ = 0;
    struct tm lastNetworkUtc_{};
};

#endif // CATM_GNSS_MODULE_H
