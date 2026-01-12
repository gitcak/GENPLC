#include <Arduino.h>
#include <M5StamPLC.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include "config/task_config.h"
#include "modules/logging/log_buffer.h"
#include <ArduinoJson.h>
#include <WiFi.h>
#include <esp_log.h>
#include <simple_qr_code.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <SD.h>
#include <FS.h>
#include <sys/time.h>
#include <Preferences.h>
#include "../include/debug_system.h"
#include "../include/string_pool.h"
#include "../include/memory_monitor.h"
#include "../include/ui_utils.h"

// Include our modules
#include "hardware/basic_stamplc.h"
#include "modules/catm_gnss/catm_gnss_module.h"
#include "modules/catm_gnss/catm_gnss_task.h"
#include "modules/catm_gnss/gnss_status.h"
#include "config/system_config.h"
#include "modules/pwrcan/pwrcan_module.h"
#include "modules/storage/sd_card_module.h"
#include "modules/storage/storage_task.h"
#include "modules/mqtt/mqtt_task.h"
#include "ui/theme.h"
#include "ui/components.h"

// Legacy UI runs directly on M5GFX framebuffer (LVGL removed)
extern "C" void vTaskPWRCAN(void* pvParameters);

// ============================================================================
// FORWARD DECLARATIONS
// ============================================================================
void drawLandingPage();
void drawGNSSPage();
void drawCellularPage();
void drawSystemPage();
void drawSettingsPage();
void drawLogsPage();
void drawButtonIndicators();
void drawSignalBar(int16_t x, int16_t y, int16_t w, int16_t h, int8_t signalDbm);
void drawCompactSignalBar(void* sprite, int16_t x, int16_t y, int8_t signal, uint16_t color);
void drawWiFiBar(int16_t x, int16_t y, int16_t w, int16_t h);
void drawSDInfo(int x, int y);
void drawBootScreen(const char* status, int progress, bool passed = true);
bool setRTCFromCellular();
bool setRTCFromGPS(const GNSSData& gnssData);
const char* getNetworkType(int8_t signalStrength);
void initializeIcons();
void updateSystemLED();
bool setRTCFromBuildTimestamp();



// ============================================================================
// CONFIGURATION
// ============================================================================
// StamPLC RGB LED is controlled via M5StamPLC::SetStatusLight()
// No direct GPIO control needed
#define SERIAL_BAUD 115200
// Prefer per-task sizes from task_config.h where safe. Values are in words.
#ifndef TASK_STACK_SIZE
#define TASK_STACK_SIZE 4096
#endif

// ============================================================================
// GLOBAL VARIABLES
// ============================================================================
// Module instances
BasicStampPLC* stampPLC = nullptr;
CatMGNSSModule* catmGnssModule = nullptr;
PWRCANModule* pwrcanModule = nullptr;
SDCardModule* sdModule = nullptr;

// Task handles
TaskHandle_t buttonTaskHandle = nullptr;
TaskHandle_t displayTaskHandle = nullptr;
TaskHandle_t statusBarTaskHandle = nullptr;

// System event group for task coordination
EventGroupHandle_t xEventGroupSystemStatus = nullptr;
TaskHandle_t stampPLCTaskHandle = nullptr;
TaskHandle_t catmGnssTaskHandle = nullptr;
TaskHandle_t pwrcanTaskHandle = nullptr;
TaskHandle_t storageTaskHandle = nullptr;
TaskHandle_t webTaskHandle = nullptr;
TaskHandle_t mqttTaskHandle = nullptr;

// Shared connectivity flags consumed by CAT-M and MQTT tasks
volatile bool g_cellularUp = false;
volatile bool g_mqttUp = false;

// UI event queue
enum class UIEventType {
    GoLanding,
    GoGNSS,
    GoCELL,
    GoSYS,
    GoSETTINGS,
    ScrollUp,
    ScrollDown,
    PrevPage,
    NextPage,
    LauncherNext,
    LauncherPrev,
    LauncherOpen,
    Redraw
};

struct UIEvent { UIEventType type; };
static QueueHandle_t g_uiQueue = nullptr;
static uint32_t g_uiEventDrops = 0;
static String g_commFailureDescription = "Ensure CatM+GNSS unit is connected to Grove Port C (G4/G5).";

struct BootLogEntry {
    String text;
    bool passed;
};

static bool enqueueUIEvent(UIEventType type, TickType_t waitTicks = pdMS_TO_TICKS(25)) {
    if (!g_uiQueue) return false;
    UIEvent ev{type};
    if (xQueueSend(g_uiQueue, &ev, waitTicks) != pdTRUE) {
        if ((g_uiEventDrops++ % 10) == 0) {
            Serial.println("[UI] queue full - dropping events");
        }
        return false;
    }
    return true;
}

static void drawWrappedText(const String& text, int16_t x, int16_t y, int16_t maxWidth, int16_t lineHeight) {
    int start = 0;
    const int length = text.length();
    while (start < length) {
        int end = start;
        int lastSpace = -1;
        while (end < length) {
            if (text[end] == ' ') {
                lastSpace = end;
            }
            String slice = text.substring(start, end + 1);
            if (M5StamPLC.Display.textWidth(slice) > maxWidth) {
                if (lastSpace >= start) {
                    end = lastSpace;
                } else if (end == start) {
                    // single word longer than width; force break
                    while (end < length && text[end] != ' ') {
                        ++end;
                    }
                }
                break;
            }
            ++end;
        }
        if (end > length) end = length;
        String line = text.substring(start, end);
        line.trim();
        if (line.length()) {
            M5StamPLC.Display.setCursor(x, y);
            M5StamPLC.Display.print(line);
            y += lineHeight;
        }
        start = end;
        while (start < length && text[start] == ' ') {
            ++start;
        }
    }
}

// Display page management
enum class DisplayPage {
    LANDING_PAGE,
    GNSS_PAGE,
    CELLULAR_PAGE,
    SYSTEM_PAGE,
    SETTINGS_PAGE,
    LOGS_PAGE
};

volatile DisplayPage currentPage = DisplayPage::LANDING_PAGE;
volatile bool pageChanged = false;

// Status bar sprite & cache
static lgfx::LGFX_Sprite statusSprite(&M5StamPLC.Display);
static bool statusSpriteInit = false;
static bool lastGnssOk = false;
static bool lastCellOk = false;
static uint32_t lastStatusUpdate = 0;
static uint8_t lastCellularSignal = 0;
static uint8_t lastGnssSatellites = 0;
static char lastTimeStr[32] = "";

// Content sprite for card-based pages
static lgfx::LGFX_Sprite contentSprite(&M5StamPLC.Display);
static bool contentSpriteInit = false;

// Icon sprites for UI elements
static lgfx::LGFX_Sprite iconSatellite(&M5StamPLC.Display);
static lgfx::LGFX_Sprite iconGear(&M5StamPLC.Display);
static lgfx::LGFX_Sprite iconTower(&M5StamPLC.Display);
static lgfx::LGFX_Sprite iconWrench(&M5StamPLC.Display);
static lgfx::LGFX_Sprite iconGPS(&M5StamPLC.Display);
static lgfx::LGFX_Sprite iconCellular(&M5StamPLC.Display);
static lgfx::LGFX_Sprite iconLog(&M5StamPLC.Display);
// Larger icons for launcher card
static lgfx::LGFX_Sprite iconSatelliteBig(&M5StamPLC.Display);
static lgfx::LGFX_Sprite iconGearBig(&M5StamPLC.Display);
static lgfx::LGFX_Sprite iconTowerBig(&M5StamPLC.Display);
static lgfx::LGFX_Sprite iconWrenchBig(&M5StamPLC.Display);
static lgfx::LGFX_Sprite iconGPSBig(&M5StamPLC.Display);
static lgfx::LGFX_Sprite iconCellularBig(&M5StamPLC.Display);
static lgfx::LGFX_Sprite iconLogBig(&M5StamPLC.Display);
static bool iconsInitialized = false;

// Sprite cleanup functions
// cleanupAllSprites declared in ui_utils.h
static void cleanupContentSprite();
static void cleanupStatusSprite();
static void cleanupIconSprites();
// Simple page transition tracking: -1 = prev (slide right), 1 = next (slide left), 0 = none
static int8_t g_lastNavDir = 0;
// NTP configuration tracking
static bool g_ntpConfigured = false;
static uint32_t g_lastTZUpdateMs = 0;
static bool g_cntpSyncedThisSession = false;
static bool g_lastCellularNtpUsedCntp = false;

// ===================== Modal / Hot-plug State =====================
enum class ModalType { NONE, NO_COMM_UNIT };
static volatile bool g_modalActive = false;
static volatile ModalType g_modalType = ModalType::NONE;
static volatile bool g_forceCatMRetry = false;
static uint32_t g_lastCatMProbeMs = 0; // background re-probe ticker
static volatile bool g_retryToastActive = false;
static uint32_t g_retryToastUntilMs = 0;

static void drawModalOverlay();
static void tryInitCatMIfAbsent(bool forced);

// Fetch NTP time via cellular - tries CNTP first, falls back to HTTP
bool fetchNtpTimeViaCellular(struct tm& timeInfo) {
    if (!catmGnssModule || !catmGnssModule->isModuleInitialized()) return false;

    CellularData cd = catmGnssModule->getCellularData();
    if (!cd.isConnected) return false;

    g_lastCellularNtpUsedCntp = false;

    // Try CNTP (AT+CNTPSTART) first - fast and efficient
    Serial.println("NTP: Attempting CNTP sync via Soracom NTP...");
    if (catmGnssModule->syncNetworkTime(timeInfo, 65000)) {
        int year = timeInfo.tm_year + 1900;
        int month = timeInfo.tm_mon + 1;
        Serial.printf("NTP: CNTP sync success - %04d-%02d-%02d %02d:%02d:%02d UTC\n",
                      year, month, timeInfo.tm_mday, timeInfo.tm_hour, 
                      timeInfo.tm_min, timeInfo.tm_sec);
        g_cntpSyncedThisSession = true;
        g_lastCellularNtpUsedCntp = true;
        return true;
    }
    
    Serial.printf("NTP: CNTP sync failed (%s), falling back to HTTP...\n", 
                  catmGnssModule->getLastError().c_str());

    // Fallback: HTTP-based time fetch via worldtimeapi.org
    String url = "http://worldtimeapi.org/api/timezone/Etc/UTC";
    String response;

    if (!catmGnssModule->sendHTTP(url, "", response)) {
        Serial.println("NTP: Failed to fetch time from cellular HTTP fallback");
        return false;
    }

    // Parse JSON response
    // Expected format: {"datetime": "2024-01-15T19:30:25.123456+00:00", ...}
    int datetimeStart = response.indexOf("\"datetime\":\"");
    if (datetimeStart < 0) {
        Serial.println("NTP: Invalid JSON response from time API");
        return false;
    }

    datetimeStart += 12; // Skip "datetime":"
    int datetimeEnd = response.indexOf("\"", datetimeStart);
    if (datetimeEnd < 0) {
        Serial.println("NTP: Malformed datetime in JSON response");
        return false;
    }

    String datetime = response.substring(datetimeStart, datetimeEnd);

    // Parse ISO 8601 datetime: 2024-01-15T19:30:25.123456+00:00
    // Extract: YYYY-MM-DDTHH:MM:SS
    if (datetime.length() < 19) {
        Serial.println("NTP: Datetime string too short");
        return false;
    }

    // Parse year, month, day
    int year = datetime.substring(0, 4).toInt();
    int month = datetime.substring(5, 7).toInt();
    int day = datetime.substring(8, 10).toInt();
    int hour = datetime.substring(11, 13).toInt();
    int minute = datetime.substring(14, 16).toInt();
    int second = datetime.substring(17, 19).toInt();

    // Validate parsed values
    if (year < 2020 || year > 2030 || month < 1 || month > 12 ||
        day < 1 || day > 31 || hour < 0 || hour > 23 ||
        minute < 0 || minute > 59 || second < 0 || second > 59) {
        Serial.println("NTP: Invalid time values parsed from API");
        return false;
    }

    // Fill tm structure (now in UTC)
    timeInfo.tm_year = year - 1900;
    timeInfo.tm_mon = month - 1;
    timeInfo.tm_mday = day;
    timeInfo.tm_hour = hour;
    timeInfo.tm_min = minute;
    timeInfo.tm_sec = second;
    timeInfo.tm_isdst = 0; // UTC doesn't observe DST

    Serial.printf("NTP: Fetched UTC time via HTTP fallback: %04d-%02d-%02d %02d:%02d:%02d\n",
                  year, month, day, hour, minute, second);
    return true;
}

static void ensureNtpConfigured() {
    if (g_ntpConfigured) return;

    // Configure NTP when either WiFi or cellular is connected
    bool hasConnectivity = false;

    // Check WiFi connectivity (use standard NTP)
    if ((WiFi.getMode() & WIFI_MODE_STA) && WiFi.status() == WL_CONNECTED) {
        hasConnectivity = true;
        Serial.println("NTP: Configuring via WiFi connection");
        // Timezone: Eastern with DST auto rules (EST/EDT). Adjust as needed.
        // Use multiple NTP servers for reliability
        configTzTime("EST5EDT,M3.2.0/2,M11.1.0/2",
                    "pool.ntp.org",
                    "time.nist.gov",
                    "time.google.com");
        g_ntpConfigured = true;
        Serial.println("NTP: Configuration completed via WiFi");
    }
    // Check cellular connectivity (NTP will be handled via HTTP)
    else if (catmGnssModule && catmGnssModule->isModuleInitialized()) {
        CellularData cd = catmGnssModule->getCellularData();
        if (cd.isConnected) {
            hasConnectivity = true;
            Serial.println("NTP: Cellular connection available (NTP via HTTP)");
            // For cellular, we don't configure standard NTP since it won't work
            // Instead, we'll use fetchNtpTimeViaCellular() in syncRTCFromAvailableSources()
            g_ntpConfigured = true; // Mark as configured so we don't keep checking
            Serial.println("NTP: HTTP-based NTP will be used via cellular");
        }
    }
}

