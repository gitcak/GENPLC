#include <Arduino.h>
#include <M5StamPLC.h>
#include <M5GFX.h>  // Required for lgfx::LGFX_Sprite compatibility
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
#include "system/crash_recovery.h"
#include "system/memory_safety.h"
#include "system/rtc_manager.h"
#include "system/time_utils.h"
#include "system/storage_utils.h"

// Include our modules
#include "hardware/basic_stamplc.h"
#include "modules/catm_gnss/catm_gnss_module.h"
#include "modules/catm_gnss/catm_gnss_task.h"
#include "modules/catm_gnss/gnss_status.h"
#include "modules/catm_gnss/network_utils.h"
#include "config/system_config.h"
#include "modules/pwrcan/pwrcan_module.h"
#include "modules/storage/sd_card_module.h"
#include "modules/storage/storage_task.h"
#include "ui/theme.h"
#include "ui/components.h"
#include "ui/ui_constants.h"
#include "ui/ui_types.h"
#include "ui/pages/landing_page.h"
#include "ui/pages/gnss_page.h"
#include "ui/pages/cellular_page.h"
#include "ui/pages/system_page.h"
#include "ui/pages/settings_page.h"
#include "ui/pages/logs_page.h"
#include "ui/components/ui_widgets.h"
#include "ui/components/icon_manager.h"
#include "ui/boot_screen.h"

// Legacy UI runs directly on M5GFX framebuffer (LVGL removed)
extern "C" void vTaskPWRCAN(void* pvParameters);

// ============================================================================
// FORWARD DECLARATIONS
// ============================================================================
void drawBootScreen(const char* status, int progress, bool passed);
// RTC functions moved to system/rtc_manager.cpp
// getNetworkType moved to modules/catm_gnss/network_utils.cpp
void initializeIcons();
void updateSystemLED();



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
// Web task removed - web server functionality not required

// Shared connectivity flags consumed by CAT-M
volatile bool g_cellularUp = false;

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

// Mutex for protecting shared UI state
static SemaphoreHandle_t g_uiStateMutex = nullptr;
static String g_commFailureDescription = "Ensure CatM+GNSS unit is connected to Grove Port C (G4/G5).";

// BootLogEntry moved to ui/boot_screen.cpp (internal use only)

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

// drawWrappedText moved to ui/components/ui_widgets.cpp

// Display page management (moved to ui_types.h, keeping declaration here for extern compatibility)
volatile DisplayPage currentPage = DisplayPage::LANDING_PAGE;
volatile bool pageChanged = false;

// Status bar sprite & cache
// Note: M5GFX maintains compatibility with lgfx namespace, so lgfx::LGFX_Sprite works correctly
lgfx::LGFX_Sprite statusSprite(&M5StamPLC.Display);
bool statusSpriteInit = false;
static bool lastGnssOk = false;
static bool lastCellOk = false;
static uint32_t lastStatusUpdate = 0;
static uint8_t lastCellularSignal = 0;
static uint8_t lastGnssSatellites = 0;
static char lastTimeStr[32] = "";

// Icon sprites for UI elements (declared here for access by icon_manager.cpp)
// Note: M5GFX maintains compatibility with lgfx namespace, so lgfx::LGFX_Sprite works correctly
lgfx::LGFX_Sprite iconSatellite(&M5StamPLC.Display);
lgfx::LGFX_Sprite iconGear(&M5StamPLC.Display);
lgfx::LGFX_Sprite iconTower(&M5StamPLC.Display);
lgfx::LGFX_Sprite iconWrench(&M5StamPLC.Display);
lgfx::LGFX_Sprite iconGPS(&M5StamPLC.Display);
lgfx::LGFX_Sprite iconCellular(&M5StamPLC.Display);
lgfx::LGFX_Sprite iconLog(&M5StamPLC.Display);
// Larger icons for launcher card
lgfx::LGFX_Sprite iconSatelliteBig(&M5StamPLC.Display);
lgfx::LGFX_Sprite iconGearBig(&M5StamPLC.Display);
lgfx::LGFX_Sprite iconTowerBig(&M5StamPLC.Display);
lgfx::LGFX_Sprite iconWrenchBig(&M5StamPLC.Display);
lgfx::LGFX_Sprite iconGPSBig(&M5StamPLC.Display);
lgfx::LGFX_Sprite iconCellularBig(&M5StamPLC.Display);
lgfx::LGFX_Sprite iconLogBig(&M5StamPLC.Display);
bool iconsInitialized = false;

