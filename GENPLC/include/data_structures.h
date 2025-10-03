/*
 * Data Structures Header
 * StampPLC CatM+GNSS FreeRTOS Modularization
 * 
 * Shared data structures and types for all modules
 * Optimized for M5Stack StampPLC (ESP32-S3FN8)
 * 
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2025 M5Stack Technology CO LTD
 */

#ifndef DATA_STRUCTURES_H
#define DATA_STRUCTURES_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>

// ============================================================================
// COMMON DATA TYPES
// ============================================================================
typedef uint32_t Timestamp;

// ============================================================================
// GNSS STATES
// ============================================================================
enum class GNSSState {
    INITIALIZING,
    SEARCHING,
    FIX_2D,
    FIX_3D,
    DGPS_FIX,
    ERROR,
    MAINTENANCE
};

// ============================================================================
// GNSS QUALITY LEVELS
// ============================================================================
enum class GNSSQuality {
    NO_FIX = 0,
    GPS_FIX = 1,
    DGPS_FIX = 2,
    PPS_FIX = 3,
    REAL_TIME_KINEMATIC = 4,
    FLOAT_RTK = 5,
    ESTIMATED = 6,
    MANUAL = 7,
    SIMULATION = 8
};

// ============================================================================
// GNSS DATA STRUCTURE
// ============================================================================
struct GNSSModuleData {
    // Position data
    double latitude;
    double longitude;
    double altitude;
    double speed;
    double course;
    
    // Time data
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    uint8_t day;
    uint8_t month;
    uint16_t year;
    
    // Quality data
    uint8_t satellites;
    GNSSQuality fixQuality;
    float hdop;  // Horizontal dilution of precision
    float vdop;  // Vertical dilution of precision
    float pdop;  // Position dilution of precision
    
    // Status
    GNSSState state;
    bool isValid;
    uint32_t lastUpdate;
    uint32_t fixAge;
    
    // Error tracking
    uint32_t errorCount;
    uint32_t timeoutCount;
};

// ============================================================================
// SENSOR DATA
// ============================================================================
struct SensorData {
    // Environmental sensors
    float temperature;
    float humidity;
    float pressure;
    float altitude;
    
    // Electrical sensors
    float voltage;
    float current;
    float power;
    float energy;
    
    // Status
    bool isValid;
    Timestamp timestamp;
    uint32_t errorCount;
    uint32_t timeoutCount;
};

// ============================================================================
// CELLULAR STATES
// ============================================================================
enum class CellularState {
    DISCONNECTED,
    CONNECTING,
    CONNECTED,
    REGISTERING,
    REGISTERED,
    DATA_TRANSMITTING,
    ERROR,
    MAINTENANCE
};

// ============================================================================
// CELLULAR COMMANDS
// ============================================================================
enum class CellularCommand {
    CONNECT,
    DISCONNECT,
    SEND_DATA,
    CHECK_SIGNAL,
    GET_IMEI,
    GET_OPERATOR,
    SEND_SMS,
    CHECK_BALANCE,
    RESET_MODULE
};

// ============================================================================
// CELLULAR RESPONSE TYPES
// ============================================================================
enum class CellularResponseType {
    SUCCESS,
    ERROR,
    TIMEOUT,
    NO_SIGNAL,
    NETWORK_ERROR,
    AUTH_ERROR,
    DATA_SENT,
    DATA_RECEIVED
};

// ============================================================================
// CELLULAR DATA STRUCTURES
// ============================================================================
struct CellularCommandData {
    CellularCommand command;
    String payload;
    uint32_t timestamp;
    uint8_t retryCount;
};

struct CellularResponseData {
    CellularResponseType type;
    String message;
    String data;
    uint32_t timestamp;
    bool success;
};

struct CellularSignalInfo {
    int8_t rssi;
    int8_t ber;
    String operator_name;
    String network_type;
    bool roaming;
};

struct CellularModuleData {
    // Connection data
    bool isConnected;
    bool isRegistered;
    CellularState state;
    
    // Signal information
    CellularSignalInfo signalInfo;
    
    // Network information
    String imei;
    String iccid;
    String operator_name;
    String network_type;
    
    // Transmission data
    uint32_t bytesSent;
    uint32_t bytesReceived;
    uint32_t lastTransmission;
    
    // Error tracking
    uint32_t errorCount;
    uint32_t timeoutCount;
    uint32_t connectionAttempts;
    uint32_t failedAttempts;
    
    // Status
    bool isValid;
    uint32_t lastUpdate;
};



// ============================================================================
// CELLULAR NETWORK TYPES
// ============================================================================
enum class CellularNetworkType {
    UNKNOWN = 0,
    GSM = 1,
    GPRS = 2,
    EDGE = 3,
    UMTS = 4,
    HSDPA = 5,
    HSUPA = 6,
    HSPA = 7,
    LTE = 8,
    CAT_M1 = 9,
    CAT_NB1 = 10,
    CAT_NB2 = 11
};

// ============================================================================
// CELLULAR STATUS
// ============================================================================
struct CellularStatus {
    // Connection status
    bool isConnected;
    bool isRegistered;
    bool isDataEnabled;
    
    // Network information
    char operator_name[32];
    char imei[16];
    char iccid[20];
    char imsi[16];
    