static void maybeUpdateTimeZoneFromCellular() {
    // Update timezone from cellular offset if available, every 10 minutes
    // Only trigger after a successful CNTP sync to ensure fresh modem time
    if (!g_cntpSyncedThisSession) return;
    if (millis() - g_lastTZUpdateMs < 10UL * 60UL * 1000UL) return;
    if (!catmGnssModule || !catmGnssModule->isModuleInitialized()) return;
    if (!catmGnssModule->isNetworkTimeSynced()) return;
    CellularData cd = catmGnssModule->getCellularData();
    if (!cd.isConnected) return;
    
    int q = 0;
    if (catmGnssModule->getNetworkTimeZoneQuarters(q)) {
        int minutes = q * 15; // minutes offset from UTC
        int hours = minutes / 60; 
        int mins = abs(minutes % 60);
        
        // POSIX TZ sign is reversed relative to human-readable UTC+X
        char tzbuf[32];
        if (minutes >= 0)
            snprintf(tzbuf, sizeof(tzbuf), "UTC-%d:%02d", hours, mins);
        else
            snprintf(tzbuf, sizeof(tzbuf), "UTC+%d:%02d", -hours, mins);
        
        setenv("TZ", tzbuf, 1);
        tzset();
        g_lastTZUpdateMs = millis();
        
        Serial.printf("Timezone updated from cellular network: %s\n", tzbuf);
    }
}

// Convert a UTC struct tm to local time strings using current TZ rules, without changing global time permanently
static void formatLocalFromUTC(const struct tm& utcIn, char* timeStr, char* dateStr) {
    char prevTZ[64] = {0};
    const char* cur = getenv("TZ");
    if (cur) { strncpy(prevTZ, cur, sizeof(prevTZ) - 1); prevTZ[sizeof(prevTZ) - 1] = '\0'; }
    setenv("TZ", "UTC0", 1); tzset();
    struct tm tmp = utcIn; // mktime() normalizes
    time_t epoch = mktime(&tmp);
    if (prevTZ[0]) setenv("TZ", prevTZ, 1); else unsetenv("TZ");
    tzset();
    struct tm lt;
    localtime_r(&epoch, &lt);
    char tbuf[16]; snprintf(tbuf, sizeof(tbuf), "%02d:%02d", lt.tm_hour, lt.tm_min);
    char dbuf[16]; snprintf(dbuf, sizeof(dbuf), "%02d/%02d/%04d", lt.tm_mon + 1, lt.tm_mday, lt.tm_year + 1900);
    strncpy(timeStr, tbuf, 15); timeStr[15] = '\0';
    strncpy(dateStr, dbuf, 15); dateStr[15] = '\0';
}


// =========================================================================
// UI LAYOUT CONSTANTS
// =========================================================================
// Based on 240x135 screen
static const int16_t STATUS_BAR_H   = 14;   // top status bar height
static const int16_t BUTTON_BAR_Y   = 123;  // y baseline for button labels
static const int16_t CONTENT_TOP    = STATUS_BAR_H;
static const int16_t CONTENT_BOTTOM = BUTTON_BAR_Y;
static const int16_t LINE_H1        = 10;   // line height for textSize=1
static const int16_t LINE_H2        = 16;   // line height for textSize=2
static const int16_t COL1_X         = 5;
static const int16_t COL2_X         = 120;

static inline int16_t y1(int row) { return CONTENT_TOP + row * LINE_H1; }
static inline int16_t y2(int row) { return CONTENT_TOP + row * LINE_H2; }

// =========================================================================
// UI SCROLL STATE
// =========================================================================
static const int16_t VIEW_HEIGHT = (CONTENT_BOTTOM - CONTENT_TOP);
static const int16_t SCROLL_STEP = LINE_H1; // pixels per scroll step
volatile int16_t scrollGNSS = 0;
volatile int16_t scrollCELL = 0;
volatile int16_t scrollSYS  = 0;
volatile int16_t scrollSETTINGS = 0;
volatile int16_t scrollLOGS = 0;

// Settings state management
enum class SettingsState {
    NORMAL              // Normal settings view
};

volatile SettingsState settingsState = SettingsState::NORMAL;

// Settings data structures
struct SettingsData {
    // Settings simplified
    uint8_t dummy; // Keep struct non-empty for compatibility
};

static SettingsData currentSettings = {
    0       // dummy: Keep struct initialized
};

// System LED states
enum class SystemLEDState {
    BOOTING,    // Orange - system starting up
    RUNNING,    // Green - normal operation
    FAULT       // Blinking red - error condition
};
static SystemLEDState currentLEDState = SystemLEDState::BOOTING;
static uint32_t lastLEDBlink = 0;
static bool ledBlinkState = false;

// Global variables for actual functionality
static uint32_t lastDisplayActivity = 0;
static bool displaySleepEnabled = true; // Enable display sleep by default
static bool displayAsleep = false;
static const uint32_t DISPLAY_SLEEP_TIMEOUT_MS = 120000; // 2 minutes
static uint8_t displayBrightness = 100; // Default brightness (0-255, adjust as needed)

static inline int16_t clampScroll(int16_t value, int16_t contentHeight) {
    if (contentHeight <= VIEW_HEIGHT) return 0;
    int16_t maxScroll = contentHeight - VIEW_HEIGHT;
    if (value < 0) return 0;
    if (value > maxScroll) return maxScroll;
    return value;
}

// Function declarations
void formatSDCard();
bool deleteDirectory(const char* path, int depth);

// ============================================================================
// TASK DECLARATIONS
// ============================================================================
void vTaskButton(void* pvParameters) {
    DEBUG_LOG_TASK_START("Button");

    const TickType_t kButtonPeriod = pdMS_TO_TICKS(20);
    TickType_t nextWake = xTaskGetTickCount();

    // Long press threshold
    const uint32_t LONG_MS = 500;
    bool aHandled = false, bHandled = false, cHandled = false;

    for (;;) {
        if (stampPLC) {
            stampPLC->update();
        }

        // Update LVGL button states (critical for LVGL keypad input)
        #if LVGL_UI_TEST_MODE
        ui_update_button_states();
        vTaskDelayUntil(&nextWake, kButtonPeriod);
        continue; // Skip old button handling when LVGL is enabled
        #endif

        if (!stampPLC) {
            vTaskDelayUntil(&nextWake, kButtonPeriod);
            continue;
        }

        auto& A = stampPLC->getStamPLC()->BtnA;
        auto& B = stampPLC->getStamPLC()->BtnB;
        auto& C = stampPLC->getStamPLC()->BtnC;

        // Determine if we are on landing page (card launcher) or in a detail page
        bool onLanding = (currentPage == DisplayPage::LANDING_PAGE);

        // Wake display on any button press
        if (A.wasPressed() || B.wasPressed() || C.wasPressed()) {
            lastDisplayActivity = millis();
        }

        // If a modal dialog is active, intercept A/C buttons for modal actions
        if (g_modalActive) {
            if (A.wasPressed()) {
                // Dismiss modal
                g_modalActive = false;
                g_modalType = ModalType::NONE;
                enqueueUIEvent(UIEventType::Redraw);
            }
            if (C.wasPressed()) {
                // Request immediate retry; leave modal visible
                g_forceCatMRetry = true;
                g_retryToastActive = true;
                g_retryToastUntilMs = millis() + 1500; // show for 1.5s
            }
            vTaskDelayUntil(&nextWake, kButtonPeriod);
            continue; // Don't process normal navigation while modal active
        }

        if (onLanding) {
            // Long-press navigation from landing page
            if (A.pressedFor(LONG_MS) && !aHandled) {
                enqueueUIEvent(UIEventType::GoCELL);
                aHandled = true;
                DEBUG_LOG_BUTTON_PRESS("A", "GoCELL (long press)");
            } else if (!A.isPressed() && currentPage != DisplayPage::SETTINGS_PAGE) { aHandled = false; }
            if (B.pressedFor(LONG_MS) && !bHandled) {
                enqueueUIEvent(UIEventType::GoGNSS);
                bHandled = true;
                DEBUG_LOG_BUTTON_PRESS("B", "GoGNSS (long press)");
            } else if (!B.isPressed() && currentPage != DisplayPage::SETTINGS_PAGE) { bHandled = false; }
            if (C.pressedFor(LONG_MS) && !cHandled) {
                enqueueUIEvent(UIEventType::GoSYS);
                cHandled = true;
                DEBUG_LOG_BUTTON_PRESS("C", "GoSYS (long press)");
            } else if (!C.isPressed() && currentPage != DisplayPage::SETTINGS_PAGE) { cHandled = false; }
        } else {
            // In-page: A=HOME (back). B/C short=Scroll Up/Down. B/C long=Prev/Next page.
            if (A.wasPressed()) {
                enqueueUIEvent(UIEventType::GoLanding);
            }

            // B button: long press = prev page, short press = scroll up
            if (B.pressedFor(LONG_MS) && !bHandled) {
                enqueueUIEvent(UIEventType::PrevPage);
                bHandled = true;
            } else if (!B.isPressed()) {
                bHandled = false;
            } else if (B.wasPressed()) {
                enqueueUIEvent(UIEventType::ScrollUp);
            }

            // C button: long press = next page, short press = scroll down
            if (C.pressedFor(LONG_MS) && !cHandled) {
                enqueueUIEvent(UIEventType::NextPage);
                cHandled = true;
            } else if (!C.isPressed()) {
                cHandled = false;
            } else if (C.wasPressed()) {
                enqueueUIEvent(UIEventType::ScrollDown);
            }

            // Settings page: no special long-press actions beyond above
        }

        // Debug every 5s
        static uint32_t lastDebugTime = 0;
        if (millis() - lastDebugTime > 5000) {
            Serial.printf("Btn A:%d B:%d C:%d | page:%d scroll g:%d c:%d s:%d | sleep:%s | timeout:%ds\n",
                          A.isPressed(), B.isPressed(), C.isPressed(),
                          (int)currentPage, scrollGNSS, scrollCELL, scrollSYS,
                          displayAsleep ? "YES" : "NO", DISPLAY_SLEEP_TIMEOUT_MS / 1000);
            if (currentPage == DisplayPage::SETTINGS_PAGE) {
                Serial.printf("Settings: %s\n", settingsState == SettingsState::NORMAL ? "Normal" : "Format confirm");
            }
            lastDebugTime = millis();
        }

        vTaskDelayUntil(&nextWake, kButtonPeriod);
    }
}

void vTaskStampPLC(void* pvParameters) {
    if (!stampPLC) {
        Serial.println("StampPLC task error: stampPLC is null");
        vTaskDelete(NULL);
        return;
    }

    const TickType_t kPlcPeriod = pdMS_TO_TICKS(100);
    TickType_t nextWake = xTaskGetTickCount();

    for (;;) {
        // Print status every 5 seconds
        static uint32_t lastStatusTime = 0;
        if (millis() - lastStatusTime > 5000) {
            stampPLC->printStatus();
            lastStatusTime = millis();
        }

        vTaskDelayUntil(&nextWake, kPlcPeriod);
    }
}

