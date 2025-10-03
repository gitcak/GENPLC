/*
 * System Configuration Header
 * StampPLC CatM+GNSS FreeRTOS Modularization
 * 
 * Contains all system-wide constants, pin definitions, and configuration parameters
 */

#ifndef SYSTEM_CONFIG_H
#define SYSTEM_CONFIG_H

#include <Arduino.h>

// ============================================================================
// FIRMWARE INFORMATION
// ============================================================================
#define STAMPLC_VERSION "2.0.0"
#define FIRMWARE_NAME "StampPLC CatM+GNSS FreeRTOS"
#define FIRMWARE_BUILD_DATE __DATE__
#define FIRMWARE_BUILD_TIME __TIME__

// ============================================================================
// HARDWARE PIN DEFINITIONS
// ============================================================================
// StampPLC Industrial I/O
#define PLC_I2C_SDA_PIN 21
#define PLC_I2C_SCL_PIN 22
#define PLC_SPI_SCK_PIN 23
#define PLC_SPI_MISO_PIN 19
#define PLC_SPI_MOSI_PIN 18
#define PLC_SPI_CS_PIN 5

// CatM Cellular Communication (Port C)
#define CATM_UART_RX_PIN 4
#define CATM_UART_TX_PIN 5
#define CATM_UART_BAUD 115200
#define CATM_UART_CONFIG SERIAL_8N1

// ============================================================================
// CATM CONFIGURATION
// ============================================================================
#define CATM_APN "soracom.io"
#define CATM_USER "sora"
#define CATM_PASS "sora"
#define CATM_SERVER "mqtt.example.com"
#define CATM_PORT 1883
#define CATM_AT_TIMEOUT_MS 5000
#define CATM_RETRY_COUNT 3

// ============================================================================
// GNSS CONFIGURATION
// ============================================================================
#define GNSS_UPDATE_RATE_MS 1000
#define GNSS_NMEA_BUFFER_SIZE 256
#define GNSS_FIX_TIMEOUT_MS 30000

// ============================================================================
// SYSTEM TIMING CONSTANTS
// ============================================================================
#define SYSTEM_TICK_RATE_MS 100
#define DISPLAY_UPDATE_RATE_MS 2000
#define SENSOR_READ_RATE_MS 1000
#define CELLULAR_STATUS_CHECK_MS 10000
#define DATA_TRANSMISSION_INTERVAL_MS 30000
#define BUTTON_DEBOUNCE_MS 50
#define LONG_PRESS_DURATION_MS 2000
#define SCREEN_TIMEOUT_MS 10000

// ============================================================================
// DISPLAY CONFIGURATION
// ============================================================================
#define DISPLAY_WIDTH 135
#define DISPLAY_HEIGHT 240
#define DISPLAY_FONT_SIZE 1
#define DISPLAY_TEXT_COLOR WHITE
#define DISPLAY_BACKGROUND_COLOR BLACK
#define DISPLAY_LINE_SPACING 15

// ============================================================================
// SENSOR LIMITS AND CALIBRATION
// ============================================================================
#define TEMP_MIN_C -40.0
#define TEMP_MAX_C 125.0
#define VOLTAGE_MIN_V 0.0
#define VOLTAGE_MAX_V 60.0
#define CURRENT_MIN_A -3.0
#define CURRENT_MAX_A 3.0

// ============================================================================
// PLC CONFIGURATION
// ============================================================================
#define PLC_INPUT_COUNT 8
#define PLC_RELAY_COUNT 4
#define PLC_ANALOG_CHANNELS 4

// ============================================================================
// ERROR HANDLING
// ============================================================================
#define MAX_ERROR_LOG_ENTRIES 50
#define ERROR_LOG_ENTRY_SIZE 128
#define WATCHDOG_TIMEOUT_MS 30000

// ============================================================================
// MEMORY CONFIGURATION
// ============================================================================
#define JSON_BUFFER_SIZE 1024
#define MQTT_BUFFER_SIZE 512
#define UART_BUFFER_SIZE 256

// ============================================================================
// DEBUG AND LOGGING
// ============================================================================
#define DEBUG_SERIAL_BAUD 115200
#define LOG_LEVEL_INFO 1
#define LOG_LEVEL_WARNING 2
#define LOG_LEVEL_ERROR 3
#define LOG_LEVEL_DEBUG 4

// ============================================================================
// FEATURE FLAGS
// ============================================================================
#define ENABLE_DEBUG_OUTPUT true
#define ENABLE_SERIAL_LOGGING true
#define ENABLE_DISPLAY_LOGGING true
#define ENABLE_CELLULAR_LOGGING true
#define ENABLE_GNSS_LOGGING true

// Enable PWRCAN integration (guarded; disabled until wiring confirmed)
// Integrated PWRCAN on StampPLC
#define ENABLE_PWRCAN false
#define ENABLE_WEB_SERVER false
// SD card usage can produce noisy logs when no card is present.
// Keep disabled by default for stability; enable when SD is required.
#ifndef ENABLE_SD
#define ENABLE_SD 1
#endif
// Optional: enable web font upload endpoint (/upload, /api/upload_font)
// Disabled by default to reduce heap pressure
#ifndef ENABLE_WEB_FONT_UPLOAD
#define ENABLE_WEB_FONT_UPLOAD 0
#endif

// PWRCAN default pins/bitrate (to be confirmed with PWRCAN module wiring)
#define PWRCAN_TX_PIN  42
#define PWRCAN_RX_PIN  43
#define PWRCAN_BITRATE_KBPS 250

// ============================================================================
// SYSTEM STATES
// ============================================================================
enum SystemState {
    SYSTEM_INITIALIZING,
    SYSTEM_READY,
    SYSTEM_ERROR,
    SYSTEM_MAINTENANCE,
    SYSTEM_SLEEP
};

enum DisplayMode {
    MAIN_SCREEN,
    PLC_STATUS_SCREEN,
    GNSS_DATA_SCREEN,
    SYSTEM_INFO_SCREEN,
    CELLULAR_STATUS_SCREEN,
    CONFIG_SCREEN
};

// ============================================================================
// ERROR CODES
// ============================================================================
enum ErrorCode {
    ERROR_NONE = 0,
    ERROR_INIT_FAILED,
    ERROR_HARDWARE_FAULT,
    ERROR_COMMUNICATION_FAILED,
    ERROR_MEMORY_FAULT,
    ERROR_SENSOR_FAULT,
    ERROR_CELLULAR_FAULT,
    ERROR_GNSS_FAULT,
    ERROR_SYSTEM_FAULT
};

// ============================================================================
// INLINE UTILITY FUNCTIONS
// ============================================================================
inline bool isInRange(float value, float min, float max) {
    return (value >= min && value <= max);
}

inline bool isValidTemperature(float temp) {
    return isInRange(temp, TEMP_MIN_C, TEMP_MAX_C);
}

inline bool isValidVoltage(float voltage) {
    return isInRange(voltage, VOLTAGE_MIN_V, VOLTAGE_MAX_V);
}

inline bool isValidCurrent(float current) {
    return isInRange(current, CURRENT_MIN_A, CURRENT_MAX_A);
}

#endif // SYSTEM_CONFIG_H