    // Signal and quality
    int8_t signalStrength;      // RSSI in dBm
    uint8_t signalQuality;      // 0-31 scale
    CellularNetworkType networkType;
    
    // Data usage
    uint32_t bytesSent;
    uint32_t bytesReceived;
    uint32_t totalBytes;
    
    // Timing
    uint32_t lastConnection;
    uint32_t connectionUptime;
    
    // Error tracking
    uint32_t connectionAttempts;
    uint32_t failedAttempts;
    uint32_t errors;
    uint32_t timeouts;
    uint32_t lastErrorCode;
};

// ============================================================================
// STAMPLC I/O STATUS
// ============================================================================
struct StampPLCStatus {
    // Digital inputs (8 channels)
    bool digitalInputs[8];
    
    // Analog inputs (4 channels)
    uint16_t analogInputs[4];
    
    // Relay outputs (2 channels)
    bool relayOutputs[2];
    
    // Status
    bool isInitialized;
    bool isCommunicating;
    Timestamp lastUpdate;
    uint32_t errorCount;
    uint32_t timeoutCount;
};

// ============================================================================
// DISPLAY STATUS
// ============================================================================
struct DisplayStatus {
    bool isInitialized;
    bool isUpdating;
    uint16_t width;
    uint16_t height;
    uint8_t rotation;
    uint32_t updateRate;
    uint32_t lastUpdate;
    uint32_t frameCount;
    uint32_t errorCount;
    uint32_t timeoutCount;
};

// ============================================================================
// CONFIGURATION STRUCTURES
// ============================================================================
struct GNSSConfig {
    uint8_t rxPin;
    uint8_t txPin;
    uint32_t baudRate;
    uint32_t updateInterval;
    bool enableLogging;
    bool enableDebug;
};

struct CellularConfig {
    // APN Configuration
    char apn[32];
    char username[32];
    char password[32];
    
    // Network Configuration
    char server[64];
    uint16_t port;
    bool useSSL;
    
    // Timing Configuration
    uint32_t timeout;
    uint32_t retryCount;
    uint32_t keepAliveInterval;
    
    // Feature Flags
    bool enableLogging;
    bool enableDebug;
    bool enableAutoReconnect;
    bool enablePowerSaving;
};

// ============================================================================
// CELLULAR TRANSMISSION DATA
// ============================================================================
struct CellularTransmissionData {
    // Message identification
    uint32_t messageId;
    uint32_t timestamp;
    
    // Content
    String payload;
    String contentType;  // "application/json", "text/plain", etc.
    
    // Transmission status
    bool isQueued;
    bool isTransmitting;
    bool isDelivered;
    bool isFailed;
    
    // Retry information
    uint8_t retryCount;
    uint8_t maxRetries;
    uint32_t lastAttempt;
    
    // Response data
    int16_t responseCode;
    String responseBody;
    uint32_t responseTime;
};

struct SensorConfig {
    uint32_t updateInterval;
    bool enableCalibration;
    bool enableLogging;
    bool enableDebug;
    float temperatureOffset;
    float humidityOffset;
    float pressureOffset;
};

struct DisplayConfig {
    uint16_t width;
    uint16_t height;
    uint8_t rotation;
    uint32_t updateRate;
    bool enableBacklight;
    bool enableLogging;
    bool enableDebug;
};

// ============================================================================
// CONSTANTS
// ============================================================================
#define MAX_ERROR_LOG_ENTRIES          50
#define MAX_TASK_COUNT                 16
#define MAX_MODULE_COUNT               10
#define MAX_QUEUE_SIZE                 20
#define MAX_SEMAPHORE_COUNT            10
#define MAX_EVENT_GROUP_COUNT          5

// ============================================================================
// UTILITY MACROS
// ============================================================================
#define ARRAY_SIZE(x)                  (sizeof(x) / sizeof((x)[0]))
#define MIN(a, b)                      ((a) < (b) ? (a) : (b))
#define MAX(a, b)                      ((a) > (b) ? (a) : (b))
#define CLAMP(x, min, max)             (MAX(min, MIN(max, x)))
#define ABS(x)                         ((x) < 0 ? -(x) : (x))

// ============================================================================
// FREE RTOS UTILITY MACROS
// ============================================================================
#define TASK_DELAY_MS(x)               pdMS_TO_TICKS(x)
#define TASK_DELAY_100MS               pdMS_TO_TICKS(100)
#define TASK_DELAY_500MS               pdMS_TO_TICKS(500)
#define TASK_DELAY_1S                  pdMS_TO_TICKS(1000)
#define TASK_DELAY_5S                  pdMS_TO_TICKS(5000)
#define TASK_DELAY_10S                 pdMS_TO_TICKS(10000)

// ============================================================================
// DEBUG MACROS
// ============================================================================
#ifdef DEBUG_ENABLED
    #define DEBUG_PRINT(x)             Serial.print(x)
    #define DEBUG_PRINTLN(x)           Serial.println(x)
    #define DEBUG_PRINTF(fmt, ...)     Serial.printf(fmt, ##__VA_ARGS__)
#else
    #define DEBUG_PRINT(x)
    #define DEBUG_PRINTLN(x)
    #define DEBUG_PRINTF(fmt, ...)
#endif

#endif // DATA_STRUCTURES_H