void vTaskDisplay(void* pvParameters) {
    // Wait for display to initialize
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    Serial.println("Display task started");
    
    // Initialize display activity timer
    lastDisplayActivity = millis();
    
    for (;;) {
        static uint32_t lastFullDraw = 0;

        // Check for display sleep/wake logic
        // Disabled when LVGL UI is active to avoid unintended blanking
        #if !LVGL_UI_TEST_MODE
        if (displaySleepEnabled) {
            uint32_t now = millis();
            bool shouldSleep = (now - lastDisplayActivity > DISPLAY_SLEEP_TIMEOUT_MS);
            
            if (shouldSleep && !displayAsleep) {
                // Put display to sleep and turn off backlight
                displayAsleep = true;
                
                // Put display to sleep and turn off backlight
                M5StamPLC.Display.sleep();
                M5StamPLC.Display.setBrightness(0);
                
                Serial.println("Display going to sleep (backlight off)");
            } else if (!shouldSleep && displayAsleep) {
                // Wake display up and restore backlight
                displayAsleep = false;
                
                M5StamPLC.Display.wakeup();
                M5StamPLC.Display.setBrightness(displayBrightness);
                
                Serial.println("Display waking up (backlight restored)");
                
                pageChanged = true; // Force full redraw
            }
        }
        #endif

        // Skip old UI logic when LVGL is enabled
        #if !LVGL_UI_TEST_MODE
        // Drain UI events and update state
        if (g_uiQueue) {
            UIEvent ev;
            while (xQueueReceive(g_uiQueue, &ev, 0) == pdTRUE) {
                // Ordered page list for Prev/Next navigation (exclude/ include landing as first)
                static const DisplayPage kPageOrder[] = {
                    DisplayPage::LANDING_PAGE,
                    DisplayPage::GNSS_PAGE,
                    DisplayPage::CELLULAR_PAGE,
                    DisplayPage::SYSTEM_PAGE,
                    DisplayPage::LOGS_PAGE,
                    DisplayPage::SETTINGS_PAGE
                };
                auto pageIndex = [&](DisplayPage p){
                    for (size_t i=0;i<sizeof(kPageOrder)/sizeof(kPageOrder[0]);++i) if (kPageOrder[i]==p) return (int)i;
                    return 0;
                };
                switch (ev.type) {
                    case UIEventType::GoLanding:
                        currentPage = DisplayPage::LANDING_PAGE;
                        pageChanged = true;
                        g_lastNavDir = 0;
                        break;
                    case UIEventType::GoGNSS:
                        currentPage = DisplayPage::GNSS_PAGE;
                        scrollGNSS = 0;
                        pageChanged = true;
                        g_lastNavDir = 0;
                        break;
                    case UIEventType::GoCELL:
                        currentPage = DisplayPage::CELLULAR_PAGE;
                        scrollCELL = 0;
                        pageChanged = true;
                        g_lastNavDir = 0;
                        break;
                    case UIEventType::GoSYS:
                        currentPage = DisplayPage::SYSTEM_PAGE;
                        scrollSYS = 0;
                        pageChanged = true;
                        g_lastNavDir = 0;
                        break;
                    case UIEventType::GoSETTINGS:
                        currentPage = DisplayPage::SETTINGS_PAGE;
                        scrollSETTINGS = 0;
                        pageChanged = true;
                        g_lastNavDir = 0;
                        break;
                    case UIEventType::ScrollUp:
                        switch (currentPage) {
                            case DisplayPage::GNSS_PAGE:   scrollGNSS = clampScroll(scrollGNSS - SCROLL_STEP, 122); break;
                            case DisplayPage::CELLULAR_PAGE: {
                                // Calculate actual content height for cellular page
                                int16_t contentH = LINE_H2 + LINE_H1 * 11; // Title + 11 lines of content
                                scrollCELL = clampScroll(scrollCELL - SCROLL_STEP, contentH);
                                break;
                            }
                            case DisplayPage::SYSTEM_PAGE: {
                                // Calculate actual content height for system page
                                int16_t contentH = LINE_H2 + LINE_H1 * 16; // Title + 16 lines of content
                                scrollSYS = clampScroll(scrollSYS - SCROLL_STEP, contentH);
                                break;
                            }
                            case DisplayPage::LOGS_PAGE: {
                                int16_t contentH = (int16_t)log_count() * LINE_H1 + 8;
                                if (contentH < (VIEW_HEIGHT + 1)) contentH = VIEW_HEIGHT + 1;
                                scrollLOGS = clampScroll(scrollLOGS - SCROLL_STEP, contentH);
                                break;
                            }
                            default: break;
                        }
                        pageChanged = true;
                        break;
                    case UIEventType::ScrollDown:
                        switch (currentPage) {
                            case DisplayPage::GNSS_PAGE:   scrollGNSS = clampScroll(scrollGNSS + SCROLL_STEP, 122); break;
                            case DisplayPage::CELLULAR_PAGE: {
                                // Calculate actual content height for cellular page
                                int16_t contentH = LINE_H2 + LINE_H1 * 11; // Title + 11 lines of content
                                scrollCELL = clampScroll(scrollCELL + SCROLL_STEP, contentH);
                                break;
                            }
                            case DisplayPage::SYSTEM_PAGE: {
                                // Calculate actual content height for system page
                                int16_t contentH = LINE_H2 + LINE_H1 * 16; // Title + 16 lines of content
                                scrollSYS = clampScroll(scrollSYS + SCROLL_STEP, contentH);
                                break;
                            }
                            case DisplayPage::LOGS_PAGE: {
                                int16_t contentH = (int16_t)log_count() * LINE_H1 + 8;
                                if (contentH < (VIEW_HEIGHT + 1)) contentH = VIEW_HEIGHT + 1;
                                scrollLOGS = clampScroll(scrollLOGS + SCROLL_STEP, contentH);
                                break;
                            }
                            default: break;
                        }
                        pageChanged = true;
                        break;
                    case UIEventType::LauncherNext:
                        pageChanged = true;
                        g_lastNavDir = 1;
                        break;
                    case UIEventType::LauncherPrev:
                        pageChanged = true;
                        g_lastNavDir = -1;
                        break;
                    case UIEventType::LauncherOpen:
                        pageChanged = true;
                        g_lastNavDir = 0;
                        break;
                    case UIEventType::PrevPage: {
                        int idx = pageIndex(currentPage);
                        int total = (int)(sizeof(kPageOrder)/sizeof(kPageOrder[0]));
                        idx = (idx - 1 + total) % total;
                        currentPage = kPageOrder[idx];
                        pageChanged = true;
                        g_lastNavDir = -1;
                        break;
                    }
                    case UIEventType::NextPage: {
                        int idx = pageIndex(currentPage);
                        int total = (int)(sizeof(kPageOrder)/sizeof(kPageOrder[0]));
                        idx = (idx + 1) % total;
                        currentPage = kPageOrder[idx];
                        pageChanged = true;
                        g_lastNavDir = 1;
                        break;
                    }
                    case UIEventType::Redraw:
                        pageChanged = true;
                        break;
                }
            }
        }
        
        #endif // !LVGL_UI_TEST_MODE

        #if LVGL_UI_TEST_MODE
        // LVGL UI is handled by its own task, just sleep
        vTaskDelay(pdMS_TO_TICKS(100));
        continue;
        #endif

        bool doFullDraw = pageChanged && !displayAsleep; // redraw only on changes and when awake

        if (doFullDraw) {
            // Full redraw on page change or periodic refresh
            M5StamPLC.Display.fillScreen(BLACK);

            switch (currentPage) {
                case DisplayPage::LANDING_PAGE:
                    Serial.println("Drawing LANDING_PAGE");
                    drawLandingPage();
                    break;
                case DisplayPage::GNSS_PAGE:
                    Serial.println("Drawing GNSS_PAGE");
                    drawGNSSPage();
                    break;
                case DisplayPage::CELLULAR_PAGE:
                    Serial.println("Drawing CELLULAR_PAGE");
                    drawCellularPage();
                    break;
                case DisplayPage::SYSTEM_PAGE:
                    Serial.println("Drawing SYSTEM_PAGE");
                    drawSystemPage();
                    break;
                case DisplayPage::SETTINGS_PAGE:
                    Serial.println("Drawing SETTINGS_PAGE");
                    drawSettingsPage();
                    break;
                case DisplayPage::LOGS_PAGE:
                    Serial.println("Drawing LOGS_PAGE");
                    drawLogsPage();
                    break;
                default:
                    Serial.println("Drawing DEFAULT_PAGE");
                    drawLandingPage();
                    break;
            }

            // Draw overlays after content render
            drawButtonIndicators();
            lastFullDraw = millis();

            pageChanged = false;
            Serial.println("Display updated");
        }

        // Always ensure modal overlay is drawn last when active (prevents background bleed)
        if (g_modalActive) {
            drawModalOverlay();
        }

        vTaskDelay(pdMS_TO_TICKS(100)); // Reduced delay for better responsiveness
    }
}

// ============================================================================
// RTC SYNCHRONIZATION
// ============================================================================

// Synchronize RTC from all available time sources
void syncRTCFromAvailableSources() {
    static uint32_t lastNtpSync = 0;
    static uint32_t lastCellularSync = 0;
    static uint32_t lastGpsSync = 0;
    uint32_t now = millis();

    // Ensure NTP is configured if we have connectivity
    ensureNtpConfigured();

    // 1. Try NTP sync (every 1 hour)
    if (now - lastNtpSync > 3600000UL) {
        // First try standard NTP (WiFi)
        struct tm lt;
        if (getLocalTime(&lt, 50)) { // NTP available via WiFi
            time_t now_time = time(nullptr);
            struct tm utc;
            gmtime_r(&now_time, &utc);
            if (M5StamPLC.RX8130.begin()) {
                M5StamPLC.RX8130.setTime(&utc);
                Serial.println("RTC synchronized from NTP (WiFi)");
                lastNtpSync = now;
                return; // Success, no need to try others
            }
        }
        // If WiFi NTP failed, try HTTP-based NTP via cellular
        else if (catmGnssModule && catmGnssModule->isModuleInitialized()) {
            CellularData cd = catmGnssModule->getCellularData();
            if (cd.isConnected) {
                struct tm ntpTime;
                if (fetchNtpTimeViaCellular(ntpTime)) {
                    if (M5StamPLC.RX8130.begin()) {
                        M5StamPLC.RX8130.setTime(&ntpTime);
                        if (g_lastCellularNtpUsedCntp) {
                            Serial.println("RTC synchronized from CNTP (Cellular)");
                        } else {
                            Serial.println("RTC synchronized from HTTP (Cellular)");
                        }
                        lastNtpSync = now;
                        return; // Success, no need to try others
                    }
                }
            }
        }
    }

    // 2. Try cellular network time (every 30 minutes)
    if (now - lastCellularSync > 1800000UL) {
        if (catmGnssModule && catmGnssModule->isModuleInitialized()) {
            CellularData cd = catmGnssModule->getCellularData();
            if (cd.isConnected && setRTCFromCellular()) {
                Serial.println("RTC synchronized from cellular network time");
                lastCellularSync = now;
                return; // Success, no need to try GPS
            }
        }
    }

    // 3. Try GPS time (every 15 minutes)
    if (now - lastGpsSync > 900000UL) {
        if (catmGnssModule && catmGnssModule->isModuleInitialized()) {
            GNSSData gnssData = catmGnssModule->getGNSSData();
            if (gnssData.isValid && gnssData.year > 2020 && setRTCFromGPS(gnssData)) {
                Serial.println("RTC synchronized from GPS");
                lastGpsSync = now;
            }
        }
    }
}

// STATUS BAR TASK
// ============================================================================
void vTaskStatusBar(void* pvParameters) {
    Serial.println("Status bar task started");
    
    #if LVGL_UI_TEST_MODE
    // LVGL handles status bar, but we still need periodic RTC sync
    for (;;) {
        EventBits_t bits = xEventGroupWaitBits(
            xEventGroupSystemStatus,
            EVENT_BIT_STATUS_CHANGE | EVENT_BIT_SYSTEM_ERROR | EVENT_BIT_HEAP_LOW,
            pdTRUE,
            pdFALSE,
            pdMS_TO_TICKS(5000)  // 5s periodic check
        );

        // Handle status change events
        if (bits & EVENT_BIT_STATUS_CHANGE) {
            // Status changed - could trigger UI update
        }

        if (bits & EVENT_BIT_SYSTEM_ERROR) {
            // System error - could show error indicator
        }

        if (bits & EVENT_BIT_HEAP_LOW) {
            // Low heap - could show memory warning
        }

        // Periodic RTC synchronization (every 5 seconds)
        static uint32_t lastRtcSyncCheck = 0;
        if (millis() - lastRtcSyncCheck >= 5000) {
            syncRTCFromAvailableSources();
            lastRtcSyncCheck = millis();
        }
    }
    #else
    for (;;) {
        // Wait for status change events or periodic timeout
        EventBits_t bits = xEventGroupWaitBits(
            xEventGroupSystemStatus,
            EVENT_BIT_STATUS_CHANGE | EVENT_BIT_SYSTEM_ERROR | EVENT_BIT_HEAP_LOW,
            pdTRUE,
            pdFALSE,
            pdMS_TO_TICKS(5000)  // 5s periodic check
        );
        
        // Immediate stack watermark check for early warning
        UBaseType_t hwmNow = uxTaskGetStackHighWaterMark(NULL);
        if (hwmNow < 512) { // < 2KB remaining (words)
            Serial.printf("WARNING: StatusBar stack low: %u words\n", (unsigned)hwmNow);
            // Signal heap low event
            xEventGroupSetBits(xEventGroupSystemStatus, EVENT_BIT_HEAP_LOW);
        }
        
        // Update memory monitoring
        g_memoryMonitor.update();

        // Check memory status and trigger alerts
        MemoryStatus memStatus = g_memoryMonitor.getStatus();
        if (memStatus == MemoryStatus::LOW_MEMORY || memStatus == MemoryStatus::CRITICAL) {
            xEventGroupSetBits(xEventGroupSystemStatus, EVENT_BIT_HEAP_LOW);
        }

        // Periodic hardware sensor monitoring and RTC sync
        static uint32_t lastSensorCheck = 0;
        if (millis() - lastSensorCheck > 10000) { // Check every 10 seconds
            // Monitor thermal sensor with I2C error handling
            if (M5StamPLC.LM75B.begin()) {
                float temp = M5StamPLC.getTemp();
                if (temp < -100.0f || temp > 200.0f) {
                    Serial.printf("WARNING: Temperature sensor reading out of range: %.1f°C\n", temp);
                }
            } else {
                Serial.println("WARNING: Thermal sensor (LM75B) I2C communication failed");
            }

            // Monitor power readings with I2C error handling
            if (M5StamPLC.INA226.begin()) {
                float voltage = M5StamPLC.INA226.getBusVoltage();
                float current = M5StamPLC.INA226.getShuntCurrent();
                if (voltage < 0.0f || voltage > 30.0f) {
                    Serial.printf("WARNING: Voltage reading out of range: %.2fV\n", voltage);
                }
                if (current < -5.0f || current > 5.0f) {
                    Serial.printf("WARNING: Current reading out of range: %.3fA\n", current);
                }
            } else {
                Serial.println("WARNING: Power monitor (INA226) I2C communication failed");
            }

            // Periodic RTC synchronization from available sources
            syncRTCFromAvailableSources();

            lastSensorCheck = millis();
        }
        
        // Handle status change events
        if (bits & EVENT_BIT_STATUS_CHANGE) {
            // Status changed - update status bar
        }
        
        if (bits & EVENT_BIT_SYSTEM_ERROR) {
            // System error - show error indicator
        }
        
        if (bits & EVENT_BIT_HEAP_LOW) {
            // Low heap - show memory warning
        }
        
        // Update every 5 seconds - much less frequent than main display
        // Periodic diagnostics: stack high water marks every 30s
        static uint32_t lastDiag = 0;
        if (millis() - lastDiag > 30000) {
            lastDiag = millis();
            auto hwm = [](TaskHandle_t h){ return h ? (unsigned)uxTaskGetStackHighWaterMark(h) : 0u; };
            Serial.printf("HWM words | Button:%u Display:%u Status:%u PLC:%u CatM:%u Web:%u Storage:%u\n",
                hwm(buttonTaskHandle), hwm(displayTaskHandle), hwm(statusBarTaskHandle), hwm(stampPLCTaskHandle),
                hwm(catmGnssTaskHandle), hwm(webTaskHandle), hwm(storageTaskHandle));
        }
    }
    #endif // !LVGL_UI_TEST_MODE
}

