#include <Arduino.h>
#include <M5Unified.h>
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
#include <SD.h>
#include <FS.h>
#include <SdFat.h>
#include "../include/debug_system.h"

// Include our modules
#include "hardware/basic_stamplc.h"
#include "modules/catm_gnss/catm_gnss_module.h"
#include "config/system_config.h"
#include "modules/pwrcan/pwrcan_module.h"
#include "modules/storage/sd_card_module.h"
#include "modules/storage/storage_task.h"
#include "modules/mqtt/mqtt_task.h"
#include "ui/theme.h"
#include "ui/components.h"
#include "ui/fonts.h"

// Legacy UI runs directly on M5GFX framebuffer (LVGL removed)
extern "C" void vTaskPWRCAN(void* pvParameters);

#ifndef USE_CARD_UI
#define USE_CARD_UI 0
#endif

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
TaskHandle_t blinkTaskHandle = nullptr;
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
static lgfx::LGFX_Sprite statusSprite(&M5.Display);
static bool statusSpriteInit = false;
static bool lastGnssOk = false;
static bool lastCellOk = false;
static uint32_t lastStatusUpdate = 0;
static uint8_t lastCellularSignal = 0;
static uint8_t lastGnssSatellites = 0;
static String lastTimeStr = "";

// Content sprite for card-based pages
static lgfx::LGFX_Sprite contentSprite(&M5.Display);
static bool contentSpriteInit = false;

// Icon sprites for UI elements
static lgfx::LGFX_Sprite iconSatellite(&M5.Display);
static lgfx::LGFX_Sprite iconGear(&M5.Display);
static lgfx::LGFX_Sprite iconTower(&M5.Display);
static lgfx::LGFX_Sprite iconWrench(&M5.Display);
static lgfx::LGFX_Sprite iconGPS(&M5.Display);
static lgfx::LGFX_Sprite iconCellular(&M5.Display);
static lgfx::LGFX_Sprite iconLog(&M5.Display);
// Larger icons for launcher card
static lgfx::LGFX_Sprite iconSatelliteBig(&M5.Display);
static lgfx::LGFX_Sprite iconGearBig(&M5.Display);
static lgfx::LGFX_Sprite iconTowerBig(&M5.Display);
static lgfx::LGFX_Sprite iconWrenchBig(&M5.Display);
static lgfx::LGFX_Sprite iconGPSBig(&M5.Display);
static lgfx::LGFX_Sprite iconCellularBig(&M5.Display);
static lgfx::LGFX_Sprite iconLogBig(&M5.Display);
static bool iconsInitialized = false;
// Simple page transition tracking: -1 = prev (slide right), 1 = next (slide left), 0 = none
static int8_t g_lastNavDir = 0;
// NTP configuration tracking
static bool g_ntpConfigured = false;
static uint32_t g_lastTZUpdateMs = 0;

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

static void ensureNtpConfigured() {
    if (g_ntpConfigured) return;
    // Configure NTP only when STA is connected
    if ((WiFi.getMode() & WIFI_MODE_STA) && WiFi.status() == WL_CONNECTED) {
        // Timezone: Eastern with DST auto rules (EST/EDT). Adjust as needed.
        configTzTime("EST5EDT,M3.2.0/2,M11.1.0/2", "pool.ntp.org", "time.nist.gov", "time.google.com");
        // SNTP refresh interval remains default (typically 1 hour). We can tune later if needed.
        g_ntpConfigured = true;
    }
}

static void maybeUpdateTimeZoneFromCellular() {
    // Update timezone from cellular offset if available, every 10 minutes
    if (millis() - g_lastTZUpdateMs < 10UL * 60UL * 1000UL) return;
    if (!catmGnssModule || !catmGnssModule->isModuleInitialized()) return;
    CellularData cd = catmGnssModule->getCellularData();
    if (!cd.isConnected) return;
    int q = 0;
    if (catmGnssModule->getNetworkTimeZoneQuarters(q)) {
        int minutes = q * 15; // minutes offset from UTC
        int hours = minutes / 60; int mins = abs(minutes % 60);
        // POSIX TZ sign is reversed relative to human-readable UTC+X
        char tzbuf[32];
        if (minutes >= 0)
            snprintf(tzbuf, sizeof(tzbuf), "UTC-%d:%02d", hours, mins);
        else
            snprintf(tzbuf, sizeof(tzbuf), "UTC+%d:%02d", -hours, mins);
        setenv("TZ", tzbuf, 1);
        tzset();
        g_lastTZUpdateMs = millis();
    }
}

// Convert a UTC struct tm to local time strings using current TZ rules, without changing global time permanently
static void formatLocalFromUTC(const struct tm& utcIn, String& timeStr, String& dateStr) {
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
    timeStr = tbuf; dateStr = dbuf;
}

#if USE_CARD_UI
// Launcher state
static volatile int launcherIndex = 0;
static const char* launcherNames[] = {"GPS", "4G Cellular", "System Info", "Settings", "Logs"};
static const int launcherCount = 5;
static int launcherScrollPx = 0;
static const int LAUNCHER_PAD = 6;
static const int LAUNCHER_CARD_H = 38;
#endif

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
    NORMAL,             // Normal settings view
    FORMAT_CONFIRM      // Format confirmation dialog
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
static void drawFormatConfirmDialog();
bool deleteDirectory(String path);

// ============================================================================
// TASK DECLARATIONS
// ============================================================================
void vTaskBlink(void* pvParameters);
void vTaskButton(void* pvParameters);
void vTaskDisplay(void* pvParameters);
void vTaskStatusBar(void* pvParameters);
void vTaskStampPLC(void* pvParameters);
void vTaskCatMGNSS(void* pvParameters);

// ============================================================================
// DISPLAY FUNCTIONS
// ============================================================================
void drawLandingPage();
void drawGNSSPage();
void drawCellularPage();
void drawSystemPage();
void drawSettingsPage();
void drawLogsPage();
void drawSystemPageCards();
void drawGNSSPageCards();
void drawCellularPageCards();
void drawSettingsPageCards();
void drawLogsPageCards();
void drawCardBox(lgfx::LGFX_Sprite* s, int x, int y, int w, int h, const char* title, lgfx::LGFX_Sprite* icon = nullptr);
void setCustomFont(lgfx::LGFX_Sprite* s, int size);
bool isDST(uint8_t month, uint8_t day, uint8_t hour, uint8_t dayOfWeek);
bool setRTCFromGPS(const GNSSData& gnssData);
bool getRTCTime(struct tm& timeinfo);
void drawStatusBar();
void drawSignalBar(int16_t x, int16_t y, int16_t w, int16_t h, int8_t signalDbm);
void drawWiFiBar(int16_t x, int16_t y, int16_t w, int16_t h);
void drawButtonIndicators();
void drawSDInfo(int x, int y);
void updateSystemLED();
void generateQRCode();