// Content sprite for card-based pages
lgfx::LGFX_Sprite contentSprite(&M5StamPLC.Display);
bool contentSpriteInit = false;

// Simple page transition tracking: -1 = prev (slide right), 1 = next (slide left), 0 = none
static int8_t g_lastNavDir = 0;
bool g_cntpSyncedThisSession = false; // Used by rtc_manager.cpp
bool g_lastCellularNtpUsedCntp = false; // Used by rtc_manager.cpp

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

// Time utilities moved to system/time_utils.cpp


// UI layout constants moved to ui/ui_constants.h

// =========================================================================
// UI SCROLL STATE
// =========================================================================
// VIEW_HEIGHT and SCROLL_STEP moved to ui/ui_constants.h
// Scroll positions remain global for access by pages and tasks
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

// clampScroll moved to ui/ui_constants.h

// Storage utilities moved to system/storage_utils.cpp

// Crash recovery helper functions
void reduceDisplayRefreshRate() {
    // Reduce display refresh rate to save CPU cycles and memory
    Serial.println("CrashRecovery: Reducing display refresh rate");
}

void reduceLoggingLevel() {
    // Reduce logging to save memory and CPU
    Serial.println("CrashRecovery: Reducing logging level");
}

// ============================================================================
// TASK DECLARATIONS
// ============================================================================
void vTaskButton(void* pvParameters) {
    // Explicit startup logging
    Serial.println("[Button] Task starting...");
    Serial.flush();
    
    const TickType_t kButtonPeriod = pdMS_TO_TICKS(20);
    TickType_t nextWake = xTaskGetTickCount();

    // Long press threshold
    const uint32_t LONG_MS = 500;
    bool aHandled = false, bHandled = false, cHandled = false;
    
    // Diagnostic counters
    static uint32_t stampPLCNullCount = 0;
    static uint32_t notReadyCount = 0;
    static uint32_t buttonNullCount = 0;

    for (;;) {
        if (stampPLC) {
            stampPLC->update();
        } else {
            // Log if stampPLC is null periodically
            if (stampPLCNullCount++ % 250 == 0) { // Every ~5 seconds at 20ms period
                Serial.println("[Button] WARNING: stampPLC is null");
                Serial.flush();
            }
        }

        // Update LVGL button states (critical for LVGL keypad input)
        #if LVGL_UI_TEST_MODE
        ui_update_button_states();
        vTaskDelayUntil(&nextWake, kButtonPeriod);
        continue; // Skip old button handling when LVGL is enabled
        #endif

        if (!stampPLC) {
            if (stampPLCNullCount++ % 250 == 0) {
                Serial.println("[Button] stampPLC null - waiting");
            }
            vTaskDelayUntil(&nextWake, kButtonPeriod);
            continue;
        }
        
        if (!stampPLC->isReady()) {
            if (notReadyCount++ % 250 == 0) {
                Serial.println("[Button] stampPLC not ready - waiting");
            }
            vTaskDelayUntil(&nextWake, kButtonPeriod);
            continue;
        }
        
        auto* stamPLC = stampPLC->getStamPLC();
        if (!stamPLC) {
            if (buttonNullCount++ % 250 == 0) {
                Serial.println("[Button] getStamPLC() returned null - waiting");
                Serial.flush();
            }
            vTaskDelayUntil(&nextWake, kButtonPeriod);
            continue;
        }

        // Get button references - these are member variables, not pointers
        // The address operator & returns a valid pointer, so null checks on btnA/B/C are redundant
        // but kept for defensive programming
        auto* btnA = &stamPLC->BtnA;
        auto* btnB = &stamPLC->BtnB;
        auto* btnC = &stamPLC->BtnC;
        
        // First-time initialization log
        static bool firstRun = true;
        if (firstRun) {
            Serial.println("[Button] Successfully accessing buttons");
            Serial.flush();
            firstRun = false;
        }

        // Determine if we are on landing page (card launcher) or in a detail page
        bool onLanding = (currentPage == DisplayPage::LANDING_PAGE);

        // Wake display on any button press - with null checks
        bool anyPressed = (btnA && btnA->wasPressed()) || 
                          (btnB && btnB->wasPressed()) || 
                          (btnC && btnC->wasPressed());
        if (anyPressed) {
            lastDisplayActivity = millis();
        }

        // If a modal dialog is active, intercept A/C buttons for modal actions
        if (g_modalActive) {
            // Check for button presses with explicit logging
            bool aPressed = btnA && btnA->wasPressed();
            bool cPressed = btnC && btnC->wasPressed();
            
            if (aPressed || cPressed) {
                Serial.printf("[Button] Modal: A=%d C=%d\n", aPressed ? 1 : 0, cPressed ? 1 : 0);
                Serial.flush();
            }
            
            if (aPressed) {
                // Dismiss modal
                Serial.println("[Button] Modal dismissed (A pressed)");
                Serial.flush();
                g_modalActive = false;
                g_modalType = ModalType::NONE;
                enqueueUIEvent(UIEventType::Redraw);
            }
            if (cPressed) {
                // Request immediate retry; leave modal visible
                Serial.println("[Button] Modal retry (C pressed)");
                Serial.flush();
                g_forceCatMRetry = true;
                g_retryToastActive = true;
                g_retryToastUntilMs = millis() + 1500; // show for 1.5s
                // Trigger retry immediately
                tryInitCatMIfAbsent(true);
            }
            vTaskDelayUntil(&nextWake, kButtonPeriod);
            continue; // Don't process normal navigation while modal active
        }

        if (onLanding) {
            // Long-press navigation from landing page
            if (btnA && btnA->pressedFor(LONG_MS) && !aHandled) {
                enqueueUIEvent(UIEventType::GoCELL);
                aHandled = true;
                DEBUG_LOG_BUTTON_PRESS("A", "GoCELL (long press)");
            } else if (btnA && !btnA->isPressed() && currentPage != DisplayPage::SETTINGS_PAGE) { aHandled = false; }
            if (btnB && btnB->pressedFor(LONG_MS) && !bHandled) {
                enqueueUIEvent(UIEventType::GoGNSS);
                bHandled = true;
                DEBUG_LOG_BUTTON_PRESS("B", "GoGNSS (long press)");
            } else if (btnB && !btnB->isPressed() && currentPage != DisplayPage::SETTINGS_PAGE) { bHandled = false; }
            if (btnC && btnC->pressedFor(LONG_MS) && !cHandled) {
                enqueueUIEvent(UIEventType::GoSYS);
                cHandled = true;
                DEBUG_LOG_BUTTON_PRESS("C", "GoSYS (long press)");
            } else if (btnC && !btnC->isPressed() && currentPage != DisplayPage::SETTINGS_PAGE) { cHandled = false; }
        } else {
            // In-page: A=HOME (back). B/C short=Scroll Up/Down. B/C long=Prev/Next page.
            if (btnA && btnA->wasPressed()) {
                enqueueUIEvent(UIEventType::GoLanding);
            }

            // B button: long press = prev page, short press = scroll up
            if (btnB && btnB->pressedFor(LONG_MS) && !bHandled) {
                enqueueUIEvent(UIEventType::PrevPage);
                bHandled = true;
            } else if (btnB && !btnB->isPressed()) {
                bHandled = false;
            } else if (btnB && btnB->wasPressed()) {
                enqueueUIEvent(UIEventType::ScrollUp);
            }

            // C button: long press = next page, short press = scroll down
            if (btnC && btnC->pressedFor(LONG_MS) && !cHandled) {
                enqueueUIEvent(UIEventType::NextPage);
                cHandled = true;
            } else if (btnC && !btnC->isPressed()) {
                cHandled = false;
            } else if (btnC && btnC->wasPressed()) {
                enqueueUIEvent(UIEventType::ScrollDown);
            }

            // Settings page: no special long-press actions beyond above
        }
        
        // Debug every 5s - only use buttons that are within scope
        static uint32_t lastDebugTime = 0;
        if (millis() - lastDebugTime > 5000) {
            Serial.printf("[Button] A:%d B:%d C:%d | page:%d modal:%d | sleep:%s\n",
                          (btnA ? btnA->isPressed() : 0), 
                          (btnB ? btnB->isPressed() : 0), 
                          (btnC ? btnC->isPressed() : 0),
                          (int)currentPage, g_modalActive ? 1 : 0,
                          displayAsleep ? "YES" : "NO");
            Serial.flush();
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
    // Use static variables to minimize stack usage
    static uint32_t lastStackCheck = 0;
    static uint32_t lastFullDraw = 0;
    static bool shouldSleep = false;
    static DisplayPage currentPageLocal = DisplayPage::LANDING_PAGE;
    static bool pageChangedLocal = false;
    static char logBuf[32]; // Static buffer for minimal logging
    
    // Wait for display to initialize
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // Minimal logging - avoid stack-heavy Serial.printf
    strcpy(logBuf, "Display task started");
    Serial.println(logBuf);
    
    // Initialize display activity timer
    lastDisplayActivity = millis();
    
    for (;;) {
        // Check stack usage periodically to detect corruption early
        uint32_t now = millis();
        if (now - lastStackCheck > 10000) { // Check every 10 seconds
            UBaseType_t stackFree = uxTaskGetStackHighWaterMark(NULL);
            if (stackFree < 512) { // Less than 2KB free - dangerous
                snprintf(logBuf, sizeof(logBuf), "Stack low: %u", (unsigned)(stackFree * sizeof(StackType_t)));
                Serial.println(logBuf);
            }
            lastStackCheck = now;
        }

        // Check for display sleep/wake logic
        // Disabled when LVGL UI is active to avoid unintended blanking
        #if !LVGL_UI_TEST_MODE
        if (displaySleepEnabled) {
            shouldSleep = (now - lastDisplayActivity > DISPLAY_SLEEP_TIMEOUT_MS);
            
            if (shouldSleep && !displayAsleep) {
                // Put display to sleep and turn off backlight
                displayAsleep = true;
                M5StamPLC.Display.sleep();
                M5StamPLC.Display.setBrightness(0);
                // Minimal logging to save stack
            } else if (!shouldSleep && displayAsleep) {
                // Wake display up and restore backlight
                displayAsleep = false;
                M5StamPLC.Display.wakeup();
                M5StamPLC.Display.setBrightness(displayBrightness);
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
                // Update local copies to reduce mutex contention
                if (g_uiStateMutex && xSemaphoreTake(g_uiStateMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                    currentPageLocal = currentPage;
                    pageChangedLocal = pageChanged;
                    
                    // Simple event processing to reduce stack usage
                    switch (ev.type) {
                        case UIEventType::GoLanding:
                            currentPageLocal = DisplayPage::LANDING_PAGE;
                            pageChangedLocal = true;
                            g_lastNavDir = 0;
                            break;
                        case UIEventType::GoGNSS:
                            currentPageLocal = DisplayPage::GNSS_PAGE;
                            scrollGNSS = 0;
                            pageChangedLocal = true;
                            g_lastNavDir = 0;
                            break;
                        case UIEventType::GoCELL:
                            currentPageLocal = DisplayPage::CELLULAR_PAGE;
                            scrollCELL = 0;
                            pageChangedLocal = true;
                            g_lastNavDir = 0;
                            break;
                        case UIEventType::GoSYS:
                            currentPageLocal = DisplayPage::SYSTEM_PAGE;
                            scrollSYS = 0;
                            pageChangedLocal = true;
                            g_lastNavDir = 0;
                            break;
                        case UIEventType::GoSETTINGS:
                            currentPageLocal = DisplayPage::SETTINGS_PAGE;
                            scrollSETTINGS = 0;
                            pageChangedLocal = true;
                            g_lastNavDir = 0;
                            break;
                        case UIEventType::ScrollUp:
                            switch (currentPageLocal) {
                                case DisplayPage::GNSS_PAGE:   
                                    scrollGNSS = clampScroll(scrollGNSS - SCROLL_STEP, 122); 
                                    break;
                                case DisplayPage::CELLULAR_PAGE: 
                                    scrollCELL = clampScroll(scrollCELL - SCROLL_STEP, LINE_H2 + LINE_H1 * 11); 
                                    break;
                                case DisplayPage::SYSTEM_PAGE: 
                                    scrollSYS = clampScroll(scrollSYS - SCROLL_STEP, LINE_H2 + LINE_H1 * 16); 
                                    break;
                                case DisplayPage::LOGS_PAGE: {
                                    int16_t contentH = (int16_t)log_count() * LINE_H1 + 8;
                                    if (contentH < (VIEW_HEIGHT + 1)) contentH = VIEW_HEIGHT + 1;
                                    scrollLOGS = clampScroll(scrollLOGS - SCROLL_STEP, contentH);
                                    break;
                                }
                                default: break;
                            }
                            pageChangedLocal = true;
                            break;
                        case UIEventType::ScrollDown:
                            switch (currentPageLocal) {
                                case DisplayPage::GNSS_PAGE:   
                                    scrollGNSS = clampScroll(scrollGNSS + SCROLL_STEP, 122); 
                                    break;
                                case DisplayPage::CELLULAR_PAGE: 
                                    scrollCELL = clampScroll(scrollCELL + SCROLL_STEP, LINE_H2 + LINE_H1 * 11); 
                                    break;
                                case DisplayPage::SYSTEM_PAGE: 
                                    scrollSYS = clampScroll(scrollSYS + SCROLL_STEP, LINE_H2 + LINE_H1 * 16); 
                                    break;
                                case DisplayPage::LOGS_PAGE: {
                                    int16_t contentH = (int16_t)log_count() * LINE_H1 + 8;
                                    if (contentH < (VIEW_HEIGHT + 1)) contentH = VIEW_HEIGHT + 1;
                                    scrollLOGS = clampScroll(scrollLOGS + SCROLL_STEP, contentH);
                                    break;
                                }
                                default: break;
                            }
                            pageChangedLocal = true;
                            break;
                        case UIEventType::PrevPage:
                        case UIEventType::NextPage:
                        case UIEventType::LauncherNext:
                        case UIEventType::LauncherPrev:
                        case UIEventType::LauncherOpen:
                        case UIEventType::Redraw:
                            pageChangedLocal = true;
                            if (ev.type == UIEventType::NextPage || ev.type == UIEventType::LauncherNext) {
                                g_lastNavDir = 1;
                            } else if (ev.type == UIEventType::PrevPage || ev.type == UIEventType::LauncherPrev) {
                                g_lastNavDir = -1;
                            } else {
                                g_lastNavDir = 0;
                            }
                            break;
                        default: break;
                    }
                    
                    // Update globals
                    currentPage = currentPageLocal;
                    pageChanged = pageChangedLocal;
                    
                    xSemaphoreGive(g_uiStateMutex);
                }
            }
        }
        #endif // !LVGL_UI_TEST_MODE

        #if LVGL_UI_TEST_MODE
        // LVGL UI is handled by its own task, just sleep
        vTaskDelay(pdMS_TO_TICKS(100));
        continue;
        #endif

        bool doFullDraw = pageChangedLocal && !displayAsleep; // redraw only on changes and when awake

        if (doFullDraw) {
            // Full redraw on page change or periodic refresh
            M5StamPLC.Display.fillScreen(BLACK);

            // Simple page drawing to reduce stack usage - removed verbose logging
            switch (currentPageLocal) {
                case DisplayPage::LANDING_PAGE:
                    drawLandingPage();
                    break;
                case DisplayPage::GNSS_PAGE:
                    drawGNSSPage();
                    break;
                case DisplayPage::CELLULAR_PAGE:
                    drawCellularPage();
                    break;
                case DisplayPage::SYSTEM_PAGE:
                    drawSystemPage();
                    break;
                case DisplayPage::SETTINGS_PAGE:
                    drawSettingsPage();
                    break;
                case DisplayPage::LOGS_PAGE:
                    drawLogsPage();
                    break;
                default:
                    drawLandingPage();
                    break;
            }

            // Draw overlays after content render
            drawButtonIndicators();
            lastFullDraw = now;

            pageChangedLocal = false;
            pageChanged = false;
            // Removed verbose logging to save stack
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

// RTC synchronization moved to system/time_utils.cpp

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
            Serial.printf("HWM words | Button:%u Display:%u Status:%u PLC:%u CatM:%u Storage:%u\n",
                hwm(buttonTaskHandle), hwm(displayTaskHandle), hwm(statusBarTaskHandle), hwm(stampPLCTaskHandle),
                hwm(catmGnssTaskHandle), hwm(storageTaskHandle));
        }
    }
    #endif // !LVGL_UI_TEST_MODE
}

// ============================================================================
// DISPLAY PAGE FUNCTIONS
// ============================================================================

// drawBootScreen moved to ui/boot_screen.cpp
// drawLandingPage moved to ui/pages/landing_page.cpp
// drawCardBox moved to ui/components/ui_widgets.cpp

// ============================================================================
// RTC TIME MANAGEMENT FUNCTIONS
// ============================================================================
// RTC functions moved to system/rtc_manager.cpp:
// - setRTCFromCellular()
// - setRTCFromGPS()
// - setRTCFromBuildTimestamp()
// - getRTCTime()

// Helper function to set custom font if available
void setCustomFont(lgfx::LGFX_Sprite* s, int size = 1) {
    s->setTextSize(size);
    // Custom font will be used if loaded, otherwise falls back to default
}

// drawGNSSPage moved to ui/pages/gnss_page.cpp

// drawCellularPage moved to ui/pages/cellular_page.cpp

// drawSystemPage moved to ui/pages/system_page.cpp

// drawSettingsPage moved to ui/pages/settings_page.cpp

// drawLogsPage moved to ui/pages/logs_page.cpp

// drawButtonIndicators moved to ui/components/ui_widgets.cpp
// drawSDInfo moved to ui/components/ui_widgets.cpp
// drawCompactSignalBar moved to ui/components/ui_widgets.cpp
// drawSignalBar moved to ui/components/ui_widgets.cpp
// drawWiFiBar moved to ui/components/ui_widgets.cpp

void drawStatusBar() {
    // Status bar removed; intentionally left blank.
}


// ============================================================================
// RTC TIME MANAGEMENT FUNCTIONS
// ============================================================================
// RTC functions moved to system/rtc_manager.cpp:
// - setRTCFromCellular()
// - setRTCFromGPS()
// - setRTCFromBuildTimestamp()
// - getRTCTime()

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================
// getNetworkType moved to modules/catm_gnss/network_utils.cpp

// Icon management functions moved to ui/components/icon_manager.cpp

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
    // Initialize serial early and ensure it's ready
    Serial.begin(115200);  // Use explicit 115200 baud for compatibility
    delay(1000);  // Longer delay to ensure serial is ready
    Serial.flush();  // Clear any buffered output
    
    // Test serial output immediately
    Serial.println("\n\n=== StampPLC CatM+GNSS Integration ===");
    Serial.println("Serial monitor initialized @ 115200 baud");
    Serial.flush();
    delay(100);  // Give serial time to send
    
    Serial.println("Initializing...");
    Serial.flush();
    
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

    // Initialize crash recovery system
    drawBootScreen("Initializing crash recovery", 15);
    g_crashRecovery = CrashRecovery::getInstance();
    if (!g_crashRecovery->begin()) {
        Serial.println("ERROR: Failed to initialize crash recovery");
        drawBootScreen("Crash recovery FAILED", 15, false);
        delay(2000);
        return;
    }
    g_crashRecovery->startRecovery();
    Serial.println("Crash recovery system started");

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
        Serial.println("WARNING: Thermal sensor (LM75B) initialization failed - I2C communication failed");
        drawBootScreen("Thermal sensor FAILED", 40, false);
        delay(800);
    }

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
        Serial.println("WARNING: Power monitor (INA226) initialization failed - I2C communication failed");
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
    drawBootScreen("Initializing StampPLC", 55);
    Serial.println("Initializing StampPLC wrapper...");
    Serial.flush();
    stampPLC = new BasicStampPLC();
    if (!stampPLC) {
        Serial.println("ERROR: Failed to allocate StampPLC wrapper");
        drawBootScreen("StampPLC alloc FAILED", 55, false);
        delay(1000);
    } else if (stampPLC->begin()) {
        Serial.println("StampPLC initialized successfully");
        Serial.printf("StampPLC ready: %s\n", stampPLC->isReady() ? "YES" : "NO");
        Serial.flush();
    } else {
        Serial.println("WARNING: StampPLC begin() failed - buttons may not work");
        Serial.flush();
        // Don't delete - keep it so button task can retry
        // delete stampPLC;
        // stampPLC = nullptr;
    }
    
    // Initialize SD module (optional)
    drawBootScreen("Initializing SD card", 60);
#if ENABLE_SD
    sdModule = new SDCardModule();
    if (sdModule->begin()) {
        Serial.println("SD card detected and mounted");
        // Print detailed status
        sdModule->printStatus();
        // Quick smoke test: log boot marker (only if write access is available)
        // Don't crash if write fails - card may be read-only
        String bootMsg = String("Boot @ ") + String((uint32_t)millis()) + " ms\n";
        if (sdModule->writeText("/boot.log", bootMsg.c_str(), true)) {
            log_add("SD card mounted (read/write)");
        } else {
            Serial.println("SDCardModule: WARNING - Write test failed, card may be read-only");
            log_add("SD card mounted (read-only)");
        }
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
    Serial.flush(); // Ensure log is written before potential crash
    yield(); // Feed watchdog
    // Create UI queue
    g_uiQueue = xQueueCreate(16, sizeof(UIEvent));
    if (g_uiQueue == NULL) {
        Serial.println("ERROR: Failed to create UI queue");
        Serial.flush();
        drawBootScreen("UI queue FAILED", 65, false);
        delay(2000);
        return;
    }
    Serial.println("DEBUG: UI queue created");
    Serial.flush();

    Serial.println("DEBUG: Creating UI state mutex");
    Serial.flush();
    yield(); // Feed watchdog
    // Create UI state mutex
    g_uiStateMutex = xSemaphoreCreateMutex();
    if (g_uiStateMutex == NULL) {
        Serial.println("ERROR: Failed to create UI state mutex");
        Serial.flush();
        drawBootScreen("UI mutex FAILED", 65, false);
        delay(2000);
        return;
    }
    Serial.println("DEBUG: UI state mutex created");
    Serial.flush();

    Serial.println("DEBUG: Creating system event group");
    Serial.flush();
    yield(); // Feed watchdog
    // Create system event group
    xEventGroupSystemStatus = xEventGroupCreate();
    if (xEventGroupSystemStatus == NULL) {
        Serial.println("ERROR: Failed to create system event group");
        Serial.flush();
        drawBootScreen("Event group FAILED", 65, false);
        delay(2000);
        return;
    }
    Serial.println("DEBUG: System event group created");
    Serial.flush();

    Serial.println("DEBUG: Initializing logging");
    Serial.flush();
    yield(); // Feed watchdog
    // Init logging
    log_init();
    log_add("Booting StampPLC CatM+GNSS...");
    Serial.println("DEBUG: Logging initialized");
    Serial.flush();

    Serial.println("DEBUG: Creating storage task");
    Serial.flush();
    yield(); // Feed watchdog
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
    if (storageTaskHandle == NULL) {
        Serial.println("ERROR: Failed to create storage task");
        Serial.flush();
        drawBootScreen("Storage task FAILED", 68, false);
        delay(2000);
        return;
    }
    Serial.println("Storage task created");
    Serial.flush();
    yield(); // Feed watchdog after task creation
#else
    Serial.println("Storage task disabled (SD disabled)");
#endif
    
    // Initialize CatM+GNSS Module
    drawBootScreen("Initializing CatM+GNSS", 70);
    Serial.println("========================================");
    Serial.println("Starting CatM+GNSS module initialization...");
    Serial.flush();
    delay(50);  // Give serial time to send
    yield(); // Feed watchdog before long init
    
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
    
    Serial.println("CatM+GNSS module allocated, calling begin()...");
    yield(); // Feed watchdog
    
    if (catmGnssModule->begin()) {
        Serial.println("========================================");
        Serial.println("CatM+GNSS module initialized successfully");
        Serial.flush();
        log_add("CatM+GNSS initialized");
        g_commFailureDescription = "";
    } else {
        Serial.println("========================================");
        Serial.println("CatM+GNSS module initialization failed");
        Serial.printf("Error: %s\n", catmGnssModule->getLastError().c_str());
        Serial.flush();
        delay(100);  // Give serial time to send error message
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

// Web server functionality removed - not required for this project

    // Create FreeRTOS tasks with proper error handling
    Serial.println("DEBUG: Creating FreeRTOS tasks");
    Serial.flush();
    yield(); // Feed watchdog

    // Button task (Core 0)
    BaseType_t buttonResult = xTaskCreatePinnedToCore(
        vTaskButton,
        "Button",
        TASK_STACK_SIZE_BUTTON_HANDLER,
        NULL,
        TASK_PRIORITY_BUTTON_HANDLER,
        &buttonTaskHandle,
        0
    );
    if (buttonResult != pdPASS) {
        Serial.println("ERROR: Failed to create button task");
        drawBootScreen("Button task FAILED", 75, false);
        delay(2000);
        return;
    }
    Serial.println("Button task created");

    // Display task (Core 1)
    BaseType_t displayResult = xTaskCreatePinnedToCore(
        vTaskDisplay,
        "Display",
        TASK_STACK_SIZE_DISPLAY,
        NULL,
        TASK_PRIORITY_DISPLAY,
        &displayTaskHandle,
        1
    );
    if (displayResult != pdPASS) {
        Serial.println("ERROR: Failed to create display task");
        drawBootScreen("Display task FAILED", 80, false);
        delay(2000);
        return;
    }
    Serial.println("Display task created");

    // Status bar task (Core 1)
    BaseType_t statusResult = xTaskCreatePinnedToCore(
        vTaskStatusBar,
        "StatusBar",
        TASK_STACK_SIZE_SYSTEM_MONITOR,
        NULL,
        TASK_PRIORITY_SYSTEM_MONITOR,
        &statusBarTaskHandle,
        1
    );
    if (statusResult != pdPASS) {
        Serial.println("ERROR: Failed to create status bar task");
        drawBootScreen("Status bar task FAILED", 85, false);
        delay(2000);
        return;
    }
    Serial.println("Status bar task created");

    // StampPLC task (Core 0)
    BaseType_t plcResult = xTaskCreatePinnedToCore(
        vTaskStampPLC,
        "StampPLC",
        TASK_STACK_SIZE_INDUSTRIAL_IO,
        NULL,
        TASK_PRIORITY_INDUSTRIAL_IO,
        &stampPLCTaskHandle,
        0
    );
    if (plcResult != pdPASS) {
        Serial.println("ERROR: Failed to create StampPLC task");
        drawBootScreen("StampPLC task FAILED", 90, false);
        delay(2000);
        return;
    }
    Serial.println("StampPLC task created");

    // CatM+GNSS task (Core 0)
    BaseType_t catmResult = xTaskCreatePinnedToCore(
        vTaskCatMGNSS,
        "CatMGNSS",
        TASK_STACK_SIZE_APP_GNSS,
        NULL,
        TASK_PRIORITY_GNSS,
        &catmGnssTaskHandle,
        0
    );
    if (catmResult != pdPASS) {
        Serial.println("ERROR: Failed to create CatMGNSS task");
        drawBootScreen("CatMGNSS task FAILED", 95, false);
        delay(2000);
        return;
    }
    Serial.println("CatMGNSS task created");


    // Boot completed successfully
    drawBootScreen("System Ready", 100, true);
    Serial.println("=== System initialization complete ===");
    log_add("System ready");
    delay(1000);
}

void loop() {
    // FreeRTOS handles all tasks, this loop is intentionally empty
    vTaskDelay(pdMS_TO_TICKS(1000));
}