// ============================================================================
// DISPLAY PAGE FUNCTIONS
// ============================================================================

// POST Boot Screen
void drawBootScreen(const char* status, int progress, bool passed) {
    constexpr size_t BOOT_LOG_CAPACITY = 8;
    static BootLogEntry logEntries[BOOT_LOG_CAPACITY];
    static size_t logCount = 0;
    static int bootProgress = 0;

    if (progress >= 0) {
        if (progress > 100) progress = 100;
        if (progress < 0) progress = 0;
        bootProgress = progress;
    }

    if (status && status[0]) {
        BootLogEntry entry;
        entry.text = status;
        entry.passed = passed;
        if (logCount < BOOT_LOG_CAPACITY) {
            logEntries[logCount++] = entry;
        } else {
            for (size_t i = 0; i < BOOT_LOG_CAPACITY - 1; ++i) {
                logEntries[i] = logEntries[i + 1];
            }
            logEntries[BOOT_LOG_CAPACITY - 1] = entry;
        }
    }

    auto& display = M5StamPLC.Display;
    const int16_t width = display.width();
    const int16_t height = display.height();

    display.fillScreen(BLACK);

    display.setTextSize(2);
    display.setTextColor(CYAN, BLACK);
    display.setCursor(10, 8);
    display.print("StampPLC POST");

    display.setTextSize(1);
    display.setTextColor(WHITE, BLACK);
    display.setCursor(10, 26);
    display.printf("Firmware %s", STAMPLC_VERSION);

    const int barX = 10;
    const int barY = 38;
    const int barW = width - 20;
    const int barH = 10;
    display.drawRect(barX, barY, barW, barH, WHITE);
    int fillW = (barW - 2) * bootProgress / 100;
    if (fillW > 0) {
        display.fillRect(barX + 1, barY + 1, fillW, barH - 2, GREEN);
    }
    display.setCursor(width - 60, barY + 14);
    display.setTextColor(0x7BEF, BLACK);
    display.printf("%3d%%", bootProgress);

    display.setCursor(10, barY + 14);
    display.setTextColor(0x7BEF, BLACK);
    display.print("Diagnostics");

    const int logTop = barY + 26;
    const int lineHeight = 12;
    for (size_t i = 0; i < logCount; ++i) {
        const auto& entry = logEntries[i];
        display.setCursor(12, logTop + static_cast<int>(i) * lineHeight);
        display.setTextColor(entry.passed ? GREEN : RED, BLACK);
        display.printf("%s %s", entry.passed ? "[OK ]" : "[FAIL]", entry.text.c_str());
    }

    int logAreaBottom = logTop + static_cast<int>(logCount) * lineHeight;
    if (logAreaBottom < height) {
        display.fillRect(10, logAreaBottom, width - 20, height - logAreaBottom, BLACK);
    }
}

void drawLandingPage() {
    auto& display = M5StamPLC.Display;
    display.setTextSize(2);
    display.setTextColor(CYAN, BLACK);
    display.setCursor(COL1_X, CONTENT_TOP);
    display.print("System Overview");

    display.setTextSize(1);
    display.setTextColor(WHITE, BLACK);
    int16_t y = CONTENT_TOP + 20;

    struct tm rtcTime {};
    M5StamPLC.RX8130.getTime(&rtcTime);
    char timeBuf[40];
    snprintf(timeBuf, sizeof(timeBuf), "%04d-%02d-%02d %02d:%02d:%02d",
             rtcTime.tm_year + 1900, rtcTime.tm_mon + 1, rtcTime.tm_mday,
             rtcTime.tm_hour, rtcTime.tm_min, rtcTime.tm_sec);
    display.setCursor(COL1_X, y);
    display.printf("Time: %s", timeBuf);
    y += 14;

    String carrier = "Offline";
    int signalDbm = -120;
    bool cellConnected = false;
    if (catmGnssModule && catmGnssModule->isModuleInitialized()) {
        CellularData cd = catmGnssModule->getCellularData();
        if (cd.operatorName.length()) carrier = cd.operatorName;
        signalDbm = cd.signalStrength;
        cellConnected = cd.isConnected;
    }
    int signalPercent = (int)constrain(map((long)signalDbm, -120, -50, 0, 100), 0L, 100L);
    int bars = (signalPercent + 19) / 20;
    char barGraph[6];
    for (int i = 0; i < 5; ++i) {
        barGraph[i] = (i < bars && cellConnected) ? '#' : '.';
    }
    barGraph[5] = '\0';
    display.setCursor(COL1_X, y);
    display.printf("Cell: %s dBm:%d [%s]", carrier.c_str(), signalDbm, barGraph);
    y += 14;

    bool gnssLocked = false;
    uint8_t sats = 0;
    uint32_t fixAgeSec = 0;
    if (catmGnssModule && catmGnssModule->isModuleInitialized()) {
        GNSSData gnss = catmGnssModule->getGNSSData();
        gnssLocked = gnss.isValid;
        sats = gnss.satellites;
        if (gnssLocked && gnss.lastUpdate != 0) {
            fixAgeSec = (millis() - gnss.lastUpdate) / 1000U;
        }
    }
    display.setCursor(COL1_X, y);
    display.printf("GPS: %s | sats:%u | last fix:%us",
                   gnssLocked ? "LOCK" : "search", sats, gnssLocked ? fixAgeSec : 0);
    y += 14;

    float sdFreeMB = -1.0f;
    float sdTotalMB = 0.0f;
    if (sdModule && sdModule->isMounted()) {
        uint64_t total = sdModule->totalBytes();
        uint64_t used = sdModule->usedBytes();
        if (total > 0 && used <= total) {
            sdTotalMB = total / (1024.0f * 1024.0f);
            sdFreeMB = (total - used) / (1024.0f * 1024.0f);
        }
    }
    display.setCursor(COL1_X, y);
    if (sdFreeMB >= 0.0f) {
        display.printf("SD: %.1f MB free / %.1f MB", sdFreeMB, sdTotalMB);
    } else {
        display.print("SD: Not detected");
    }
    y += 14;

    uint32_t freeHeap = ESP.getFreeHeap();
    display.setCursor(COL1_X, y);
    display.printf("Memory: %.1f MB free", freeHeap / (1024.0f * 1024.0f));
}

void drawCardBox(lgfx::LGFX_Sprite* s, int x, int y, int w, int h, const char* title, lgfx::LGFX_Sprite* icon) {
    // Enhanced card styling with better visual hierarchy
    s->fillRoundRect(x, y, w, h, 8, 0x212121);
    s->drawRoundRect(x, y, w, h, 8, 0x6A6A6A);
    
    // Add subtle inner highlight
    s->drawRoundRect(x + 1, y + 1, w - 2, h - 2, 7, 0x3A3A3A);
    
    // Draw icon if provided
    if (icon != nullptr) {
        icon->pushSprite(s, x + 6, y + 6);
        s->setCursor(x + 18, y + 8);
    } else {
        s->setCursor(x + 10, y + 8);
    }
    
    // Draw title with better positioning and font
    s->setTextColor(WHITE);
    s->setTextSize(1);
    s->print(title);
    
    // Add subtle bottom accent line
    s->drawFastHLine(x + 8, y + h - 2, w - 16, 0x4A4A4A);
}

// Helper function to check if date is in DST period (US rules)
// RTC time management functions
bool setRTCFromCellular() {
    if (!catmGnssModule || !catmGnssModule->isModuleInitialized()) return false;
    
    CellularData cd = catmGnssModule->getCellularData();
    if (!cd.isConnected) {
        Serial.println("RTC: Cellular data session not active; attempting to read network clock anyway");
    }
    
    // Get network time from cellular module, trigger CNTP if clock not ready
    struct tm networkTime;
    if (!catmGnssModule->getNetworkTime(networkTime)) {
        Serial.println("RTC: +CCLK? did not return time, attempting CNTP sync...");
        if (!catmGnssModule->syncNetworkTime(networkTime, 65000)) {
            Serial.printf("RTC: CNTP sync failed: %s\n", catmGnssModule->getLastError().c_str());
            return false;
        }
        g_cntpSyncedThisSession = true;
        g_lastCellularNtpUsedCntp = true;
    }
    
    // Set RTC time (UTC)
    if (M5StamPLC.RX8130.begin()) {
        M5StamPLC.RX8130.setTime(&networkTime);
        Serial.printf("RTC synchronized from cellular network: %04d-%02d-%02d %02d:%02d:%02d UTC\n", 
                     networkTime.tm_year + 1900, networkTime.tm_mon + 1, networkTime.tm_mday,
                     networkTime.tm_hour, networkTime.tm_min, networkTime.tm_sec);
        return true;
    }
    return false;
}

bool setRTCFromBuildTimestamp() {
    const char* buildDate = FIRMWARE_BUILD_DATE; // example: "Jan  7 2025"
    const char* buildTime = FIRMWARE_BUILD_TIME; // example: "15:04:05"

    char monthStr[4] = {0};
    int day = 0;
    int year = 0;
    if (sscanf(buildDate, "%3s %d %d", monthStr, &day, &year) != 3) {
        return false;
    }

    static const char* months[] = {
        "Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"
    };
    int month = -1;
    for (int i = 0; i < 12; ++i) {
        if (strcasecmp(monthStr, months[i]) == 0) {
            month = i + 1;
            break;
        }
    }
    if (month == -1) return false;

    int hour = 0, minute = 0, second = 0;
    if (sscanf(buildTime, "%d:%d:%d", &hour, &minute, &second) != 3) {
        return false;
    }

    struct tm tmUtc {};
    tmUtc.tm_year = year - 1900;
    tmUtc.tm_mon = month - 1;
    tmUtc.tm_mday = day;
    tmUtc.tm_hour = hour;
    tmUtc.tm_min = minute;
    tmUtc.tm_sec = second;
    tmUtc.tm_isdst = 0;

    time_t epoch = mktime(&tmUtc);
    if (epoch <= 0) return false;

    struct timeval tv;
    tv.tv_sec = epoch;
    tv.tv_usec = 0;
    settimeofday(&tv, nullptr);

    if (M5StamPLC.RX8130.begin()) {
        M5StamPLC.RX8130.setTime(&tmUtc);
        Serial.printf("RTC seeded from firmware build timestamp: %04d-%02d-%02d %02d:%02d:%02d\n",
                      year, month, day, hour, minute, second);
        return true;
    }
    return false;
}

bool setRTCFromGPS(const GNSSData& gnssData) {
    if (!gnssData.isValid || gnssData.year < 2020) return false;
    
    // Create tm structure for RTC (store as UTC)
    struct tm rtcTime;
    rtcTime.tm_year = gnssData.year - 1900;  // tm_year is years since 1900
    rtcTime.tm_mon = gnssData.month - 1;     // tm_mon is 0-11
    rtcTime.tm_mday = gnssData.day;
    rtcTime.tm_hour = gnssData.hour;
    rtcTime.tm_min = gnssData.minute;
    rtcTime.tm_sec = gnssData.second;
    
    // Calculate day of week
    int m = gnssData.month;
    int y = gnssData.year;
    if (m < 3) {
        m += 12;
        y -= 1;
    }
    int dayOfWeek = (gnssData.day + (13 * (m + 1)) / 5 + y + y / 4 - y / 100 + y / 400) % 7;
    rtcTime.tm_wday = (dayOfWeek + 5) % 7; // Adjust to 0=Sunday
    
    // Set RTC time (UTC)
    if (M5StamPLC.RX8130.begin()) {
        M5StamPLC.RX8130.setTime(&rtcTime);
        Serial.printf("RTC synchronized from GPS: %04d-%02d-%02d %02d:%02d:%02d UTC\n", 
                     gnssData.year, gnssData.month, gnssData.day, 
                     gnssData.hour, gnssData.minute, gnssData.second);
        return true;
    }
    return false;
}

bool getRTCTime(struct tm& timeinfo) {
    if (M5StamPLC.RX8130.begin()) {
        M5StamPLC.RX8130.getTime(&timeinfo);
        // Fix tm structure (RTC returns year as 2-digit, month as 1-12)
        if (timeinfo.tm_year < 100) timeinfo.tm_year += 100; // Convert to years since 1900
        timeinfo.tm_mon -= 1; // Convert to 0-11 range
        return true;
    }
    return false;
}

// Helper function to set custom font if available
void setCustomFont(lgfx::LGFX_Sprite* s, int size = 1) {
    s->setTextSize(size);
    // Custom font will be used if loaded, otherwise falls back to default
}