// Push the content sprite with a simple slide animation between pages (card UI only)
static void pushContentAnimated(int8_t dir) {
#if USE_CARD_UI
    const int w = M5.Display.width();
    const int h = CONTENT_BOTTOM - CONTENT_TOP;
    auto clearArea = [&](){ M5.Display.fillRect(0, CONTENT_TOP, w, h, BLACK); };

    if (dir == 0) {
        clearArea();
        contentSprite.pushSprite(0, CONTENT_TOP);
        return;
    }
    int start = (dir > 0) ? w : -w;
    int step  = (dir > 0) ? -(w/8) : (w/8);
    if (step == 0) step = (dir > 0) ? -20 : 20;
    for (int x = start; (dir > 0) ? (x > 0) : (x < 0); x += step) {
        clearArea();
        contentSprite.pushSprite(x, CONTENT_TOP);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    clearArea();
    contentSprite.pushSprite(0, CONTENT_TOP);
#else
    (void)dir;
#endif
}

#if USE_CARD_UI
void drawSystemPageCards() {
    const int w = M5.Display.width();
    const int h = CONTENT_BOTTOM - CONTENT_TOP;
    if (!contentSpriteInit) {
        contentSprite.setColorDepth(16);
        contentSprite.createSprite(w, h);
        contentSpriteInit = true;
        ui::fonts::applyToSprite(&contentSprite);
    }

    auto* s = &contentSprite;
    s->fillRect(0, 0, w, h, BLACK);

    const int pad = 8;
    const int cardW = w - pad * 2;
    int y = pad - scrollSYS;

    // Memory card
    int cardH = 42;
    ui::drawCard(s, pad, y, cardW, cardH, "Memory", &iconGear);
    s->setTextSize(1);
    char kv[32];
    snprintf(kv, sizeof(kv), "%d KB", ESP.getFreeHeap() / 1024);
    ui::drawKV(s, pad + 8, y + 18, "Free:", kv);
    snprintf(kv, sizeof(kv), "%d KB", ESP.getHeapSize() / 1024);
    ui::drawKV(s, pad + 8, y + 30, "Total:", kv);
    y += cardH + pad;

    // WiFi card
    cardH = 42;
    ui::drawCard(s, pad, y, cardW, cardH, "WiFi", &iconTower);
    const auto& th = ui::theme();
    int8_t rssi = (WiFi.getMode() & WIFI_MODE_STA) && WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : -120;
    int16_t pct = constrain(map(rssi, -100, -30, 0, 100), 0, 100);
    uint16_t col = (pct > 70) ? th.green : (pct > 40) ? th.yellow : th.red;
    const char* modeStr = (WiFi.status() == WL_CONNECTED) ? "STA" : ((WiFi.getMode() & WIFI_MODE_AP) ? "AP" : "OFF");
    ui::drawBar(s, pad + 8, y + 24, cardW - 80, 10, pct, col);
    s->setTextSize(1);
    s->setTextColor(th.text);
    s->setCursor(pad + cardW - 68, y + 22);
    s->printf("%s %ddBm", modeStr, rssi);
    y += cardH + pad;

    // Modules card
    cardH = 52;
    ui::drawCard(s, pad, y, cardW, cardH, "Modules", &iconWrench);
    bool plcOk = stampPLC && stampPLC->isReady();
    bool catmOk = catmGnssModule && catmGnssModule->isModuleInitialized();
    bool sdOk  = sdModule && sdModule->isMounted();
    ui::drawKV(s, pad + 8, y + 18, "StampPLC:", plcOk ? "OK" : "ERR", plcOk ? th.green : th.red);
    ui::drawKV(s, pad + 8, y + 30, "CatM+GNSS:", catmOk ? "OK" : "ERR", catmOk ? th.green : th.red);
    ui::drawKV(s, pad + 8, y + 42, "SD:", sdOk ? "OK" : "ERR", sdOk ? th.green : th.red);
    y += cardH + pad;

    // Do not push here; vTaskDisplay handles pushing (with animation)
}
#endif

#if USE_CARD_UI
void drawLogsPageCards() {
    const int w = M5.Display.width();
    const int h = CONTENT_BOTTOM - CONTENT_TOP;
    if (!contentSpriteInit) {
        contentSprite.setColorDepth(16);
        contentSprite.createSprite(w, h);
        contentSpriteInit = true;
        ui::fonts::applyToSprite(&contentSprite);
    }
    auto* s = &contentSprite;
    s->fillRect(0, 0, w, h, BLACK);

    const int pad = 8;
    const int cardW = w - pad * 2;
    const int cardH = h - 2 * pad;
    // Always draw the Logs card at a fixed position
    ui::drawCard(s, pad, pad, cardW, cardH, "Logs", &iconLog);

    // Compute inner scrolling area (leave space for the header inside the card)
    const int innerX = pad + 8;
    const int innerY = pad + 24; // header height inside card
    const int innerH = cardH - 24 - 8; // header + bottom padding
    const int innerW = cardW - 16; // left/right padding inside card

    // Build lines and scroll them inside the inner area
    size_t cnt = log_count();
    int contentH = (int)cnt * LINE_H1; // only lines height
    // clamp was done in event handler; but we defend here as well
    if (contentH < innerH) contentH = innerH;
    scrollLOGS = clampScroll(scrollLOGS, contentH);

    s->setTextSize(1);
    s->setTextWrap(false);
    s->setTextColor(ui::theme().text);
    char line[160];
    int lineY = innerY - scrollLOGS;
    for (size_t i = 0; i < cnt; ++i) {
        if (log_get_line(i, line, sizeof(line))) {
            if ((lineY + LINE_H1) >= innerY && lineY < innerY + innerH) {
                s->setCursor(innerX, lineY);
                // Truncate to fit inner width with ellipsis
                String lstr(line);
                int tw = s->textWidth(lstr);
                if (tw > innerW) {
                    const char* ell = "...";
                    int ellW = s->textWidth(ell);
                    int maxW = innerW - ellW;
                    int lo = 0, hi = (int)lstr.length();
                    while (lo < hi) {
                        int mid = (lo + hi + 1) / 2;
                        String sub = lstr.substring(0, mid);
                        int w = s->textWidth(sub);
                        if (w <= maxW) lo = mid; else hi = mid - 1;
                    }
                    lstr = lstr.substring(0, lo) + ell;
                }
                s->print(lstr);
            }
            lineY += LINE_H1;
        }
    }
    s->pushSprite(0, CONTENT_TOP);
}
#endif

#if USE_CARD_UI
void drawGNSSPageCards() {
    const int w = M5.Display.width();
    const int h = CONTENT_BOTTOM - CONTENT_TOP;
    if (!contentSpriteInit) {
        contentSprite.setColorDepth(16);
        contentSprite.createSprite(w, h);
        contentSpriteInit = true;
        ui::fonts::applyToSprite(&contentSprite);
    }
    auto* s = &contentSprite;
    s->fillRect(0, 0, w, h, BLACK);

    const int pad = 8;
    const int cardW = w - pad * 2;
    int y = pad - scrollGNSS;

    // Guard when module is absent
    if (!catmGnssModule || !catmGnssModule->isModuleInitialized()) {
        const int pad = 8;
        const int cardW = w - pad * 2;
        int y = pad - scrollGNSS;
        int cardH = 40;
        ui::drawCard(s, pad, y, cardW, cardH, "GNSS", &iconSatellite);
        s->setTextSize(1);
        s->setTextColor(RED);
        s->setCursor(pad + 8, y + 20);
        s->print("Module not present");
        s->pushSprite(0, CONTENT_TOP);
        return;
    }

    GNSSData data = catmGnssModule->getGNSSData();

    // Position card
    int cardH = 62;
    ui::drawCard(s, pad, y, cardW, cardH, "Position", &iconSatellite);
    char buf[32];
    s->setTextSize(1);
    snprintf(buf, sizeof(buf), "%.4f", data.latitude);
    ui::drawKV(s, pad + 8, y + 18, "Lat:", buf);
    snprintf(buf, sizeof(buf), "%.4f", data.longitude);
    ui::drawKV(s, pad + 8, y + 30, "Lon:", buf);
    snprintf(buf, sizeof(buf), "%.1fm", data.altitude);
    ui::drawKV(s, pad + 8, y + 42, "Alt:", buf);
    snprintf(buf, sizeof(buf), "%d", (int)data.satellites);
    ui::drawKV(s, pad + 8, y + 54, "Sats:", buf);
    y += cardH + pad;

    // Time card
    cardH = 42;
    ui::drawCard(s, pad, y, cardW, cardH, "Time", &iconGPS);
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", data.hour, data.minute, data.second);
    ui::drawKV(s, pad + 8, y + 18, "UTC:", buf);
    snprintf(buf, sizeof(buf), "%d/%d/%d", (int)data.month, (int)data.day, (int)data.year);
    ui::drawKV(s, pad + 8, y + 30, "Date:", buf);
    y += cardH + pad;

    s->pushSprite(0, CONTENT_TOP);
}

void drawCellularPageCards() {
    const int w = M5.Display.width();
    const int h = CONTENT_BOTTOM - CONTENT_TOP;
    if (!contentSpriteInit) {
        contentSprite.setColorDepth(16);
        if (!contentSprite.createSprite(w, h)) {
            Serial.println("FATAL: Failed to create contentSprite");
            log_add("FATAL: contentSprite create failed");
        } else {
            contentSpriteInit = true;
            ui::fonts::applyToSprite(&contentSprite);
        }
    }
    auto* s = &contentSprite;
    s->fillRect(0, 0, w, h, BLACK);

    const int pad = 8;
    const int cardW = w - pad * 2;
    int y = pad - scrollCELL;

    if (!catmGnssModule || !catmGnssModule->isModuleInitialized()) {
        int cardH = 40;
        ui::drawCard(s, pad, y, cardW, cardH, "Cellular", &iconCellular);
        s->setTextSize(1);
        s->setTextColor(RED);
        s->setCursor(pad + 8, y + 20);
        s->print("Module not present");
        s->pushSprite(0, CONTENT_TOP);
        return;
    }

    CellularData raw = catmGnssModule->getCellularData();
    CellStatus status = catmGnssModule->getCellStatus();
    const auto& th = ui::theme();

    auto drawSignalBars = [&](int x, int y, int width, int height, uint16_t pct) {
        const int bars = 5;
        const int gap = 3;
        int barWidth = (width - (bars - 1) * gap) / bars;
        if (barWidth < 3) barWidth = 3;
        for (int i = 0; i < bars; ++i) {
            float threshold = (i + 1) * 20.0f;
            bool active = pct >= threshold - 10;
            int barHeight = height * (i + 1) / bars;
            int bx = x + i * (barWidth + gap);
            int by = y + (height - barHeight);
            uint16_t fill = active ? ((pct >= 70) ? th.green : (pct >= 40 ? th.yellow : th.red)) : th.cardBg;
            uint16_t outline = th.cardOutline;
            s->fillRect(bx, by, barWidth, barHeight, fill);
            s->drawRect(bx, by, barWidth, barHeight, outline);
        }
    };

    uint32_t ageMs = raw.lastUpdate ? (millis() - raw.lastUpdate) : 0;
    char ageBuf[24];
    if (raw.lastUpdate) {
        if (ageMs < 1000) {
            snprintf(ageBuf, sizeof(ageBuf), "%lu ms", static_cast<unsigned long>(ageMs));
        } else {
            snprintf(ageBuf, sizeof(ageBuf), "%.1f s", ageMs / 1000.0f);
        }
    } else {
        strcpy(ageBuf, "--");
    }

    int cardH = 72;
    ui::drawCard(s, pad, y, cardW, cardH, "Connection", &iconCellular);
    s->setTextSize(1);
    ui::drawKV(s, pad + 8, y + 20, "Status:", status.statusText.c_str(), status.isConnected ? th.green : th.red);
    ui::drawKV(s, pad + 8, y + 32, "Registration:", status.registrationText.c_str(), status.isRegistered ? th.green : th.text);
    ui::drawKV(s, pad + 8, y + 44, "APN:", status.apn.length() ? status.apn.c_str() : raw.apn.c_str());
    ui::drawKV(s, pad + 8, y + 56, "IP:", raw.hasIpAddress ? raw.ipAddress.c_str() : "--");
    s->setCursor(pad + cardW - 80, y + 56);
    s->setTextColor(th.muted);
    s->printf("age %s", ageBuf);
    y += cardH + pad;

    cardH = 82;
    ui::drawCard(s, pad, y, cardW, cardH, "Signal", &iconCellular);
    s->setTextSize(2);
    s->setTextColor(th.text);
    s->setCursor(pad + 8, y + 24);
    if (status.operatorName.length()) {
        s->print(status.operatorName);
    } else {
        s->print("Unknown operator");
    }
    s->setTextSize(1);
    s->setCursor(pad + 8, y + 46);
    s->printf("RSSI: %s", status.rssiText.c_str());
    s->setCursor(pad + 8, y + 58);
    s->printf("Quality: %s", status.signalText.c_str());
    drawSignalBars(pad + cardW - 80, y + 26, 72, 44, status.signalPercentage);
    y += cardH + pad;

    cardH = 74;
    ui::drawCard(s, pad, y, cardW, cardH, "Modem", &iconTower);
    ui::drawKV(s, pad + 8, y + 20, "IMEI:", raw.imei.length() ? raw.imei.c_str() : status.imei.c_str());
    ui::drawKV(s, pad + 8, y + 32, "Errors:", String(raw.errorCount).c_str());
    ui::drawKV(s, pad + 8, y + 44, "Last CEER:", raw.lastDetachReason.length() ? raw.lastDetachReason.c_str() : "--");
    y += cardH + pad;

    s->pushSprite(0, CONTENT_TOP);
}


void drawSettingsPageCards() {
    const int w = M5.Display.width();
    const int h = CONTENT_BOTTOM - CONTENT_TOP;
    if (!contentSpriteInit) {
        contentSprite.setColorDepth(16);
        contentSprite.createSprite(w, h);
        contentSpriteInit = true;
        ui::fonts::applyToSprite(&contentSprite);
    }
    auto* s = &contentSprite;
    s->fillRect(0, 0, w, h, BLACK);

    const int pad = 8;
    const int cardW = w - pad * 2;
    int y = pad - scrollSETTINGS;

    // SD Card Format card
    int cardH = 50;
    ui::drawCard(s, pad, y, cardW, cardH, "SD Card", &iconGear);
    
    // Add SD card info
    s->setTextSize(1);
    s->setTextColor(ui::theme().text);
    s->setCursor(pad + 8, y + 25);
    if (sdModule && sdModule->isMounted()) {
        s->print("Mounted - Press C to format");
    } else {
        s->print("Not mounted");
    }

    s->pushSprite(0, CONTENT_TOP);
}
#endif

// ============================================================================
// SIGNAL BAR HELPERS
// ============================================================================
String getNetworkType(int8_t signalStrength) {
    // Network type detection based on signal strength
    if (signalStrength > -70) return "4G";      // Strong signal = likely 4G
    else if (signalStrength > -90) return "3G"; // Medium signal = could be 3G
    else return "2G";                           // Weak signal = likely 2G/GPRS
}

void drawCompactSignalBar(lgfx::LGFX_Sprite* sprite, int x, int y, int percent, uint16_t color) {
    const int barW = 8;
    const int barH = 4;
    
    // Background
    sprite->fillRect(x, y, barW, barH, 0x2104);
    sprite->drawRect(x, y, barW, barH, WHITE);
    
    // Signal level
    int fillWidth = (barW - 2) * percent / 100;
    if (fillWidth > 0) {
        sprite->fillRect(x + 1, y + 1, fillWidth, barH - 2, color);
    }
}

// ============================================================================
// ICON CREATION FUNCTIONS
// ============================================================================
static void drawSatelliteIconTo(lgfx::LGFX_Sprite* spr, int sz) {
    spr->setColorDepth(16);
    spr->createSprite(sz, sz);
    spr->fillRect(0, 0, sz, sz, ui::theme().bg);
    auto S = [&](int v){ return (v * sz) / 24; };
    // Body
    spr->fillRect(S(10), S(10), S(4), S(4), WHITE);
    // Panels
    spr->fillRect(S(5), S(10), S(5), S(4), WHITE);
    spr->fillRect(S(14), S(10), S(5), S(4), WHITE);
    // Antenna
    spr->drawLine(S(12), S(4), S(12), S(10), WHITE);
    spr->drawLine(S(12), S(14), S(12), S(20), WHITE);
}
void createSatelliteIcon() { drawSatelliteIconTo(&iconSatellite, 24); }
static void createSatelliteIconBig(int sz) { drawSatelliteIconTo(&iconSatelliteBig, sz); }
static void drawGearIconTo(lgfx::LGFX_Sprite* spr, int sz) {
    spr->setColorDepth(16);
    spr->createSprite(sz, sz);
    spr->fillRect(0, 0, sz, sz, ui::theme().bg);
    auto S = [&](int v){ return (v * sz) / 24; };
    // Teeth
    spr->fillRect(S(11), S(1), S(2), S(3), WHITE);
    spr->fillRect(S(11), S(20), S(2), S(3), WHITE);
    spr->fillRect(S(1), S(11), S(3), S(2), WHITE);
    spr->fillRect(S(20), S(11), S(3), S(2), WHITE);
    // Center
    spr->fillRoundRect(S(7), S(7), S(10), S(10), S(3), WHITE);
}
void createGearIcon() { drawGearIconTo(&iconGear, 24); }
static void createGearIconBig(int sz) { drawGearIconTo(&iconGearBig, sz); }

static void drawTowerIconTo(lgfx::LGFX_Sprite* spr, int sz) {
    spr->setColorDepth(16);
    spr->createSprite(sz, sz);
    spr->fillRect(0, 0, sz, sz, ui::theme().bg);
    auto S = [&](int v){ return (v * sz) / 24; };
    // Base
    spr->fillRect(S(11), S(12), S(2), S(10), WHITE);
    // Top head
    spr->fillRoundRect(S(8), S(8), S(8), S(6), S(2), WHITE);
    // Antenna
    spr->drawLine(S(12), S(4), S(12), S(8), WHITE);
}
void createTowerIcon() { drawTowerIconTo(&iconTower, 24); }
static void createTowerIconBig(int sz) { drawTowerIconTo(&iconTowerBig, sz); }

static void drawWrenchIconTo(lgfx::LGFX_Sprite* spr, int sz) {
    spr->setColorDepth(16);
    spr->createSprite(sz, sz);
    spr->fillRect(0, 0, sz, sz, ui::theme().bg);
    auto S = [&](int v){ return (v * sz) / 24; };
    // Handle
    spr->fillRect(S(4), S(10), S(14), S(4), WHITE);
    // Head
    spr->fillRoundRect(S(16), S(7), S(6), S(10), S(2), WHITE);
    // Grip
    spr->fillRect(S(8), S(9), S(3), S(6), WHITE);
}
void createWrenchIcon() { drawWrenchIconTo(&iconWrench, 24); }
static void createWrenchIconBig(int sz) { drawWrenchIconTo(&iconWrenchBig, sz); }

static void drawGPSIconTo(lgfx::LGFX_Sprite* spr, int sz) {
    spr->setColorDepth(16);
    spr->createSprite(sz, sz);
    spr->fillRect(0, 0, sz, sz, ui::theme().bg);
    auto S = [&](int v){ return (v * sz) / 24; };
    // Dish
    spr->drawCircle(S(12), S(12), S(8), WHITE);
    spr->fillCircle(S(12), S(12), S(4), WHITE);
    // Signal lines
    spr->drawLine(S(3), S(12), S(6), S(12), WHITE);
    spr->drawLine(S(21), S(12), S(18), S(12), WHITE);
}
void createGPSIcon() { drawGPSIconTo(&iconGPS, 24); }
static void createGPSIconBig(int sz) { drawGPSIconTo(&iconGPSBig, sz); }

static void drawCellularIconTo(lgfx::LGFX_Sprite* spr, int sz) {
    spr->setColorDepth(16);
    spr->createSprite(sz, sz);
    spr->fillRect(0, 0, sz, sz, ui::theme().bg);
    auto S = [&](int v){ return (v * sz) / 24; };
    // Bars
    spr->fillRect(S(4), S(16), S(3), S(6), WHITE);
    spr->fillRect(S(9), S(12), S(3), S(10), WHITE);
    spr->fillRect(S(14), S(8), S(3), S(14), WHITE);
    spr->fillRect(S(19), S(4), S(3), S(18), WHITE);
}
void createCellularIcon() { drawCellularIconTo(&iconCellular, 24); }
static void createCellularIconBig(int sz) { drawCellularIconTo(&iconCellularBig, sz); }

static void drawLogIconTo(lgfx::LGFX_Sprite* spr, int sz) {
    spr->setColorDepth(16);
    spr->createSprite(sz, sz);
    spr->fillRect(0, 0, sz, sz, ui::theme().bg);
    auto S = [&](int v){ return (v * sz) / 24; };
    // Simple page icon
    spr->drawRect(S(5), S(5), S(14), S(14), WHITE);
    spr->drawFastHLine(S(7), S(11), S(10), WHITE);
    spr->drawFastHLine(S(7), S(15), S(10), WHITE);
}
void createLogIcon() { drawLogIconTo(&iconLog, 24); }
static void createLogIconBig(int sz) { drawLogIconTo(&iconLogBig, sz); }

void initializeIcons() {
    if (!iconsInitialized) {
        createSatelliteIcon();
        createGearIcon();
        createTowerIcon();
        createWrenchIcon();
        createGPSIcon();
        createCellularIcon();
        createLogIcon();
        // Create larger variants for launcher
        int big = 48; // fits within typical card height
        createSatelliteIconBig(big);
        createGearIconBig(big);
        createTowerIconBig(big);
        createWrenchIconBig(big);
        createGPSIconBig(big);
        createCellularIconBig(big);
        createLogIconBig(big);
        iconsInitialized = true;
    }
}

// ============================================================================
// TASK IMPLEMENTATIONS
// ============================================================================
void vTaskBlink(void* pvParameters) {
    // StamPLC RGB LED is controlled by settings, not this task
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void vTaskButton(void* pvParameters) {
    DEBUG_LOG_TASK_START("Button");

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
        vTaskDelay(pdMS_TO_TICKS(20));
        continue; // Skip old button handling when LVGL is enabled
        #endif

        if (!stampPLC) {
            vTaskDelay(pdMS_TO_TICKS(20));
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
                if (g_uiQueue) { UIEvent e{UIEventType::Redraw}; xQueueSend(g_uiQueue, &e, 0); }
            }
            if (C.wasPressed()) {
                // Request immediate retry; leave modal visible
                g_forceCatMRetry = true;
                g_retryToastActive = true;
                g_retryToastUntilMs = millis() + 1500; // show for 1.5s
            }
            vTaskDelay(pdMS_TO_TICKS(20));
            continue; // Don't process normal navigation while modal active
        }

        if (onLanding) {
#if USE_CARD_UI
            // Card launcher: B=Next, C=Prev, A=Open
            if (B.wasPressed()) {
                if (g_uiQueue) { UIEvent e{UIEventType::LauncherNext}; xQueueSend(g_uiQueue, &e, 0); }
                DEBUG_LOG_BUTTON_PRESS("B", "LauncherNext");
            }
            if (C.wasPressed()) {
                if (g_uiQueue) { UIEvent e{UIEventType::LauncherPrev}; xQueueSend(g_uiQueue, &e, 0); }
                DEBUG_LOG_BUTTON_PRESS("C", "LauncherPrev");
            }
            if (A.wasPressed()) {
                DEBUG_LOG_BUTTON_PRESS("A", "LauncherOpen");
                if (g_uiQueue) { UIEvent e{UIEventType::LauncherOpen}; xQueueSend(g_uiQueue, &e, 0); }
            }
#else
            // Legacy: long-press to enter pages
            if (A.pressedFor(LONG_MS) && !aHandled) {
                if (g_uiQueue) { UIEvent e{UIEventType::GoGNSS}; xQueueSend(g_uiQueue, &e, 0); }
                aHandled = true;
                DEBUG_LOG_BUTTON_PRESS("A", "GoGNSS (long press)");
            } else if (!A.isPressed() && currentPage != DisplayPage::SETTINGS_PAGE) { aHandled = false; }
            if (B.pressedFor(LONG_MS) && !bHandled) {
                if (g_uiQueue) { UIEvent e{UIEventType::GoCELL}; xQueueSend(g_uiQueue, &e, 0); }
                bHandled = true;
                DEBUG_LOG_BUTTON_PRESS("B", "GoCELL (long press)");
            } else if (!B.isPressed() && currentPage != DisplayPage::SETTINGS_PAGE) { bHandled = false; }
            if (C.pressedFor(LONG_MS) && !cHandled) {
                if (g_uiQueue) { UIEvent e{UIEventType::GoSYS}; xQueueSend(g_uiQueue, &e, 0); }
                cHandled = true;
                DEBUG_LOG_BUTTON_PRESS("C", "GoSYS (long press)");
            } else if (!C.isPressed() && currentPage != DisplayPage::SETTINGS_PAGE) { cHandled = false; }
#endif
        } else {
            // In-page: A=HOME (back). B/C short=Scroll Up/Down. B/C long=Prev/Next page.
            if (A.wasPressed()) {
                if (g_uiQueue) { UIEvent e{UIEventType::GoLanding}; xQueueSend(g_uiQueue, &e, 0); }
            }

            // B button: long press = prev page, short press = scroll up
            if (B.pressedFor(LONG_MS) && !bHandled) {
                if (g_uiQueue) { UIEvent e{UIEventType::PrevPage}; xQueueSend(g_uiQueue, &e, 0); }
                bHandled = true;
            } else if (!B.isPressed()) {
                bHandled = false;
            } else if (B.wasPressed()) {
                // Special handling for settings page format dialog
                if (currentPage == DisplayPage::SETTINGS_PAGE && settingsState == SettingsState::FORMAT_CONFIRM) {
                    // Confirm format
                    formatSDCard();
                    settingsState = SettingsState::NORMAL;
                    if (g_uiQueue) { UIEvent e{UIEventType::Redraw}; xQueueSend(g_uiQueue, &e, 0); }
                } else {
                    if (g_uiQueue) { UIEvent e{UIEventType::ScrollUp}; xQueueSend(g_uiQueue, &e, 0); }
                }
            }

            // C button: long press = next page, short press = scroll down
            if (C.pressedFor(LONG_MS) && !cHandled) {
                if (g_uiQueue) { UIEvent e{UIEventType::NextPage}; xQueueSend(g_uiQueue, &e, 0); }
                cHandled = true;
            } else if (!C.isPressed()) {
                cHandled = false;
            } else if (C.wasPressed()) {
                // Special handling for settings page
                if (currentPage == DisplayPage::SETTINGS_PAGE) {
                    if (settingsState == SettingsState::NORMAL) {
                        // Show format confirmation dialog
                        settingsState = SettingsState::FORMAT_CONFIRM;
                        if (g_uiQueue) { UIEvent e{UIEventType::Redraw}; xQueueSend(g_uiQueue, &e, 0); }
                    } else if (settingsState == SettingsState::FORMAT_CONFIRM) {
                        // Cancel format
                        settingsState = SettingsState::NORMAL;
                        if (g_uiQueue) { UIEvent e{UIEventType::Redraw}; xQueueSend(g_uiQueue, &e, 0); }
                    }
                } else {
                    if (g_uiQueue) { UIEvent e{UIEventType::ScrollDown}; xQueueSend(g_uiQueue, &e, 0); }
                }
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

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void vTaskStampPLC(void* pvParameters) {
    if (!stampPLC) {
        Serial.println("StampPLC task error: stampPLC is null");
        vTaskDelete(NULL);
        return;
    }
    
    for (;;) {
        // Update StampPLC
        stampPLC->update();
        
        // Print status every 5 seconds
        static uint32_t lastStatusTime = 0;
        if (millis() - lastStatusTime > 5000) {
            stampPLC->printStatus();
            lastStatusTime = millis();
        }
        
        vTaskDelay(pdMS_TO_TICKS(100));
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
                M5.Display.sleep();
                M5.Display.setBrightness(0);
                
                // Try direct GPIO control for backlight (common backlight pins)
                // GPIO 21 is commonly used for backlight on ESP32-S3
                pinMode(21, OUTPUT);
                digitalWrite(21, LOW);
                
                Serial.println("Display going to sleep (backlight off)");
            } else if (!shouldSleep && displayAsleep) {
                // Wake display up and restore backlight
                displayAsleep = false;
                
                // Restore backlight and wake display
                pinMode(21, OUTPUT);
                digitalWrite(21, HIGH);
                
                M5.Display.wakeup();
                M5.Display.setBrightness(displayBrightness);
                
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
                            case DisplayPage::CELLULAR_PAGE: scrollCELL = clampScroll(scrollCELL - SCROLL_STEP, 100); break;
                            case DisplayPage::SYSTEM_PAGE: scrollSYS  = clampScroll(scrollSYS  - SCROLL_STEP, 160); break;
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
                            case DisplayPage::CELLULAR_PAGE: scrollCELL = clampScroll(scrollCELL + SCROLL_STEP, 100); break;
                            case DisplayPage::SYSTEM_PAGE: scrollSYS  = clampScroll(scrollSYS  + SCROLL_STEP, 160); break;
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
#if USE_CARD_UI
                        launcherIndex = (launcherIndex + 1) % launcherCount;
#endif
                        pageChanged = true;
                        g_lastNavDir = 1;
                        break;
                    case UIEventType::LauncherPrev:
#if USE_CARD_UI
                        launcherIndex = (launcherIndex - 1 + launcherCount) % launcherCount;
#endif
                        pageChanged = true;
                        g_lastNavDir = -1;
                        break;
                    case UIEventType::LauncherOpen:
#if USE_CARD_UI
                        switch (launcherIndex) {
                            case 0: currentPage = DisplayPage::GNSS_PAGE; break;
                            case 1: currentPage = DisplayPage::CELLULAR_PAGE; break;
                            case 2: currentPage = DisplayPage::SYSTEM_PAGE; break;
                            case 3: currentPage = DisplayPage::SETTINGS_PAGE; break;
                            case 4: currentPage = DisplayPage::LOGS_PAGE; break;
                        }
#endif
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
            M5.Display.fillScreen(BLACK);

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

            // Push content with slide-in animation for card UI
#if USE_CARD_UI
            pushContentAnimated(g_lastNavDir);
#endif
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
        
        // Draw format confirmation dialog if active
        if (currentPage == DisplayPage::SETTINGS_PAGE && settingsState == SettingsState::FORMAT_CONFIRM) {
            drawFormatConfirmDialog();
        }

        vTaskDelay(pdMS_TO_TICKS(100)); // Reduced delay for better responsiveness
    }
}

// ============================================================================
// STATUS BAR TASK
// ============================================================================
void vTaskStatusBar(void* pvParameters) {
    Serial.println("Status bar task started");
    
    #if LVGL_UI_TEST_MODE
    // LVGL handles status bar, wait for status change events or periodic timeout
    for (;;) {
        EventBits_t bits = xEventGroupWaitBits(
            xEventGroupSystemStatus,
            EVENT_BIT_STATUS_CHANGE | EVENT_BIT_SYSTEM_ERROR | EVENT_BIT_HEAP_LOW,
            pdFALSE,
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
    }
    #else
    for (;;) {
        // Wait for status change events or periodic timeout
        EventBits_t bits = xEventGroupWaitBits(
            xEventGroupSystemStatus,
            EVENT_BIT_STATUS_CHANGE | EVENT_BIT_SYSTEM_ERROR | EVENT_BIT_HEAP_LOW,
            pdFALSE,
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
        
        // Only update status bar, don't interfere with main display
        drawStatusBar();
        
        // Update every 5 seconds - much less frequent than main display
        // Periodic diagnostics: stack high water marks every 30s
        static uint32_t lastDiag = 0;
        if (millis() - lastDiag > 30000) {
            lastDiag = millis();
            auto hwm = [](TaskHandle_t h){ return h ? (unsigned)uxTaskGetStackHighWaterMark(h) : 0u; };
            Serial.printf("HWM words | Blink:%u Button:%u Display:%u Status:%u PLC:%u CatM:%u MQTT:%u Web:%u Storage:%u\n",
                hwm(blinkTaskHandle), hwm(buttonTaskHandle), hwm(displayTaskHandle), hwm(statusBarTaskHandle),
                hwm(stampPLCTaskHandle), hwm(catmGnssTaskHandle), hwm(mqttTaskHandle), hwm(webTaskHandle), hwm(storageTaskHandle));
        }
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
    #endif // !LVGL_UI_TEST_MODE
}

// ============================================================================
// DISPLAY PAGE FUNCTIONS
// ============================================================================
void drawLandingPage() {
#if USE_CARD_UI
    // Full-screen single card launcher (one card per view)
    const int w = M5.Display.width();
    const int h = CONTENT_BOTTOM - CONTENT_TOP;
    if (!contentSpriteInit) {
        contentSprite.setColorDepth(16);
        contentSprite.createSprite(w, h);
        contentSpriteInit = true;
    }
    auto* s = &contentSprite;
    s->fillRect(0, 0, w, h, ui::theme().bg);
    const int pad = LAUNCHER_PAD;
    const int cardW = w - pad * 2;
    const int cardH = h - pad * 2;

    const auto& th = ui::theme();
    // Select current icon
    lgfx::LGFX_Sprite* ic = nullptr;
    switch (launcherIndex) {
        case 0: ic = &iconGPS; break;
        case 1: ic = &iconCellular; break;
        case 2: ic = &iconWrench; break;
        case 3: ic = &iconGear; break;
        case 4: ic = &iconLog; break;
        default: ic = &iconGear; break;
    }

    // Draw the single full-screen card, with subtle lift effect
    int yCard = pad - 2; // lifted a bit for emphasis
    s->drawRoundRect(pad, yCard + 2, cardW, cardH, th.radius, th.accent); // shadow underline
    // Use centered layout and larger icons for the launcher card
    lgfx::LGFX_Sprite* icBig = nullptr;
    switch (launcherIndex) {
        case 0: icBig = &iconGPSBig; break;
        case 1: icBig = &iconCellularBig; break;
        case 2: icBig = &iconWrenchBig; break;
        case 3: icBig = &iconGearBig; break;
        case 4: icBig = &iconLogBig; break;
        default: icBig = ic; break;
    }
    ui::drawCardCentered(s, pad, yCard, cardW, cardH, launcherNames[launcherIndex], icBig ? icBig : ic);
    s->drawRoundRect(pad, yCard, cardW, cardH, th.radius, th.text);

    // Small tag in header
    s->setTextColor(th.textSecondary);
    s->setTextSize(2);
    s->setCursor(pad + cardW - 24, yCard + 6);
    if (launcherIndex == 0) s->print("GPS");
    if (launcherIndex == 1) s->print("4G");
    if (launcherIndex == 2) s->print("SYS");
    if (launcherIndex == 3) s->print("CFG");
    if (launcherIndex == 4) s->print("LOG");
#else
    // Title
    M5.Display.setTextColor(WHITE);
    M5.Display.setTextSize(3);
    M5.Display.setCursor(COL1_X, CONTENT_TOP);
    M5.Display.println("StampPLC");

    M5.Display.setTextSize(1);
    int16_t y = CONTENT_TOP + LINE_H2 + 6;
    M5.Display.setCursor(COL1_X + 10, y);
    M5.Display.println("Hold A/B/C to enter pages:");

    M5.Display.setTextColor(YELLOW);
    M5.Display.setCursor(COL1_X + 10, y + LINE_H1);
    M5.Display.println("A: GNSS Page");

    M5.Display.setCursor(COL1_X + 10, y + LINE_H1 * 2);
    M5.Display.println("B: Cellular Page");

    M5.Display.setCursor(COL1_X + 10, y + LINE_H1 * 3);
    M5.Display.println("C: System Page");

    // Uptime in footer area, above buttons
    M5.Display.setTextColor(CYAN);
    M5.Display.setCursor(COL1_X, BUTTON_BAR_Y - LINE_H1 - 2);
    M5.Display.printf("Up: %lus", (unsigned long)(millis() / 1000));
#endif
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
bool isDST(uint8_t month, uint8_t day, uint8_t hour, uint8_t dayOfWeek) {
    // DST starts: 2nd Sunday in March at 2 AM
    // DST ends: 1st Sunday in November at 2 AM
    
    if (month < 3 || month > 11) return false;  // Winter months
    if (month > 3 && month < 11) return true;  // Summer months
    
    // March: Check if after 2nd Sunday
    if (month == 3) {
        // Find 2nd Sunday in March
        int secondSunday = 8 + (7 - dayOfWeek) % 7;  // 2nd Sunday
        if (day > secondSunday || (day == secondSunday && hour >= 2)) {
            return true;
        }
        return false;
    }
    
    // November: Check if before 1st Sunday
    if (month == 11) {
        // Find 1st Sunday in November
        int firstSunday = 1 + (7 - dayOfWeek) % 7;  // 1st Sunday
        if (day < firstSunday || (day == firstSunday && hour < 2)) {
            return true;
        }
        return false;
    }
    
    return false;
}

// RTC time management functions
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
        Serial.printf("RTC set from GPS: %04d-%02d-%02d %02d:%02d:%02d UTC\n", 
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
#if USE_CARD_UI
    drawGNSSPageCards();
    return;
#endif
    int16_t yOffset = scrollGNSS;
    // Title
    M5.Display.setTextColor(WHITE);
    M5.Display.setTextSize(2);
    M5.Display.setCursor(COL1_X, CONTENT_TOP - yOffset);
    M5.Display.println("GNSS Status");
    
    // GNSS data
    if (catmGnssModule && catmGnssModule->isModuleInitialized()) {
        GNSSData data = catmGnssModule->getGNSSData();
        
        M5.Display.setTextSize(1);
        M5.Display.setTextColor(YELLOW);
        M5.Display.setCursor(COL1_X, CONTENT_TOP + LINE_H2 - yOffset);
        M5.Display.println("Position Data:");
        
        M5.Display.setTextColor(WHITE);
        if (data.isValid) {
            M5.Display.setCursor(COL1_X, CONTENT_TOP + LINE_H2 + LINE_H1 - yOffset);
            M5.Display.printf("Latitude:  %.6f", data.latitude);
            
            M5.Display.setCursor(COL1_X, CONTENT_TOP + LINE_H2 + LINE_H1 * 2 - yOffset);
            M5.Display.printf("Longitude: %.6f", data.longitude);
            
            M5.Display.setCursor(COL1_X, CONTENT_TOP + LINE_H2 + LINE_H1 * 3 - yOffset);
            M5.Display.printf("Altitude:  %.1f m", data.altitude);
            
            M5.Display.setCursor(COL1_X, CONTENT_TOP + LINE_H2 + LINE_H1 * 4 - yOffset);
            M5.Display.printf("Speed:     %.1f km/h", data.speed);
            
            M5.Display.setCursor(COL1_X, CONTENT_TOP + LINE_H2 + LINE_H1 * 5 - yOffset);
            M5.Display.printf("Course:    %.1f deg", data.course);
        } else {
            M5.Display.setTextColor(RED);
            M5.Display.setCursor(COL1_X, CONTENT_TOP + LINE_H2 + LINE_H1 - yOffset);
            M5.Display.println("No valid fix");
        }
        
        M5.Display.setTextColor(YELLOW);
        M5.Display.setCursor(COL1_X, CONTENT_TOP + LINE_H2 + LINE_H1 * 6 - yOffset);
        M5.Display.println("Satellites:");
        
        M5.Display.setTextColor(WHITE);
        M5.Display.setCursor(COL1_X, CONTENT_TOP + LINE_H2 + LINE_H1 * 7 - yOffset);
        M5.Display.printf("In view: %d", data.satellites);
        
        // Time data
        if (data.isValid) {
            M5.Display.setTextColor(CYAN);
            M5.Display.setCursor(COL2_X, CONTENT_TOP + LINE_H2 + LINE_H1 - yOffset);
            M5.Display.println("UTC Time:");
            
            M5.Display.setTextColor(WHITE);
            M5.Display.setCursor(COL2_X, CONTENT_TOP + LINE_H2 + LINE_H1 * 2 - yOffset);
            M5.Display.printf("%04d-%02d-%02d", data.year, data.month, data.day);
            
            M5.Display.setCursor(COL2_X, CONTENT_TOP + LINE_H2 + LINE_H1 * 3 - yOffset);
            M5.Display.printf("%02d:%02d:%02d", data.hour, data.minute, data.second);
        }
    } else {
        M5.Display.setTextSize(1);
        M5.Display.setTextColor(RED);
        M5.Display.setCursor(10, 60);
        M5.Display.println("GNSS module not initialized");
    }
}

void drawCellularPage() {
    #if USE_CARD_UI
    drawCellularPageCards();
    return;
    #endif
    int16_t yOffset = scrollCELL;
    // Title
    M5.Display.setTextColor(WHITE);
    M5.Display.setTextSize(2);
    M5.Display.setCursor(COL1_X, CONTENT_TOP - yOffset);
    M5.Display.println("Cellular Status");
    
    // Cellular data
    if (catmGnssModule && catmGnssModule->isModuleInitialized()) {
        CellularData data = catmGnssModule->getCellularData();
        
        M5.Display.setTextSize(1);
        M5.Display.setTextColor(YELLOW);
        M5.Display.setCursor(COL1_X, CONTENT_TOP + LINE_H2 - yOffset);
        M5.Display.println("Network Status:");
        
        M5.Display.setTextColor(data.isConnected ? GREEN : RED);
        M5.Display.setCursor(COL1_X, CONTENT_TOP + LINE_H2 + LINE_H1 - yOffset);
        M5.Display.printf("Connected: %s", data.isConnected ? "YES" : "NO");
        
        M5.Display.setTextColor(WHITE);
        M5.Display.setCursor(COL1_X, CONTENT_TOP + LINE_H2 + LINE_H1 * 2 - yOffset);
        M5.Display.printf("Operator: %s", data.operatorName.c_str());
        
        // Signal strength bar
        drawSignalBar(COL1_X, CONTENT_TOP + LINE_H2 + LINE_H1 * 3 - yOffset, 120, 12, data.signalStrength);
        
        M5.Display.setTextColor(YELLOW);
        M5.Display.setCursor(COL1_X, CONTENT_TOP + LINE_H2 + LINE_H1 * 5 - yOffset);
        M5.Display.println("Device Info:");
        
        M5.Display.setTextColor(WHITE);
        M5.Display.setCursor(COL1_X, CONTENT_TOP + LINE_H2 + LINE_H1 * 6 - yOffset);
        M5.Display.printf("IMEI: %s", data.imei.c_str());
        
        M5.Display.setCursor(COL1_X, CONTENT_TOP + LINE_H2 + LINE_H1 * 7 - yOffset);
        M5.Display.printf("Last Update: %d ms ago", millis() - data.lastUpdate);
        
        M5.Display.setCursor(COL1_X, CONTENT_TOP + LINE_H2 + LINE_H1 * 8 - yOffset);
        M5.Display.printf("Errors: %d", data.errorCount);
        
        // Add some spacing at bottom
        M5.Display.setCursor(COL1_X, CONTENT_BOTTOM - LINE_H1 * 2 - yOffset);
        M5.Display.print(""); // Empty line for spacing
    } else {
        M5.Display.setTextSize(1);
        M5.Display.setTextColor(RED);
        M5.Display.setCursor(10, 60);
        M5.Display.println("Cellular module not initialized");
    }
}

void drawSystemPage() {
#if USE_CARD_UI
    drawSystemPageCards();
    return;
#endif
    int16_t yOffset = scrollSYS;
    // Title
    M5.Display.setTextColor(WHITE);
    M5.Display.setTextSize(2);
    M5.Display.setCursor(COL1_X, CONTENT_TOP - yOffset);
    M5.Display.println("System Status");
    
    // System info - compact layout
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(YELLOW);
    M5.Display.setCursor(COL1_X, CONTENT_TOP + LINE_H2 - yOffset);
    M5.Display.println("Memory:");
    
    M5.Display.setTextColor(WHITE);
    M5.Display.setCursor(COL1_X, CONTENT_TOP + LINE_H2 + LINE_H1 - yOffset);
    M5.Display.printf("Free RAM: %d KB", ESP.getFreeHeap() / 1024);
    
    M5.Display.setCursor(COL1_X, CONTENT_TOP + LINE_H2 + LINE_H1 * 2 - yOffset);
    M5.Display.printf("Total RAM: %d KB", ESP.getHeapSize() / 1024);
    
    M5.Display.setCursor(COL1_X, CONTENT_TOP + LINE_H2 + LINE_H1 * 3 - yOffset);
    M5.Display.printf("CPU Freq: %d MHz", ESP.getCpuFreqMHz());
    
    // WiFi connection bar
    // Extra spacing before WiFi bar
    drawWiFiBar(COL1_X, CONTENT_TOP + LINE_H2 + LINE_H1 * 5 - yOffset, 120, 12);
    
    // Module status - moved up
    M5.Display.setTextColor(YELLOW);
    M5.Display.setCursor(COL1_X, CONTENT_TOP + LINE_H2 + LINE_H1 * 7 - yOffset);
    M5.Display.println("Module Status:");
    
    M5.Display.setTextColor(WHITE);
    M5.Display.setCursor(COL1_X, CONTENT_TOP + LINE_H2 + LINE_H1 * 8 - yOffset);
    M5.Display.printf("StampPLC: %s", stampPLC && stampPLC->isReady() ? "READY" : "NOT READY");
    
    M5.Display.setCursor(COL1_X, CONTENT_TOP + LINE_H2 + LINE_H1 * 9 - yOffset);
    M5.Display.printf("CatM+GNSS: %s", catmGnssModule && catmGnssModule->isModuleInitialized() ? "READY" : "NOT READY");
    
    // SD card status
    M5.Display.setCursor(COL1_X, CONTENT_TOP + LINE_H2 + LINE_H1 * 10 - yOffset);
    M5.Display.printf("SD: %s", (sdModule && sdModule->isMounted()) ? "MOUNTED" : "NOT PRESENT");
    if (sdModule && sdModule->isMounted()) {
        drawSDInfo(COL2_X, CONTENT_TOP + LINE_H2 + LINE_H1 * 9 - yOffset);
    }
    
    // Uptime - moved up
    // Removed uptime per request
}

void drawSettingsPage() {
#if USE_CARD_UI
    drawSettingsPageCards();
    return;
#endif
    int16_t yOffset = scrollSETTINGS;
    
    // Title
    M5.Display.setTextColor(WHITE);
    M5.Display.setTextSize(3);
    M5.Display.setCursor(COL1_X, CONTENT_TOP - yOffset);
    M5.Display.println("Settings");
    
    // SD Card information
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(YELLOW);
    M5.Display.setCursor(COL1_X, CONTENT_TOP + LINE_H2 * 2 - yOffset);
    M5.Display.println("SD Card");
    
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(WHITE);
    M5.Display.setCursor(COL1_X, CONTENT_TOP + LINE_H2 * 4 - yOffset);
    if (sdModule && sdModule->isMounted()) {
        M5.Display.println("Status: Mounted");
        M5.Display.setCursor(COL1_X, CONTENT_TOP + LINE_H2 * 5 - yOffset);
        M5.Display.println("Press C to format card");
        
        // Show SD card info
        drawSDInfo(COL1_X, CONTENT_TOP + LINE_H2 * 6 - yOffset);
    } else {
        M5.Display.println("Status: Not mounted");
    }
}

void drawLogsPage() {
#if USE_CARD_UI
    drawLogsPageCards();
    return;
#endif
    int16_t yOffset = scrollLOGS;
    M5.Display.setTextColor(WHITE);
    M5.Display.setTextSize(2);
    M5.Display.setCursor(COL1_X, CONTENT_TOP - yOffset);
    M5.Display.println("Logs");

    // Draw recent lines (oldest at top)
    M5.Display.setTextSize(1);
    const int pad = 2;
    size_t cnt = log_count();
    int contentH = (int)cnt * LINE_H1 + pad * 2;
    scrollLOGS = clampScroll(scrollLOGS, contentH);

    int y = CONTENT_TOP + LINE_H2 - yOffset;
    char line[160];
    for (size_t i = 0; i < cnt; ++i) {
        if (log_get_line(i, line, sizeof(line))) {
            if (y > CONTENT_TOP && y < CONTENT_BOTTOM) {
                M5.Display.setCursor(COL1_X, y);
                M5.Display.print(line);
            }
            y += LINE_H1;
        }
    }
}

void drawButtonIndicators() {
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(WHITE);

    bool onLanding = (currentPage == DisplayPage::LANDING_PAGE);
    if (onLanding) {
#if USE_CARD_UI
        // Card launcher: B=Next, C=Prev, A=Open
        M5.Display.setCursor(10, BUTTON_BAR_Y + 2);
        M5.Display.print("A: OPEN");
        M5.Display.setCursor(90, BUTTON_BAR_Y + 2);
        M5.Display.print("B: NEXT");
        M5.Display.setCursor(170, BUTTON_BAR_Y + 2);
        M5.Display.print("C: PREV");
#else
        M5.Display.setCursor(10, BUTTON_BAR_Y + 2);
        M5.Display.print("A: HOME");
        M5.Display.setCursor(90, BUTTON_BAR_Y + 2);
        M5.Display.print("B: PREV");
        M5.Display.setCursor(170, BUTTON_BAR_Y + 2);
        M5.Display.print("C: NEXT");
#endif
        return;
    }

    // Special handling for settings page with format dialog
    if (currentPage == DisplayPage::SETTINGS_PAGE && settingsState == SettingsState::FORMAT_CONFIRM) {
        M5.Display.setCursor(10, BUTTON_BAR_Y + 2);
        M5.Display.print("A: HOME");
        M5.Display.setCursor(90, BUTTON_BAR_Y + 2);
        M5.Display.print("B: YES");
        M5.Display.setCursor(170, BUTTON_BAR_Y + 2);
        M5.Display.print("C: CANCEL");
        return;
    }
    
    // Special handling for settings page
    if (currentPage == DisplayPage::SETTINGS_PAGE && settingsState == SettingsState::NORMAL) {
        M5.Display.setCursor(10, BUTTON_BAR_Y + 2);
        M5.Display.print("A: HOME");
        M5.Display.setCursor(100, BUTTON_BAR_Y + 2);
        M5.Display.print("B: PREV");
        M5.Display.setCursor(170, BUTTON_BAR_Y + 2);
        M5.Display.print("C: FORMAT");
        return;
    }

    // Global controls: A=HOME, B=PREV, C=NEXT
    M5.Display.setCursor(10, BUTTON_BAR_Y + 2);
    M5.Display.print("A: HOME");
    M5.Display.setCursor(100, BUTTON_BAR_Y + 2);
    M5.Display.print("B: PREV");
    M5.Display.setCursor(185, BUTTON_BAR_Y + 2);
    M5.Display.print("C: NEXT");
}

void drawSDInfo(int x, int y) {
    if (!sdModule || !sdModule->isMounted()) return;
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(CYAN);
    M5.Display.setCursor(x, y);
    uint64_t total = sdModule->totalBytes();
    uint64_t used = sdModule->usedBytes();
    M5.Display.printf("SD Total: %.1f MB", total ? (double)total / (1024.0 * 1024.0) : 0.0);
    M5.Display.setCursor(x, y + 20);
    M5.Display.printf("SD Used:  %.1f MB", used ? (double)used / (1024.0 * 1024.0) : 0.0);
}

void drawStatusBar() {
    const int barH = STATUS_BAR_H;
    if (!statusSpriteInit) {
        statusSprite.setColorDepth(16);
        statusSprite.createSprite(M5.Display.width(), barH);
        statusSpriteInit = true;
        lastGnssOk = !lastGnssOk; // force first render
    }

    // Ensure NTP configured if possible and update TZ from cellular
    ensureNtpConfigured();
    maybeUpdateTimeZoneFromCellular();

    // Get current time and date preferring NTP/localtime, then RTC, then GPS, then fallback
    String timeStr;
    String dateStr;
    bool timeFromGPS = false;
    bool timeFromRTC = false;
    bool timeFromNTP = false;

    // 1) Try local time (NTP) first
    struct tm lt;
    if (getLocalTime(&lt, 50)) { // quick, non-blocking
        timeFromNTP = true;
        char buf[16];
        snprintf(buf, sizeof(buf), "%02d:%02d", lt.tm_hour, lt.tm_min);
        timeStr = buf;
        char dbuf[16];
        snprintf(dbuf, sizeof(dbuf), "%02d/%02d/%04d", lt.tm_mon + 1, lt.tm_mday, lt.tm_year + 1900);
        dateStr = dbuf;
        // Optionally sync RTC from NTP periodically (store UTC in RTC)
        static uint32_t lastRtcSync = 0;
        if (millis() - lastRtcSync > 3600000UL) { // hourly
            time_t now = time(nullptr);
            struct tm utc;
            gmtime_r(&now, &utc);
            if (M5StamPLC.RX8130.begin()) {
                M5StamPLC.RX8130.setTime(&utc);
                lastRtcSync = millis();
            }
        }
    }

    // 2) If no NTP, try RTC (UTC) and convert to local using TZ
    if (!timeFromNTP) {
        struct tm rtcTime;
        if (getRTCTime(rtcTime)) {
            formatLocalFromUTC(rtcTime, timeStr, dateStr);
            timeFromRTC = true;
        }
    }

    // 3) If no NTP/RTC, try GPS
    if (!timeFromNTP && !timeFromRTC) {
        if (catmGnssModule && catmGnssModule->isModuleInitialized()) {
            GNSSData gnssData = catmGnssModule->getGNSSData();
            if (gnssData.isValid && gnssData.year > 2020) {
                // Optionally set RTC from GPS only if we don't have NTP
                setRTCFromGPS(gnssData);
                struct tm utc{};
                utc.tm_year = gnssData.year - 1900;
                utc.tm_mon  = gnssData.month - 1;
                utc.tm_mday = gnssData.day;
                utc.tm_hour = gnssData.hour;
                utc.tm_min  = gnssData.minute;
                utc.tm_sec  = gnssData.second;
                utc.tm_isdst = 0;
                formatLocalFromUTC(utc, timeStr, dateStr);
                timeFromGPS = true;
            }
        }
    }

    // 4) Last resort: uptime-based pseudo time
    if (!timeFromNTP && !timeFromRTC && !timeFromGPS) {
        uint32_t uptime = millis() / 1000;
        int uh = (uptime / 3600) % 24;
        int um = (uptime / 60) % 60;
        timeStr = String(uh < 10 ? "0" : "") + String(uh) + ":" + (um < 10 ? "0" : "") + String(um);
        dateStr = "--/--/----";
    }
    
    // Get GNSS data
    bool gnssOk = (catmGnssModule && catmGnssModule->isModuleInitialized());
    uint8_t gnssSatellites = 0;
    if (gnssOk) {
        GNSSData gnssData = catmGnssModule->getGNSSData();
        gnssSatellites = gnssData.satellites;
    }
    
    // Get cellular data
    bool cellOk = false;
    uint8_t cellularSignal = 0;
    if (catmGnssModule && catmGnssModule->isModuleInitialized()) {
        CellularData cellData = catmGnssModule->getCellularData();
        cellOk = cellData.isConnected;
        cellularSignal = constrain(map(cellData.signalStrength, -120, -50, 0, 100), 0, 100);
    }

    // Check if we need to update (every 10 seconds or on data change)
    bool needsUpdate = (millis() - lastStatusUpdate > 10000) ||
                       (gnssOk != lastGnssOk) ||
                       (cellOk != lastCellOk) ||
                       (gnssSatellites != lastGnssSatellites) ||
                       (cellularSignal != lastCellularSignal) ||
                       (timeStr != lastTimeStr);
    
    if (needsUpdate) {
        // Clear background
        statusSprite.fillRect(0, 0, M5.Display.width(), barH, BLACK);
        
        // Draw time and date on single line (left side) - compact format
        statusSprite.setTextSize(1);
        // Always white per request
        statusSprite.setTextColor(WHITE);
        statusSprite.setCursor(2, 3);
        statusSprite.print(timeStr);
        statusSprite.print(" ");
        statusSprite.print(dateStr);
        
        // Draw GPS indicator (center) - with icon and visual signal bar
        int gpsX = 110;
        if (gnssOk) {
            // Draw GPS icon
            statusSprite.setTextColor(GREEN); statusSprite.setCursor(gpsX, 3); statusSprite.print("GPS");
            
            
            // Satellite count
            statusSprite.setTextColor(WHITE);
            statusSprite.setCursor(gpsX + 20, 3);
            if (gnssSatellites > 0) {
                statusSprite.print(gnssSatellites);
                statusSprite.print("s");
            } else {
                statusSprite.print("--");
            }
        } else {
            statusSprite.setTextColor(RED);
            statusSprite.setCursor(gpsX, 3);
            statusSprite.print("GPS");
            statusSprite.setCursor(gpsX + 12, 3);
            statusSprite.print("OFF");
        }
        
        // Draw cellular status (right side) - with tower icon and visual signal bar
        int signalX = M5.Display.width() - 40;
        if (cellOk) {
            // Get cellular data for network type detection
            CellularData cellData = catmGnssModule->getCellularData();
            String networkType = getNetworkType(cellData.signalStrength);
            
            // Draw cellular tower icon
            
            // Cellular signal bar
            uint16_t cellColor = (cellularSignal > 70) ? GREEN : (cellularSignal > 40) ? YELLOW : RED;
            drawCompactSignalBar(&statusSprite, signalX + 12, 5, cellularSignal, cellColor);
            
            // Signal strength percentage
            statusSprite.setTextColor(WHITE);
            statusSprite.setCursor(signalX + 24, 3);
            statusSprite.print(cellularSignal);
        } else {
            statusSprite.setTextColor(RED);
            statusSprite.setCursor(signalX, 3);
            statusSprite.print("OFF");
            statusSprite.setCursor(signalX + 12, 3);
            statusSprite.print("--");
        }
        
        // Bottom border line
        statusSprite.drawFastHLine(0, barH - 1, M5.Display.width(), 0x4A4A4A);
        
        // Update last values
        lastGnssOk = gnssOk;
        lastCellOk = cellOk;
        lastGnssSatellites = gnssSatellites;
        lastCellularSignal = cellularSignal;
        lastTimeStr = timeStr;
        lastStatusUpdate = millis();
        
        // Push sprite only when we actually update
        statusSprite.pushSprite(0, 0);
    }
}

void drawSignalBar(int16_t x, int16_t y, int16_t w, int16_t h, int8_t signalDbm) {
    // Draw signal strength bar (dBm to percentage)
    int16_t signalPercent = map(signalDbm, -120, -50, 0, 100);
    signalPercent = constrain(signalPercent, 0, 100);
    
    // Background
    M5.Display.fillRect(x, y, w, h, 0x2104); // Dark gray
    M5.Display.drawRect(x, y, w, h, WHITE);
    
    // Signal level
    uint16_t signalColor = (signalPercent > 70) ? GREEN : 
                          (signalPercent > 40) ? YELLOW : RED;
    int16_t fillWidth = (w - 2) * signalPercent / 100;
    if (fillWidth > 0) {
        M5.Display.fillRect(x + 1, y + 1, fillWidth, h - 2, signalColor);
    }
    
    // Text label
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(WHITE);
    M5.Display.setCursor(x + w + 5, y);
    M5.Display.printf("%ddBm", signalDbm);
}

void drawWiFiBar(int16_t x, int16_t y, int16_t w, int16_t h) {
    // Background
    M5.Display.fillRect(x, y, w, h, 0x2104); // Dark gray
    M5.Display.drawRect(x, y, w, h, WHITE);

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
            M5.Display.fillRect(x + 1, y + 1, fillWidth, h - 2, wifiColor);
        }
        M5.Display.setTextSize(1);
        M5.Display.setTextColor(WHITE);
        M5.Display.setCursor(x + w + 5, y);
        M5.Display.printf("STA:%ddBm", wifiRSSI);
        return;
    }

    if (isAp) {
        // Show AP active; client count if available
        uint16_t color = YELLOW;
        M5.Display.fillRect(x + 1, y + 1, w - 2, h - 2, color);
        M5.Display.setTextSize(1);
        M5.Display.setTextColor(WHITE);
        M5.Display.setCursor(x + w + 5, y);
        // softAPgetStationNum may not exist on all cores; guard with weak behavior
        #ifdef ARDUINO_ARCH_ESP32
        M5.Display.printf("AP:%d", WiFi.softAPgetStationNum());
        #else
        M5.Display.print("AP:ON");
        #endif
        return;
    }

    // No WiFi
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(RED);
    M5.Display.setCursor(x + w + 5, y);
    M5.Display.print("WiFi:OFF");
}

// ============================================================================
// ARDUINO SETUP & LOOP
// ============================================================================
static void drawFormatConfirmDialog() {
    // Format confirmation dialog
    const int w = M5.Display.width();
    const int contentY = CONTENT_TOP;
    const int contentH = CONTENT_BOTTOM - CONTENT_TOP;
    const int boxW = w - 20;
    const int boxH = 85;
    const int x = (w - boxW) / 2;
    const int y = contentY + (contentH - boxH) / 2;

    // Opaque backdrop for content area to fully hide page content
    M5.Display.fillRect(0, contentY, w, contentH, BLACK);

    // Modal box
    const uint16_t bg = 0x3186; // slightly lighter gray for contrast
    M5.Display.fillRect(x, y, boxW, boxH, bg);
    M5.Display.drawRect(x, y, boxW, boxH, YELLOW);

    // Title (centered)
    M5.Display.setTextColor(WHITE);
    M5.Display.setTextSize(2);
    {
        const char* ttl = "Format SD Card";
        int tw = M5.Display.textWidth(ttl);
        int tx = x + (boxW - tw) / 2;
        M5.Display.setCursor(tx, y + 6);
        M5.Display.print(ttl);
    }

    // Body
    M5.Display.setTextSize(1);
    M5.Display.setCursor(x + 8, y + 28);
    M5.Display.print("Are you sure you wish to format");
    M5.Display.setCursor(x + 8, y + 40);
    M5.Display.print("the SD card? All data will be");
    M5.Display.setCursor(x + 8, y + 52);
    M5.Display.print("removed.");

    // Actions row
    M5.Display.setTextColor(CYAN);
    M5.Display.setCursor(x + 8, y + boxH - 16);
    M5.Display.print("B: YES");
    M5.Display.setCursor(x + boxW - 80, y + boxH - 16);
    M5.Display.print("C: CANCEL");
}

static void drawModalOverlay() {
    // Modal in content area only to avoid clashing with status bar
    const int w = M5.Display.width();
    const int contentY = CONTENT_TOP;
    const int contentH = CONTENT_BOTTOM - CONTENT_TOP;
    const int boxW = w - 24;
    const int boxH = 78;
    const int x = (w - boxW) / 2;
    const int y = contentY + (contentH - boxH) / 2;

    // Opaque backdrop for content area to fully hide page content
    M5.Display.fillRect(0, contentY, w, contentH, BLACK);

    // Modal box
    const uint16_t bg = 0x3186; // slightly lighter gray for contrast
    M5.Display.fillRect(x, y, boxW, boxH, bg);
    M5.Display.drawRect(x, y, boxW, boxH, YELLOW);

    // Title (centered)
    M5.Display.setTextColor(WHITE);
    M5.Display.setTextSize(2);
    {
        const char* ttl = "Unit Not Found";
        int tw = M5.Display.textWidth(ttl);
        int tx = x + (boxW - tw) / 2;
        M5.Display.setCursor(tx, y + 6);
        M5.Display.print(ttl);
    }

    // Body
    M5.Display.setTextSize(1);
    // Short, pre-wrapped lines to avoid overflow
    M5.Display.setCursor(x + 8, y + 28);
    M5.Display.print("Comm Unit (CatM+GNSS) not detected.");
    M5.Display.setCursor(x + 8, y + 40);
    M5.Display.print("Connect unit, then press C to retry.");

    // Actions row
    M5.Display.setTextColor(CYAN);
    M5.Display.setCursor(x + 8, y + boxH - 16);
    M5.Display.print("A: OK");
    M5.Display.setCursor(x + boxW - 80, y + boxH - 16);
    M5.Display.print("C: Retry");

    // Optional toast message while retrying
    if (g_retryToastActive && (int32_t)(g_retryToastUntilMs - millis()) > 0) {
        M5.Display.setTextColor(YELLOW);
        const char* toast = "Retrying...";
        int tw = M5.Display.textWidth(toast);
        int tx = x + (boxW - tw) / 2;
        M5.Display.setCursor(tx, y + boxH - 30);
        M5.Display.print(toast);
    } else {
        g_retryToastActive = false;
    }
}

static void tryInitCatMIfAbsent(bool forced) {
    if (catmGnssModule) return; // already present
    uint32_t now = millis();
    if (!forced && (now - g_lastCatMProbeMs) < 60000UL) return; // 60s interval
    g_lastCatMProbeMs = now;

    Serial.println("[CatM] Background re-probe starting...");
    CatMGNSSModule* m = new CatMGNSSModule();
    if (!m) return;
    if (m->begin()) {
        catmGnssModule = m;
        Serial.println("[CatM] Hot-attach successful");
        log_add("CatM+GNSS hot-attached");
        // Spawn the CatMGNSS worker task
        xTaskCreatePinnedToCore(
            vTaskCatMGNSS,
            "CatMGNSS",
            TASK_STACK_SIZE_APP_GNSS,
            catmGnssModule,
            TASK_PRIORITY_GNSS,
            &catmGnssTaskHandle,
            0
        );
        Serial.println("CatM+GNSS task created (hot)");
        // Dismiss modal if shown
        g_modalActive = false;
        g_modalType = ModalType::NONE;
        if (g_uiQueue) { UIEvent e{UIEventType::Redraw}; xQueueSend(g_uiQueue, &e, 0); }
    } else {
        Serial.println("[CatM] Re-probe failed: not found");
        delete m;
    }
}

void setup() {
    // Initialize serial
    Serial.begin(SERIAL_BAUD);
    delay(500);  // Short delay for serial to initialize
    
    Serial.println("\n\n=== StampPLC CatM+GNSS Integration ===");
    Serial.println("Initializing...");
    
    set_log_level(DEBUG_LOG_LEVEL_INFO);

    // Initialize M5Stack
    auto cfg = M5.config();
    cfg.fallback_board = m5::board_t::board_M5StampPLC;
    M5.begin(cfg);
    
    // Reduce log noise from M5GFX/LGFX components
    esp_log_level_set("lgfx", ESP_LOG_ERROR);  // Only show errors, not verbose warnings
    esp_log_level_set("esp32-hal-uart", ESP_LOG_ERROR);
    esp_log_level_set("esp32-hal-i2c", ESP_LOG_ERROR);  // Reduce I2C noise
    esp_log_level_set("M5GFX", ESP_LOG_ERROR);
    Serial.println("M5Stack initialized");
    Serial.printf("Display w=%d, h=%d\n", M5.Display.width(), M5.Display.height());
    
    // Initialize display brightness
    M5.Display.setBrightness(displayBrightness);
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
    
    // Load better font from SD if available
    if (ui::fonts::init() && ui::fonts::applyToDisplay()) {
        Serial.println("Custom font loaded (SD /fonts)");
    } else {
        Serial.println("Custom font not found; using default font");
    }
    
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
#if ENABLE_SD
    sdModule = new SDCardModule();
    if (sdModule->begin()) {
        Serial.println("SD card detected and mounted");
        // Quick smoke test: log boot marker
        sdModule->writeText("/boot.log", String("Boot @ ") + String((uint32_t)millis()) + " ms\n", true);
        log_add("SD card mounted");
    } else {
        Serial.println("No SD card present or mount failed");
        delete sdModule;
        sdModule = nullptr;
        log_add("SD card not present");
    }
#else
    Serial.println("SD support disabled (ENABLE_SD=0)");
#endif

    // Create UI queue
    g_uiQueue = xQueueCreate(16, sizeof(UIEvent));
    
    // Create system event group
    xEventGroupSystemStatus = xEventGroupCreate();
    if (xEventGroupSystemStatus == NULL) {
        Serial.println("ERROR: Failed to create system event group");
        return;
    }
    
    // Init logging
    log_init();
    log_add("Booting StampPLC CatM+GNSS...");

    // Storage task (Core 0)
    xTaskCreatePinnedToCore(
        vTaskStorage,
        "Storage",
        TASK_STACK_SIZE,
        NULL,
        TASK_PRIORITY_DATA_TRANSMIT,
        &storageTaskHandle,
        0
    );
    Serial.println("Storage task created");
    
    // Initialize CatM+GNSS Module
    catmGnssModule = new CatMGNSSModule();
    if (catmGnssModule->begin()) {
        Serial.println("CatM+GNSS module initialized successfully");
        log_add("CatM+GNSS initialized");
    } else {
        Serial.println("CatM+GNSS module initialization failed");
        delete catmGnssModule;
        catmGnssModule = nullptr;
        log_add("CatM+GNSS init failed");
        // Show modal alert; allow user to dismiss (A) or retry (C)
        g_modalActive = true;
        g_modalType = ModalType::NO_COMM_UNIT;
        pageChanged = true; // ensure UI redraw shows modal
    }

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

    // MQTT task (Core 0)
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
    
    // Create blink task (Core 0)
    xTaskCreatePinnedToCore(
        vTaskBlink,
        "Blink",
        TASK_STACK_SIZE_SYSTEM_MONITOR,
        NULL,
        TASK_PRIORITY_BUTTON_HANDLER,
        &blinkTaskHandle,
        0
    );
    Serial.println("Blink task created");
    
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
    
    // Create status bar task (Core 0) - dedicated thread for status updates
    xTaskCreatePinnedToCore(
        vTaskStatusBar,
        "StatusBar",
        TASK_STACK_SIZE_SYSTEM_MONITOR,
        NULL,
        TASK_PRIORITY_SYSTEM_MONITOR,
        &statusBarTaskHandle,
        0
    );
    Serial.println("Status bar task created");
    log_add("StatusBar task started");
    
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
    SD.end();
    
    // Wait for unmount to complete
    vTaskDelay(pdMS_TO_TICKS(500));
    
    bool formatSuccess = false;
    
    // Try SDFat library for proper formatting first
    Serial.println("Using SDFat library for formatting...");
    SdFat sdFat;
    
    // Initialize SDFat with proper pin configuration
    // Note: Use GPIO 15 for StampPLC SD CS to avoid conflict with CatM UART
    const int SD_CS_PIN = 15; // StampPLC SD CS pin (avoid GPIO 4 conflict)
    
    if (sdFat.begin(SD_CS_PIN, SD_SCK_MHZ(50))) {
        Serial.println("SDFat initialized, starting format...");
        
        // Get card info
        cid_t cid;
        csd_t csd;
        if (sdFat.card()->readCID(&cid) && sdFat.card()->readCSD(&csd)) {
            Serial.printf("Card capacity: %.2f GB\n", 
                        0.000512 * sdFat.card()->sectorCount());
        }
        
        // Perform proper format using SDFat
        // Note: SDFat erase() requires sector range, format() doesn't exist
        // Use sector-by-sector erase for full card erase
        uint32_t sectorCount = sdFat.card()->sectorCount();
        if (sdFat.card()->erase(0, sectorCount - 1)) {
            Serial.println("Card erase completed");
            formatSuccess = true;
        } else {
            Serial.println("Card erase failed");
        }
        
        sdFat.end();
    } else {
        Serial.println("SDFat initialization failed, falling back to file deletion");
        formatSuccess = false;
    }

    // Fallback: Use file deletion approach if SDFat fails or not available
    if (!formatSuccess) {
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
                        if (!deleteDirectory("/" + fileName)) {
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

// Helper function to recursively delete directories
bool deleteDirectory(String path) {
    File dir = SD.open(path);
    if (!dir) return false;
    
    bool success = true;
    File file = dir.openNextFile();
    while (file && success) {
        String fileName = "/" + String(file.name());
        if (file.isDirectory()) {
            success = deleteDirectory(path + fileName);
        } else {
            success = SD.remove(path + fileName);
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