void drawGNSSPage() {
    int16_t yOffset = scrollGNSS;
    // Title
    M5StamPLC.Display.setTextColor(WHITE);
    M5StamPLC.Display.setTextSize(2);
    M5StamPLC.Display.setCursor(COL1_X, CONTENT_TOP - yOffset);
    M5StamPLC.Display.println("GNSS Status");
    
    // GNSS data
    if (catmGnssModule && catmGnssModule->isModuleInitialized()) {
        GNSSData data = catmGnssModule->getGNSSData();
        auto formatBytes = [](uint64_t bytes) -> String {
            static const char* units[] = {"B", "KB", "MB", "GB"};
            double value = static_cast<double>(bytes);
            size_t idx = 0;
            while (value >= 1024.0 && idx < 3) {
                value /= 1024.0;
                ++idx;
            }
            char buf[32];
            if (idx == 0 || value >= 100.0) {
                snprintf(buf, sizeof(buf), "%.0f %s", value, units[idx]);
            } else {
                snprintf(buf, sizeof(buf), "%.1f %s", value, units[idx]);
            }
            return String(buf);
        };

        auto formatRate = [](uint32_t bytesPerSec) -> String {
            if (bytesPerSec == 0) {
                return String("0 B/s");
            }
            static const char* units[] = {"B/s", "KB/s", "MB/s"};
            double value = static_cast<double>(bytesPerSec);
            size_t idx = 0;
            while (value >= 1024.0 && idx < 2) {
                value /= 1024.0;
                ++idx;
            }
            char buf[32];
            if (idx == 0 || value >= 100.0) {
                snprintf(buf, sizeof(buf), "%.0f %s", value, units[idx]);
            } else {
                snprintf(buf, sizeof(buf), "%.1f %s", value, units[idx]);
            }
            return String(buf);
        };

        
        M5StamPLC.Display.setTextSize(1);
        M5StamPLC.Display.setTextColor(YELLOW);
        M5StamPLC.Display.setCursor(COL1_X, CONTENT_TOP + LINE_H2 - yOffset);
        M5StamPLC.Display.println("Position Data:");
        
        M5StamPLC.Display.setTextColor(WHITE);
        if (data.isValid) {
            M5StamPLC.Display.setCursor(COL1_X, CONTENT_TOP + LINE_H2 + LINE_H1 - yOffset);
            M5StamPLC.Display.printf("Latitude:  %.6f", data.latitude);
            
            M5StamPLC.Display.setCursor(COL1_X, CONTENT_TOP + LINE_H2 + LINE_H1 * 2 - yOffset);
            M5StamPLC.Display.printf("Longitude: %.6f", data.longitude);
            
            M5StamPLC.Display.setCursor(COL1_X, CONTENT_TOP + LINE_H2 + LINE_H1 * 3 - yOffset);
            M5StamPLC.Display.printf("Altitude:  %.1f m", data.altitude);
            
            M5StamPLC.Display.setCursor(COL1_X, CONTENT_TOP + LINE_H2 + LINE_H1 * 4 - yOffset);
            M5StamPLC.Display.printf("Speed:     %.1f km/h", data.speed);
            
            M5StamPLC.Display.setCursor(COL1_X, CONTENT_TOP + LINE_H2 + LINE_H1 * 5 - yOffset);
            M5StamPLC.Display.printf("Course:    %.1f deg", data.course);
        } else {
            M5StamPLC.Display.setTextColor(RED);
            M5StamPLC.Display.setCursor(COL1_X, CONTENT_TOP + LINE_H2 + LINE_H1 - yOffset);
            M5StamPLC.Display.println("No valid fix");
        }
        
        M5StamPLC.Display.setTextColor(YELLOW);
        M5StamPLC.Display.setCursor(COL1_X, CONTENT_TOP + LINE_H2 + LINE_H1 * 6 - yOffset);
        M5StamPLC.Display.println("Satellites:");
        
        M5StamPLC.Display.setTextColor(WHITE);
        M5StamPLC.Display.setCursor(COL1_X, CONTENT_TOP + LINE_H2 + LINE_H1 * 7 - yOffset);
        M5StamPLC.Display.printf("In view: %d", data.satellites);
        
        // Time data
        if (data.isValid) {
            M5StamPLC.Display.setTextColor(CYAN);
            M5StamPLC.Display.setCursor(COL2_X, CONTENT_TOP + LINE_H2 + LINE_H1 - yOffset);
            M5StamPLC.Display.println("UTC Time:");
            
            M5StamPLC.Display.setTextColor(WHITE);
            M5StamPLC.Display.setCursor(COL2_X, CONTENT_TOP + LINE_H2 + LINE_H1 * 2 - yOffset);
            M5StamPLC.Display.printf("%04d-%02d-%02d", data.year, data.month, data.day);
            
            M5StamPLC.Display.setCursor(COL2_X, CONTENT_TOP + LINE_H2 + LINE_H1 * 3 - yOffset);
            M5StamPLC.Display.printf("%02d:%02d:%02d", data.hour, data.minute, data.second);
        }
    } else {
        M5StamPLC.Display.setTextSize(1);
        M5StamPLC.Display.setTextColor(RED);
        M5StamPLC.Display.setCursor(10, 60);
        M5StamPLC.Display.println("GNSS module not initialized");
    }
}

void drawCellularPage() {
    int16_t yOffset = scrollCELL;
    // Title
    M5StamPLC.Display.setTextColor(WHITE);
    M5StamPLC.Display.setTextSize(2);
    M5StamPLC.Display.setCursor(COL1_X, CONTENT_TOP - yOffset);
    M5StamPLC.Display.println("Cellular Status");
    
    // Cellular data
    if (catmGnssModule && catmGnssModule->isModuleInitialized()) {
        CellularData data = catmGnssModule->getCellularData();
        char txSummary[64], rxSummary[64];
        snprintf(txSummary, sizeof(txSummary), "%llu bytes (%u bps)", data.txBytes, data.txBps);
        snprintf(rxSummary, sizeof(rxSummary), "%llu bytes (%u bps)", data.rxBytes, data.rxBps);
        
        M5StamPLC.Display.setTextSize(1);
        M5StamPLC.Display.setTextColor(YELLOW);
        M5StamPLC.Display.setCursor(COL1_X, CONTENT_TOP + LINE_H2 - yOffset);
        M5StamPLC.Display.println("Network Status:");
        
        M5StamPLC.Display.setTextColor(data.isConnected ? GREEN : RED);
        M5StamPLC.Display.setCursor(COL1_X, CONTENT_TOP + LINE_H2 + LINE_H1 - yOffset);
        M5StamPLC.Display.printf("Connected: %s", data.isConnected ? "YES" : "NO");
        
        M5StamPLC.Display.setTextColor(WHITE);
        M5StamPLC.Display.setCursor(COL1_X, CONTENT_TOP + LINE_H2 + LINE_H1 * 2 - yOffset);
        M5StamPLC.Display.printf("Operator: %s", data.operatorName.c_str());
        
        // Signal strength bar
        drawSignalBar(COL1_X, CONTENT_TOP + LINE_H2 + LINE_H1 * 3 - yOffset, 120, 12, data.signalStrength);
        
        M5StamPLC.Display.setTextColor(YELLOW);
        M5StamPLC.Display.setCursor(COL1_X, CONTENT_TOP + LINE_H2 + LINE_H1 * 5 - yOffset);
        M5StamPLC.Display.println("Device Info:");
        
        M5StamPLC.Display.setTextColor(WHITE);
        M5StamPLC.Display.setCursor(COL1_X, CONTENT_TOP + LINE_H2 + LINE_H1 * 6 - yOffset);
        M5StamPLC.Display.printf("IMEI: %s", data.imei.c_str());
        
        M5StamPLC.Display.setCursor(COL1_X, CONTENT_TOP + LINE_H2 + LINE_H1 * 7 - yOffset);
        M5StamPLC.Display.printf("Last Update: %d ms ago", millis() - data.lastUpdate);
        
        M5StamPLC.Display.setCursor(COL1_X, CONTENT_TOP + LINE_H2 + LINE_H1 * 8 - yOffset);
        M5StamPLC.Display.printf("Errors: %d", data.errorCount);
        M5StamPLC.Display.setCursor(COL1_X, CONTENT_TOP + LINE_H2 + LINE_H1 * 9 - yOffset);
        M5StamPLC.Display.setCursor(COL1_X, CONTENT_TOP + LINE_H2 + LINE_H1 * 8 - yOffset);
        M5StamPLC.Display.printf("Errors: %d", data.errorCount);
        M5StamPLC.Display.printf("Tx: %s", txSummary);
        M5StamPLC.Display.setCursor(COL1_X, CONTENT_TOP + LINE_H2 + LINE_H1 * 10 - yOffset);
        M5StamPLC.Display.printf("Rx: %s", rxSummary);
        
        // Add some spacing at bottom
        M5StamPLC.Display.setCursor(COL1_X, CONTENT_BOTTOM - LINE_H1 * 2 - yOffset);
        M5StamPLC.Display.print(""); // Empty line for spacing
    } else {
        M5StamPLC.Display.setTextSize(1);
        M5StamPLC.Display.setTextColor(RED);
        M5StamPLC.Display.setCursor(10, 60);
        M5StamPLC.Display.println("Cellular module not initialized");
    }
}

void drawSystemPage() {
    int16_t yOffset = scrollSYS;
    // Title
    M5StamPLC.Display.setTextColor(WHITE);
    M5StamPLC.Display.setTextSize(2);
    M5StamPLC.Display.setCursor(COL1_X, CONTENT_TOP - yOffset);
    M5StamPLC.Display.println("System Status");
    
    // System info - compact layout
    M5StamPLC.Display.setTextSize(1);
    M5StamPLC.Display.setTextColor(YELLOW);
    M5StamPLC.Display.setCursor(COL1_X, CONTENT_TOP + LINE_H2 - yOffset);
    M5StamPLC.Display.println("Memory:");
    
    M5StamPLC.Display.setTextColor(WHITE);
    M5StamPLC.Display.setCursor(COL1_X, CONTENT_TOP + LINE_H2 + LINE_H1 - yOffset);
    M5StamPLC.Display.printf("Free RAM: %d KB", ESP.getFreeHeap() / 1024);
    
    M5StamPLC.Display.setCursor(COL1_X, CONTENT_TOP + LINE_H2 + LINE_H1 * 2 - yOffset);
    M5StamPLC.Display.printf("Total RAM: %d KB", ESP.getHeapSize() / 1024);
    
    M5StamPLC.Display.setCursor(COL1_X, CONTENT_TOP + LINE_H2 + LINE_H1 * 3 - yOffset);
    M5StamPLC.Display.printf("CPU Freq: %d MHz", ESP.getCpuFreqMHz());
    
    // WiFi connection bar
    // Extra spacing before WiFi bar
    drawWiFiBar(COL1_X, CONTENT_TOP + LINE_H2 + LINE_H1 * 5 - yOffset, 120, 12);
    
    // Module status - moved up
    M5StamPLC.Display.setTextColor(YELLOW);
    M5StamPLC.Display.setCursor(COL1_X, CONTENT_TOP + LINE_H2 + LINE_H1 * 7 - yOffset);
    M5StamPLC.Display.println("Module Status:");
    
    M5StamPLC.Display.setTextColor(WHITE);
    M5StamPLC.Display.setCursor(COL1_X, CONTENT_TOP + LINE_H2 + LINE_H1 * 8 - yOffset);
    M5StamPLC.Display.printf("StampPLC: %s", stampPLC && stampPLC->isReady() ? "READY" : "NOT READY");
    
    M5StamPLC.Display.setCursor(COL1_X, CONTENT_TOP + LINE_H2 + LINE_H1 * 9 - yOffset);
    M5StamPLC.Display.printf("CatM+GNSS: %s", catmGnssModule && catmGnssModule->isModuleInitialized() ? "READY" : "NOT READY");
    
    // SD card status
    M5StamPLC.Display.setCursor(COL1_X, CONTENT_TOP + LINE_H2 + LINE_H1 * 10 - yOffset);
    M5StamPLC.Display.printf("SD: %s", (sdModule && sdModule->isMounted()) ? "MOUNTED" : "NOT PRESENT");
    if (sdModule && sdModule->isMounted()) {
        drawSDInfo(COL2_X, CONTENT_TOP + LINE_H2 + LINE_H1 * 9 - yOffset);
    }
    
    // Hardware sensor data
    M5StamPLC.Display.setTextColor(YELLOW);
    M5StamPLC.Display.setCursor(COL1_X, CONTENT_TOP + LINE_H2 + LINE_H1 * 12 - yOffset);
    M5StamPLC.Display.println("Hardware Sensors:");
    
    M5StamPLC.Display.setTextColor(WHITE);
    // Temperature
    float temperature = M5StamPLC.getTemp();
    M5StamPLC.Display.setCursor(COL1_X, CONTENT_TOP + LINE_H2 + LINE_H1 * 13 - yOffset);
    M5StamPLC.Display.printf("Temperature: %.1f°C", temperature);
    
    // Power monitoring
    float voltage = M5StamPLC.INA226.getBusVoltage();
    float current = M5StamPLC.INA226.getShuntCurrent();
    M5StamPLC.Display.setCursor(COL1_X, CONTENT_TOP + LINE_H2 + LINE_H1 * 14 - yOffset);
    M5StamPLC.Display.printf("Power: %.2fV, %.3fA", voltage, current);
    
    // RTC status
    struct tm rtcTime;
    bool rtcValid = false;
    if (M5StamPLC.RX8130.begin()) {
        M5StamPLC.RX8130.getTime(&rtcTime);
        rtcValid = (rtcTime.tm_year >= 100); // Valid if year > 2000
    }
    M5StamPLC.Display.setCursor(COL1_X, CONTENT_TOP + LINE_H2 + LINE_H1 * 15 - yOffset);
    M5StamPLC.Display.printf("RTC: %s", rtcValid ? "VALID" : "NOT SET");
    
    // Uptime - moved up
    // Removed uptime per request
}

void drawSettingsPage() {
    int16_t yOffset = scrollSETTINGS;
    
    // Title
    M5StamPLC.Display.setTextColor(WHITE);
    M5StamPLC.Display.setTextSize(3);
    M5StamPLC.Display.setCursor(COL1_X, CONTENT_TOP - yOffset);
    M5StamPLC.Display.println("Settings");
    
    // SD Card information
    M5StamPLC.Display.setTextSize(2);
    M5StamPLC.Display.setTextColor(YELLOW);
    M5StamPLC.Display.setCursor(COL1_X, CONTENT_TOP + LINE_H2 * 2 - yOffset);
    M5StamPLC.Display.println("SD Card");
    
    M5StamPLC.Display.setTextSize(1);
    M5StamPLC.Display.setTextColor(WHITE);
    M5StamPLC.Display.setCursor(COL1_X, CONTENT_TOP + LINE_H2 * 4 - yOffset);
    if (sdModule && sdModule->isMounted()) {
        M5StamPLC.Display.println("Status: Mounted");
        
        // Show SD card info
        M5StamPLC.Display.setCursor(COL1_X, CONTENT_TOP + LINE_H2 * 5 - yOffset);
        drawSDInfo(COL1_X, CONTENT_TOP + LINE_H2 * 5 - yOffset);
    } else {
        M5StamPLC.Display.println("Status: Not mounted");
    }
}

void drawLogsPage() {
    int16_t yOffset = scrollLOGS;
    M5StamPLC.Display.setTextColor(WHITE);
    M5StamPLC.Display.setTextSize(2);
    M5StamPLC.Display.setCursor(COL1_X, CONTENT_TOP - yOffset);
    M5StamPLC.Display.println("Logs");

    // Draw recent lines (oldest at top)
    M5StamPLC.Display.setTextSize(1);
    const int pad = 2;
    size_t cnt = log_count();
    int contentH = (int)cnt * LINE_H1 + pad * 2;
    scrollLOGS = clampScroll(scrollLOGS, contentH);

    int y = CONTENT_TOP + LINE_H2 - yOffset;
    char line[160];
    for (size_t i = 0; i < cnt; ++i) {
        if (log_get_line(i, line, sizeof(line))) {
            if (y > CONTENT_TOP && y < CONTENT_BOTTOM) {
                M5StamPLC.Display.setCursor(COL1_X, y);
                M5StamPLC.Display.print(line);
            }
            y += LINE_H1;
        }
    }
}

void drawButtonIndicators() {
    M5StamPLC.Display.setTextSize(1);
    M5StamPLC.Display.setTextColor(WHITE);

    bool onLanding = (currentPage == DisplayPage::LANDING_PAGE);
    if (onLanding) {
        M5StamPLC.Display.setCursor(10, BUTTON_BAR_Y + 2);
        M5StamPLC.Display.print("A: CELL");
        M5StamPLC.Display.setCursor(95, BUTTON_BAR_Y + 2);
        M5StamPLC.Display.print("B: GPS");
        M5StamPLC.Display.setCursor(165, BUTTON_BAR_Y + 2);
        M5StamPLC.Display.print("C: SYSTEM");
        return;
    }

    // Special handling for settings page
    if (currentPage == DisplayPage::SETTINGS_PAGE) {
        M5StamPLC.Display.setCursor(10, BUTTON_BAR_Y + 2);
        M5StamPLC.Display.print("A: HOME");
        M5StamPLC.Display.setCursor(100, BUTTON_BAR_Y + 2);
        M5StamPLC.Display.print("B: UP");
        M5StamPLC.Display.setCursor(180, BUTTON_BAR_Y + 2);
        M5StamPLC.Display.print("C: DOWN");
        return;
    }

    // Global controls: A=HOME, B=PREV, C=NEXT
    M5StamPLC.Display.setCursor(10, BUTTON_BAR_Y + 2);
    M5StamPLC.Display.print("A: HOME");
    M5StamPLC.Display.setCursor(100, BUTTON_BAR_Y + 2);
    M5StamPLC.Display.print("B: PREV");
    M5StamPLC.Display.setCursor(185, BUTTON_BAR_Y + 2);
    M5StamPLC.Display.print("C: NEXT");
}

void drawSDInfo(int x, int y) {
    if (!sdModule || !sdModule->isMounted()) return;
    M5StamPLC.Display.setTextSize(1);
    M5StamPLC.Display.setTextColor(CYAN);
    M5StamPLC.Display.setCursor(x, y);
    uint64_t total = sdModule->totalBytes();
    uint64_t used = sdModule->usedBytes();
    M5StamPLC.Display.printf("SD Total: %.1f MB", total ? (double)total / (1024.0 * 1024.0) : 0.0);
    M5StamPLC.Display.setCursor(x, y + 20);
    M5StamPLC.Display.printf("SD Used:  %.1f MB", used ? (double)used / (1024.0 * 1024.0) : 0.0);
}

// Compact signal bar for status bar sprite
void drawCompactSignalBar(void* sprite, int16_t x, int16_t y, int8_t signal, uint16_t color) {
    lgfx::LGFX_Sprite* spr = (lgfx::LGFX_Sprite*)sprite;
    if (!spr) return;
    
    // Draw mini signal bar (5 bars, 2px wide each)
    for (int i = 0; i < 5; i++) {
        int barHeight = (i + 1) * 2;
        int barX = x + i * 3;
        int barY = y + 10 - barHeight;
        
        if (signal >= (i + 1) * 20) {
            spr->fillRect(barX, barY, 2, barHeight, color);
        } else {
            spr->drawRect(barX, barY, 2, barHeight, 0x4A4A4A);
        }
    }
}

void drawStatusBar() {
    // Status bar removed; intentionally left blank.
}

void drawSignalBar(int16_t x, int16_t y, int16_t w, int16_t h, int8_t signalDbm) {
    // Draw signal strength bar (dBm to percentage)
    int16_t signalPercent = map(signalDbm, -120, -50, 0, 100);
    signalPercent = constrain(signalPercent, 0, 100);
    
    // Background
    M5StamPLC.Display.fillRect(x, y, w, h, 0x2104); // Dark gray
    M5StamPLC.Display.drawRect(x, y, w, h, WHITE);
    
    // Signal level
    uint16_t signalColor = (signalPercent > 70) ? GREEN : 
                          (signalPercent > 40) ? YELLOW : RED;
    int16_t fillWidth = (w - 2) * signalPercent / 100;
    if (fillWidth > 0) {
        M5StamPLC.Display.fillRect(x + 1, y + 1, fillWidth, h - 2, signalColor);
    }
    
    // Text label
    M5StamPLC.Display.setTextSize(1);
    M5StamPLC.Display.setTextColor(WHITE);
    M5StamPLC.Display.setCursor(x + w + 5, y);
    M5StamPLC.Display.printf("%ddBm", signalDbm);
}

void drawWiFiBar(int16_t x, int16_t y, int16_t w, int16_t h) {
    // Background
    M5StamPLC.Display.fillRect(x, y, w, h, 0x2104); // Dark gray
    M5StamPLC.Display.drawRect(x, y, w, h, WHITE);

    wifi_mode_t mode = WIFI_MODE_NULL;
    // Guard against older cores without getMode signature
    #ifdef ESP_WIFI_INCLUDED
    mode = WiFi.getMode();
    #else
    mode = WiFi.getMode();
    #endif

    bool isSta = (mode & WIFI_MODE_STA) != 0;
    bool isAp  = (mode & WIFI_MODE_AP)  != 0;

    if (isSta && WiFi.status() == WL_CONNECTED) {
        int8_t wifiRSSI = WiFi.RSSI();
        int16_t wifiPercent = map(wifiRSSI, -100, -30, 0, 100);
        wifiPercent = constrain(wifiPercent, 0, 100);
        uint16_t wifiColor = (wifiPercent > 70) ? GREEN : (wifiPercent > 40) ? YELLOW : RED;
        int16_t fillWidth = (w - 2) * wifiPercent / 100;
        if (fillWidth > 0) {
            M5StamPLC.Display.fillRect(x + 1, y + 1, fillWidth, h - 2, wifiColor);
        }
        M5StamPLC.Display.setTextSize(1);
        M5StamPLC.Display.setTextColor(WHITE);
        M5StamPLC.Display.setCursor(x + w + 5, y);
        M5StamPLC.Display.printf("STA:%ddBm", wifiRSSI);
        return;
    }

    if (isAp) {
        // Show AP active; client count if available
        uint16_t color = YELLOW;
        M5StamPLC.Display.fillRect(x + 1, y + 1, w - 2, h - 2, color);
        M5StamPLC.Display.setTextSize(1);
        M5StamPLC.Display.setTextColor(WHITE);
        M5StamPLC.Display.setCursor(x + w + 5, y);
        // softAPgetStationNum may not exist on all cores; guard with weak behavior
        #ifdef ARDUINO_ARCH_ESP32
        M5StamPLC.Display.printf("AP:%d", WiFi.softAPgetStationNum());
        #else
        M5StamPLC.Display.print("AP:ON");
        #endif
        return;
    }

    // No WiFi
    M5StamPLC.Display.setTextSize(1);
    M5StamPLC.Display.setTextColor(RED);
    M5StamPLC.Display.setCursor(x + w + 5, y);
    M5StamPLC.Display.print("WiFi:OFF");
}


// Get network type based on signal strength (simplified)
const char* getNetworkType(int8_t signalStrength) {
    // This is a simplified version - real detection would need AT commands
    if (signalStrength < -100) return "---";
    if (signalStrength < -85) return "2G";
    if (signalStrength < -75) return "3G";
    return "LTE";
}

// Initialize icon sprites
void initializeIcons() {
    if (iconsInitialized) return;
    
    // Small icons (16x16) for pages
    iconSatellite.createSprite(16, 16);
    iconGear.createSprite(16, 16);
    iconTower.createSprite(16, 16);
    iconWrench.createSprite(16, 16);
    iconGPS.createSprite(16, 16);
    iconCellular.createSprite(16, 16);
    iconLog.createSprite(16, 16);
    
    // Large icons (32x32) for launcher
    iconSatelliteBig.createSprite(32, 32);
    iconGearBig.createSprite(32, 32);
    iconTowerBig.createSprite(32, 32);
    iconWrenchBig.createSprite(32, 32);
    iconGPSBig.createSprite(32, 32);
    iconCellularBig.createSprite(32, 32);
    iconLogBig.createSprite(32, 32);
    
    // Draw simple placeholder icons (can be replaced with actual artwork)
    // For now, just fill with a color to indicate they exist
    iconSatellite.fillSprite(0x4A4A4A);
    iconGear.fillSprite(0x4A4A4A);
    iconTower.fillSprite(0x4A4A4A);
    iconWrench.fillSprite(0x4A4A4A);
    iconGPS.fillSprite(0x4A4A4A);
    iconCellular.fillSprite(0x4A4A4A);
    iconLog.fillSprite(0x4A4A4A);
    
    iconSatelliteBig.fillSprite(0x4A4A4A);
    iconGearBig.fillSprite(0x4A4A4A);
    iconTowerBig.fillSprite(0x4A4A4A);
    iconWrenchBig.fillSprite(0x4A4A4A);
    iconGPSBig.fillSprite(0x4A4A4A);
    iconCellularBig.fillSprite(0x4A4A4A);
    iconLogBig.fillSprite(0x4A4A4A);
    
    iconsInitialized = true;
}

// Sprite cleanup functions
static void cleanupContentSprite() {
    if (contentSpriteInit) {
        contentSprite.deleteSprite();
        contentSpriteInit = false;
    }
}

static void cleanupStatusSprite() {
    if (statusSpriteInit) {
        statusSprite.deleteSprite();
        statusSpriteInit = false;
    }
}

static void cleanupIconSprites() {
    if (iconsInitialized) {
        iconSatellite.deleteSprite();
        iconGear.deleteSprite();
        iconTower.deleteSprite();
        iconWrench.deleteSprite();
        iconGPS.deleteSprite();
        iconCellular.deleteSprite();
        iconLog.deleteSprite();
        iconSatelliteBig.deleteSprite();
        iconGearBig.deleteSprite();
        iconTowerBig.deleteSprite();
        iconWrenchBig.deleteSprite();
        iconGPSBig.deleteSprite();
        iconCellularBig.deleteSprite();
        iconLogBig.deleteSprite();
        iconsInitialized = false;
    }
}

void cleanupAllSprites() {
    cleanupContentSprite();
    cleanupStatusSprite();
    cleanupIconSprites();
}

// ============================================================================
// ARDUINO SETUP & LOOP
// ============================================================================
static void drawModalOverlay() {
    // Modal in content area only to avoid clashing with status bar
    const int w = M5StamPLC.Display.width();
    const int contentY = CONTENT_TOP;
    const int contentH = CONTENT_BOTTOM - CONTENT_TOP;
    const int boxW = w - 24;
    const int boxH = 78;
    const int x = (w - boxW) / 2;
    const int y = contentY + (contentH - boxH) / 2;

    // Opaque backdrop for content area to fully hide page content
    M5StamPLC.Display.fillRect(0, contentY, w, contentH, BLACK);

    // Modal box
    const uint16_t bg = 0x3186; // slightly lighter gray for contrast
    M5StamPLC.Display.fillRect(x, y, boxW, boxH, bg);
    M5StamPLC.Display.drawRect(x, y, boxW, boxH, YELLOW);

    // Title (centered)
    M5StamPLC.Display.setTextColor(WHITE);
    M5StamPLC.Display.setTextSize(2);
    {
        const char* ttl = "Unit Not Found";
        int tw = M5StamPLC.Display.textWidth(ttl);
        int tx = x + (boxW - tw) / 2;
        M5StamPLC.Display.setCursor(tx, y + 6);
        M5StamPLC.Display.print(ttl);
    }

    // Body
    M5StamPLC.Display.setTextSize(1);
    // Short, pre-wrapped lines to avoid overflow
    M5StamPLC.Display.setCursor(x + 8, y + 28);
    M5StamPLC.Display.print("Comm Unit (CatM+GNSS) not detected.");
    M5StamPLC.Display.setCursor(x + 8, y + 40);
    M5StamPLC.Display.print("Connect unit, then press C to retry.");

    if (g_commFailureDescription.length()) {
        M5StamPLC.Display.setTextColor(YELLOW);
        int16_t detailsTop = y + 52;
        drawWrappedText(g_commFailureDescription, x + 8, detailsTop, boxW - 16, 10);
    }

    // Actions row
    M5StamPLC.Display.setTextColor(CYAN);
    M5StamPLC.Display.setCursor(x + 8, y + boxH - 16);
    M5StamPLC.Display.print("A: OK");
    M5StamPLC.Display.setCursor(x + boxW - 80, y + boxH - 16);
    M5StamPLC.Display.print("C: Retry");

    // Optional toast message while retrying
    if (g_retryToastActive && (int32_t)(g_retryToastUntilMs - millis()) > 0) {
        M5StamPLC.Display.setTextColor(YELLOW);
        const char* toast = "Retrying...";
        int tw = M5StamPLC.Display.textWidth(toast);
        int tx = x + (boxW - tw) / 2;
        M5StamPLC.Display.setCursor(tx, y + boxH - 30);
        M5StamPLC.Display.print(toast);
    } else {
        g_retryToastActive = false;
    }
}

static void tryInitCatMIfAbsent(bool forced) {
    uint32_t now = millis();
    if (!forced && (now - g_lastCatMProbeMs) < 60000UL) return; // 60s interval
    g_lastCatMProbeMs = now;

    // Clean up existing instance if present but not working
    if (catmGnssModule) {
        if (!catmGnssModule->isModuleInitialized()) {
            Serial.println("[CatM] Cleaning up failed module instance");
            // Delete existing task if running
            if (catmGnssTaskHandle) {
                vTaskDelete(catmGnssTaskHandle);
                catmGnssTaskHandle = nullptr;
            }
            // Clean up module
            delete catmGnssModule;
            catmGnssModule = nullptr;
        } else {
            return; // Module is working, no need to re-probe
        }
    }

    Serial.println("[CatM] Background re-probe starting...");
    CatMGNSSModule* m = new CatMGNSSModule();
    if (!m) {
        Serial.println("[CatM] Failed to allocate module instance");
        g_commFailureDescription = "Out of memory while allocating CatM module";
        return;
    }
    
    if (m->begin()) {
        catmGnssModule = m;
        Serial.println("[CatM] Hot-attach successful");
        log_add("CatM+GNSS hot-attached");
        g_commFailureDescription = "";
        // Spawn the CatMGNSS worker task
        BaseType_t taskResult = xTaskCreatePinnedToCore(
            vTaskCatMGNSS,
            "CatMGNSS",
            TASK_STACK_SIZE_APP_GNSS,
            catmGnssModule,
            TASK_PRIORITY_GNSS,
            &catmGnssTaskHandle,
            0
        );
        if (taskResult == pdPASS) {
        Serial.println("CatM+GNSS task created (hot)");
        } else {
            Serial.println("Failed to create CatM+GNSS task");
            g_commFailureDescription = "Failed to create CatM worker task";
            delete catmGnssModule;
            catmGnssModule = nullptr;
            return;
        }
        // Dismiss modal if shown
        g_modalActive = false;
        g_modalType = ModalType::NONE;
        enqueueUIEvent(UIEventType::Redraw);
    } else {
        Serial.println("[CatM] Re-probe failed: not found");
        g_commFailureDescription = m->getLastError();
        delete m;
        enqueueUIEvent(UIEventType::Redraw);
    }
}

void setup() {
    // Initialize serial
    Serial.begin(SERIAL_BAUD);
    delay(500);  // Short delay for serial to initialize
    
    Serial.println("\n\n=== StampPLC CatM+GNSS Integration ===");
    Serial.println("Initializing...");
    
    set_log_level(DEBUG_LOG_LEVEL_INFO);

    // Suppress noisy init logs (VFS/SD, I2C reinit warnings)
    esp_log_level_set("vfs_api", ESP_LOG_NONE);
    esp_log_level_set("vfs", ESP_LOG_NONE);
    esp_log_level_set("sd_diskio", ESP_LOG_NONE);
    esp_log_level_set("Wire", ESP_LOG_ERROR);  // Only show errors, not warnings
    
    // Initialize I2C bus first to avoid conflicts
    Wire.begin(PLC_I2C_SDA_PIN, PLC_I2C_SCL_PIN);
    Wire.setClock(400000); // 400kHz I2C speed
    delay(100); // Allow I2C bus to stabilize
    
    // Configure M5StamPLC to disable conflicting I2C devices
    auto cfg = M5StamPLC.config();
    cfg.enableSdCard = false;      // Let SDCardModule handle SD card
    M5StamPLC.config(cfg);
    
    // Initialize M5StamPLC hardware
    M5StamPLC.begin();
    
    // Initialize display early for POST screen
    M5StamPLC.Display.setBrightness(128);
    M5StamPLC.Display.fillScreen(BLACK);
    drawBootScreen("Starting POST...", 0);
    delay(300);

    // Initialize memory monitoring and string pooling
    drawBootScreen("Initializing memory monitor", 10);
    if (!g_memoryMonitor.begin()) {
        Serial.println("ERROR: Failed to initialize memory monitor");
        drawBootScreen("Memory monitor FAILED", 10, false);
        delay(2000);
        return;
    }

    g_memoryMonitor.startMonitoring();
    Serial.println("Memory monitoring started");

    // Feed watchdog
    yield();
    delay(200);
    
    // Quick I2C bus check (skip full scan during POST to avoid timing issues)
    drawBootScreen("Checking I2C bus", 20);
    Serial.println("Checking I2C bus...");

    // Test I2C bus connectivity with a simple operation
    Wire.beginTransmission(0x00); // Ping the bus
    uint8_t busStatus = Wire.endTransmission();

    if (busStatus == 0 || busStatus == 4) { // 4 = other error, but bus responds
        Serial.println("I2C bus is responsive");
    } else {
        Serial.printf("WARNING: I2C bus error (status: %d)\n", busStatus);
        drawBootScreen("I2C bus error", 20, false);
        delay(300);
    }

    // Feed watchdog to prevent reset during POST
    yield();
    delay(200);
    
    // Initialize RTC (RX8130) with error handling
    drawBootScreen("Initializing RTC", 30);
    Serial.println("Initializing RTC (RX8130)...");
    bool rtcOk = M5StamPLC.RX8130.begin();
    if (rtcOk) {
        Serial.println("RTC (RX8130) initialized successfully");
        
        // Check if RTC has valid time (not default 2000-01-01)
        struct tm rtcTime;
        M5StamPLC.RX8130.getTime(&rtcTime);
        if (rtcTime.tm_year < 100) { // RTC returns 2-digit year
            Serial.println("RTC time not set, will sync from NTP/GPS when available");
        } else {
            Serial.printf("RTC current time: %04d-%02d-%02d %02d:%02d:%02d\n",
                         rtcTime.tm_year + 1900, rtcTime.tm_mon + 1, rtcTime.tm_mday,
                         rtcTime.tm_hour, rtcTime.tm_min, rtcTime.tm_sec);
        }
    } else {
        Serial.println("WARNING: RTC (RX8130) initialization failed - I2C communication error");
        drawBootScreen("RTC init FAILED", 30, false);
        delay(800);
    }
    yield();
    delay(200);

    // Initialize thermal sensor (LM75B) with error handling
    drawBootScreen("Initializing thermal sensor", 40);
    Serial.println("Initializing thermal sensor (LM75B)...");
    bool thermalOk = M5StamPLC.LM75B.begin();
    if (thermalOk) {
        Serial.println("Thermal sensor (LM75B) initialized successfully");
        float temp = M5StamPLC.getTemp();
        if (temp > -100.0f && temp < 200.0f) { // Reasonable temperature range
            Serial.printf("Current temperature: %.1f°C\n", temp);
        } else {
            Serial.printf("WARNING: Temperature reading out of range: %.1f°C\n", temp);
        }
    } else {
        Serial.println("WARNING: Thermal sensor (LM75B) initialization failed - I2C communication error");
        drawBootScreen("Thermal sensor FAILED", 40, false);
        delay(800);
    }
    yield();
    delay(200);

    // Initialize power monitoring (INA226) with error handling
    drawBootScreen("Initializing power monitor", 50);
    Serial.println("Initializing power monitor (INA226)...");
    bool powerOk = M5StamPLC.INA226.begin();
    if (powerOk) {
        Serial.println("Power monitor (INA226) initialized successfully");
        float voltage = M5StamPLC.INA226.getBusVoltage();
        float current = M5StamPLC.INA226.getShuntCurrent();
        if (voltage >= 0.0f && voltage <= 30.0f && current >= -5.0f && current <= 5.0f) {
            Serial.printf("Current power: %.2fV, %.3fA\n", voltage, current);
        } else {
            Serial.printf("WARNING: Power readings out of range: %.2fV, %.3fA\n", voltage, current);
        }
    } else {
        Serial.println("WARNING: Power monitor (INA226) initialization failed - I2C communication error");
        drawBootScreen("Power monitor FAILED", 50, false);
        delay(800);
    }
    yield();
    delay(200);
    
    // Allow hardware to settle after initialization
    delay(500);

    // Reduce log noise from M5GFX/LGFX components
    esp_log_level_set("lgfx", ESP_LOG_ERROR);  // Only show errors, not verbose warnings
    esp_log_level_set("esp32-hal-uart", ESP_LOG_ERROR);
    esp_log_level_set("esp32-hal-i2c", ESP_LOG_ERROR);  // Reduce I2C noise
    esp_log_level_set("M5GFX", ESP_LOG_ERROR);
    Serial.println("M5StamPLC initialized");
    Serial.printf("Display w=%d, h=%d\n", M5StamPLC.Display.width(), M5StamPLC.Display.height());
    
    // Initialize display brightness
    M5StamPLC.Display.setBrightness(displayBrightness);
    Serial.printf("Display brightness set to %d\n", displayBrightness);

    // Initialize LVGL UI (test mode)
    #if LVGL_UI_TEST_MODE
    Serial.println("=== Initializing LVGL UI Integration ===");
    
    // Create LVGL tick task (CRITICAL - must run every 1ms)
    BaseType_t result = xTaskCreatePinnedToCore(
        [](void* pvParameters) {
            for(;;) {
                ui_tick_1ms();
                vTaskDelay(pdMS_TO_TICKS(1));
            }
        },
        "LVGL_Tick",
        2048,  // Stack size
        NULL,
        TASK_PRIORITY_GNSS,     // High priority - critical timing
        &lvglTickTaskHandle,
        0      // Core 0
    );
    
    if (result == pdPASS) {
        Serial.println("LVGL tick task created successfully");
    } else {
        Serial.println("ERROR: Failed to create LVGL tick task");
    }
    
    // Start LVGL UI (this creates the GUI task)
    ui_create_gui_task();
    Serial.println("LVGL UI integration initialized");
    #endif
    
    // Initialize UI icons
    initializeIcons();
    Serial.println("UI icons initialized");
    
    // Ensure first frame draws landing page
    pageChanged = true;
    
    // Initialize StampPLC
    stampPLC = new BasicStampPLC();
    if (stampPLC->begin()) {
        Serial.println("StampPLC initialized successfully");
    } else {
        Serial.println("StampPLC initialization failed");
        delete stampPLC;
        stampPLC = nullptr;
    }
    
    // Initialize SD module (optional)
    drawBootScreen("Initializing SD card", 60);
#if ENABLE_SD
    sdModule = new SDCardModule();
    if (sdModule->begin()) {
        Serial.println("SD card detected and mounted");
        // Print detailed status
        sdModule->printStatus();
        // Quick smoke test: log boot marker
        String bootMsg = String("Boot @ ") + String((uint32_t)millis()) + " ms\n";
        sdModule->writeText("/boot.log", bootMsg.c_str(), true);
        log_add("SD card mounted");
    } else {
        Serial.println("No SD card present or mount failed");
        drawBootScreen("SD card not detected", 60, false);
        delete sdModule;
        sdModule = nullptr;
        log_add("SD card not present");
        delay(800);
    }
#else
    Serial.println("SD support disabled (ENABLE_SD=0)");
#endif
    yield();
    delay(200);

    Serial.println("DEBUG: Creating UI queue");
    // Create UI queue
    g_uiQueue = xQueueCreate(16, sizeof(UIEvent));
    if (g_uiQueue == NULL) {
        Serial.println("ERROR: Failed to create UI queue");
        return;
    }
    Serial.println("DEBUG: UI queue created");

    Serial.println("DEBUG: Creating system event group");
    // Create system event group
    xEventGroupSystemStatus = xEventGroupCreate();
    if (xEventGroupSystemStatus == NULL) {
        Serial.println("ERROR: Failed to create system event group");
        return;
    }
    Serial.println("DEBUG: System event group created");

    Serial.println("DEBUG: Creating CatM command queue");
    g_catmCommandQueue = xQueueCreate(8, sizeof(CatMCommand));
    if (g_catmCommandQueue == nullptr) {
        Serial.println("ERROR: Failed to allocate CatM command queue");
        return;
    }
    Serial.println("DEBUG: CatM command queue created");

    Serial.println("DEBUG: Initializing logging");
    // Init logging
    log_init();
    log_add("Booting StampPLC CatM+GNSS...");
    Serial.println("DEBUG: Logging initialized");

    Serial.println("DEBUG: Creating storage task");
    // Storage task (Core 1) - only if SD is enabled
#if ENABLE_SD
    xTaskCreatePinnedToCore(
        vTaskStorage,
        "Storage",
        TASK_STACK_SIZE,
        NULL,
        TASK_PRIORITY_DATA_TRANSMIT,
        &storageTaskHandle,
        1
    );
    Serial.println("Storage task created");
#else
    Serial.println("Storage task disabled (SD disabled)");
#endif
    
    // Initialize CatM+GNSS Module
    drawBootScreen("Initializing CatM+GNSS", 70);
    catmGnssModule = new CatMGNSSModule();
    if (!catmGnssModule) {
        Serial.println("ERROR: Failed to allocate CatM+GNSS module - out of memory");
        log_add("CatM+GNSS allocation failed");
        drawBootScreen("CatM+GNSS allocation FAILED", 70, false);
        g_modalActive = true;
        g_modalType = ModalType::NO_COMM_UNIT;
        pageChanged = true; // ensure UI redraw shows modal
        delay(800);
        return;
    }
    
    if (catmGnssModule->begin()) {
        Serial.println("CatM+GNSS module initialized successfully");
        log_add("CatM+GNSS initialized");
        g_commFailureDescription = "";
    } else {
        Serial.println("CatM+GNSS module initialization failed");
        drawBootScreen("CatM+GNSS init FAILED", 70, false);
        g_commFailureDescription = catmGnssModule->getLastError();
        delete catmGnssModule;
        catmGnssModule = nullptr;
        log_add("CatM+GNSS init failed");
        // Show modal alert; allow user to dismiss (A) or retry (C)
        g_modalActive = true;
        g_modalType = ModalType::NO_COMM_UNIT;
        pageChanged = true; // ensure UI redraw shows modal
        
        // Schedule retry attempt
        g_lastCatMProbeMs = millis() + 30000; // Retry in 30 seconds
        delay(800);
    }
    yield();
    delay(200);

    // Initialize PWRCAN (guarded)
#if ENABLE_PWRCAN
    pwrcanModule = new PWRCANModule();
    if (pwrcanModule->begin(PWRCAN_TX_PIN, PWRCAN_RX_PIN, PWRCAN_BITRATE_KBPS)) {
        Serial.println("PWRCAN initialized successfully");
    } else {
        Serial.println("PWRCAN initialization failed");
        delete pwrcanModule;
        pwrcanModule = nullptr;
    }
#endif

#if ENABLE_WEB_SERVER
    // Web task (Core 1)
    xTaskCreatePinnedToCore(
        vTaskWeb,
        "Web",
        TASK_STACK_SIZE,
        NULL,
        TASK_PRIORITY_DATA_TRANSMIT,
        &webTaskHandle,
        1
    );
    Serial.println("Web task created");
    log_add("Web task started");
#endif

    // MQTT task (Core 0) - ENABLED for ThingsBoard integration
    xTaskCreatePinnedToCore(
        vTaskMQTT,
        "MQTT",
        TASK_STACK_SIZE_APP_MQTT,
        NULL,
        TASK_PRIORITY_DATA_TRANSMIT,
        &mqttTaskHandle,
        0
    );
    Serial.println("MQTT task created");
    log_add("MQTT task created");
    
    // Create tasks
    Serial.println("Creating FreeRTOS tasks...");
    
    // Create button task (Core 1)
    xTaskCreatePinnedToCore(
        vTaskButton,
        "Button",
        TASK_STACK_SIZE_BUTTON_HANDLER,
        NULL,
        TASK_PRIORITY_DISPLAY,
        &buttonTaskHandle,
        1
    );
    Serial.println("Button task created");
    
    // Create display task (Core 1)
    xTaskCreatePinnedToCore(
        vTaskDisplay,
        "Display",
        TASK_STACK_SIZE_APP_DISPLAY,
        NULL,
        TASK_PRIORITY_DISPLAY,
        &displayTaskHandle,
        1
    );
    Serial.println("Display task created");
    
    // Create status bar task (Core 1) - shares display pipeline to avoid cross-core conflicts
    drawBootScreen("Creating FreeRTOS tasks", 85);
    xTaskCreatePinnedToCore(
        vTaskStatusBar,
        "StatusBar",
        TASK_STACK_SIZE_SYSTEM_MONITOR,
        NULL,
        TASK_PRIORITY_SYSTEM_MONITOR,
        &statusBarTaskHandle,
        1
    );
    Serial.println("Status bar task created");
    log_add("StatusBar task started");
    delay(200);
    
    // Create StampPLC task
    if (stampPLC) {
        xTaskCreatePinnedToCore(
            vTaskStampPLC,
            "StampPLC",
            TASK_STACK_SIZE,
            NULL,
            TASK_PRIORITY_INDUSTRIAL_IO,
            &stampPLCTaskHandle,
            0
        );
        Serial.println("StampPLC task created");
    }
    
    // Create CatM+GNSS task
    if (catmGnssModule) {
        xTaskCreatePinnedToCore(
            vTaskCatMGNSS,
            "CatMGNSS",
            TASK_STACK_SIZE_APP_GNSS,
            catmGnssModule,
            TASK_PRIORITY_GNSS,
            &catmGnssTaskHandle,
            0
        );
        Serial.println("CatM+GNSS task created");
    }

    // Create PWRCAN task (guarded)
#if ENABLE_PWRCAN
    if (pwrcanModule) {
        xTaskCreatePinnedToCore(
            vTaskPWRCAN,
            "PWRCAN",
            TASK_STACK_SIZE,
            pwrcanModule,
            TASK_PRIORITY_INDUSTRIAL_IO,
            &pwrcanTaskHandle,
            0
        );
        Serial.println("PWRCAN task created");
    }
#endif
    
    // POST complete - show final screen
    drawBootScreen("POST complete - Ready!", 100);
    delay(1000);

    // Do initial RTC sync before clearing screen
    Serial.println("Performing initial RTC synchronization...");
    syncRTCFromAvailableSources();

    struct tm rtcCheck {};
    if (!getRTCTime(rtcCheck) || (rtcCheck.tm_year + 1900) < 2020) {
        Serial.println("RTC still invalid after sync attempts; seeding from firmware build timestamp");
        if (!setRTCFromBuildTimestamp()) {
            Serial.println("WARNING: Failed to seed RTC from build timestamp");
        }
    }

    // Clear screen for normal operation
    M5StamPLC.Display.fillScreen(BLACK);
    // Ensure UI task redraws landing page after POST clears the screen
    enqueueUIEvent(UIEventType::GoLanding);
    pageChanged = true;

    Serial.println("Setup complete");
    log_add("Setup complete");
}

void loop() {
    // Update system LED functionality
    updateSystemLED();

    // Background hot-plug re-probe; also on user-forced retry
    bool forced = g_forceCatMRetry;
    if (forced) g_forceCatMRetry = false;
    tryInitCatMIfAbsent(forced);

    vTaskDelay(pdMS_TO_TICKS(1000));
}



// ============================================================================
// LED CONTROL FUNCTIONALITY
// ============================================================================
void updateSystemLED() {
    static uint32_t lastLedUpdate = 0;
    
    // Update LED every 500ms
    if (millis() - lastLedUpdate < 500) return;
    lastLedUpdate = millis();
    
    // Determine system state
    SystemLEDState newState = SystemLEDState::RUNNING; // Default to running
    
    // Check for faults
    bool hasFault = false;
    if (!catmGnssModule) hasFault = true;
    if (g_modalActive && g_modalType == ModalType::NO_COMM_UNIT) hasFault = true;
    if (catmGnssModule && !catmGnssModule->isModuleInitialized()) hasFault = true;
    if (catmGnssModule && catmGnssModule->isModuleInitialized()) {
        CellularData cellData = catmGnssModule->getCellularData();
        if (!cellData.isConnected) hasFault = true;
    }
    if (stampPLC && !stampPLC->isReady()) hasFault = true;
    
    if (hasFault) {
        newState = SystemLEDState::FAULT;
    } else if (millis() < 30000) { // First 30 seconds = booting
        newState = SystemLEDState::BOOTING;
    }
    
    // Update LED based on state
    if (newState != currentLEDState) {
        currentLEDState = newState;
        Serial.printf("System LED: %s\n", 
            (currentLEDState == SystemLEDState::BOOTING) ? "BOOTING" :
            (currentLEDState == SystemLEDState::RUNNING) ? "RUNNING" : "FAULT");
    }
    
    // Drive LED based on current state
    if (stampPLC && stampPLC->getStamPLC()) {
        switch (currentLEDState) {
            case SystemLEDState::BOOTING:
                // Orange (255, 165, 0)
                stampPLC->getStamPLC()->setStatusLight(255, 165, 0);
                break;
            case SystemLEDState::RUNNING:
                // Green (0, 255, 0)
                stampPLC->getStamPLC()->setStatusLight(0, 255, 0);
                break;
            case SystemLEDState::FAULT:
                // Blinking red
                if (millis() - lastLEDBlink > 500) {
                    ledBlinkState = !ledBlinkState;
                    lastLEDBlink = millis();
                }
                if (ledBlinkState) {
                    stampPLC->getStamPLC()->setStatusLight(255, 0, 0); // Red
                } else {
                    stampPLC->getStamPLC()->setStatusLight(0, 0, 0); // Off
                }
                break;
        }
    }
}

// ============================================================================
// SD CARD FORMAT FUNCTIONALITY
// ============================================================================
void formatSDCard() {
    if (!sdModule || !sdModule->isMounted()) {
        Serial.println("SD card not mounted, cannot format");
        return;
    }
    
    Serial.println("Starting SD card format...");
    
    // Unmount SD card first
    // SdFat doesn't need explicit end() call
    
    // Wait for unmount to complete
    vTaskDelay(pdMS_TO_TICKS(500));
    
    bool formatSuccess = false;
    
    // Use file deletion approach for formatting
    Serial.println("Using file deletion approach for formatting...");

    // Try to remount and delete all files (basic format)
    if (SD.begin()) {
        Serial.println("Erasing all files...");

        // Delete all files and directories
        bool allDeleted = true;
        File root = SD.open("/");
        if (root) {
            File file = root.openNextFile();
            while (file) {
                String fileName = file.name();
                bool isDirectory = file.isDirectory();
                file.close();

                if (isDirectory) {
                    // Recursively delete directory contents
                    String fullPath = "/" + fileName;
                    if (!deleteDirectory(fullPath.c_str(), 0)) {
                        allDeleted = false;
                        Serial.printf("Failed to delete directory: %s\n", fileName.c_str());
                    }
                } else {
                    // Delete file
                    if (!SD.remove("/" + fileName)) {
                        allDeleted = false;
                        Serial.printf("Failed to delete file: %s\n", fileName.c_str());
                    }
                }
                file = root.openNextFile();
            }
            root.close();
        }

        if (allDeleted) {
            formatSuccess = true;
            Serial.println("File deletion completed");
        }
    }
    
    // Create basic directory structure and format marker
    if (formatSuccess && SD.begin()) {
        SD.mkdir("/data");
        SD.mkdir("/logs");
        SD.mkdir("/config");

        // Create a format marker file
        File formatFile = SD.open("/format_marker.txt", FILE_WRITE);
        if (formatFile) {
            formatFile.println("SD card formatted on: ");
            formatFile.printf("Timestamp: %lu ms\n", millis());
            formatFile.printf("Method: SDFat library\n");
            formatFile.close();
            Serial.println("Format marker created");
        }
        SD.end();
    }
    
    // Reinitialize SD module
    if (sdModule) {
        delete sdModule;
        sdModule = nullptr;
    }
    
#if ENABLE_SD
    sdModule = new SDCardModule();
    if (sdModule->begin()) {
        Serial.println("SD card remounted after format");
        if (formatSuccess) {
            log_add("SD card formatted successfully");
        } else {
            log_add("SD card format completed with warnings");
        }
    } else {
        Serial.println("Failed to remount SD card after format");
        delete sdModule;
        sdModule = nullptr;
        log_add("SD card format failed - remount failed");
    }
#endif
}

// Helper function to recursively delete directories (with depth limit for stack safety)
bool deleteDirectory(const char* path, int depth = 0) {
    const int MAX_DEPTH = 10; // Prevent stack overflow on deep directory trees
    
    if (depth > MAX_DEPTH) {
        Serial.printf("deleteDirectory: Maximum depth (%d) exceeded at %s\n", MAX_DEPTH, path);
        return false;
    }
    
    File dir = SD.open(path);
    if (!dir) return false;

    bool success = true;
    File file = dir.openNextFile();
    while (file && success) {
        char fileName[64];
        snprintf(fileName, sizeof(fileName), "/%s", file.name());
        char fullPath[128];
        snprintf(fullPath, sizeof(fullPath), "%s%s", path, fileName);
        if (file.isDirectory()) {
            success = deleteDirectory(fullPath, depth + 1);
        } else {
            success = SD.remove(fullPath);
        }
        file.close();
        file = dir.openNextFile();
    }
    dir.close();

    // Remove the directory itself if it's empty
    if (success) {
        success = SD.rmdir(path);
    }

    return success;
}


