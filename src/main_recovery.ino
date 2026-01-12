/*
 * GENPLC Recovery Main
 * 
 * Simplified main application for emergency stabilization
 * Addresses mutex contention, memory pressure, and task starvation
 * 
 * Created: November 2025
 * Purpose: Emergency stabilization of production system
 */

#include <Arduino.h>
#include <M5StamPLC.h>
#include <M5GFX.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <esp_log.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <SD.h>
#include <FS.h>
#include <sys/time.h>
#include <Preferences.h>

// Essential includes only
#include "include/debug_system.h"
#include "include/memory_monitor.h"
#include "include/ui_utils.h"
#include "system/crash_recovery.h"
#include "system/memory_safety.h"
#include "system/rtc_manager.h"
#include "system/time_utils.h"
#include "system/storage_utils.h"
#include "hardware/basic_stamplc.h"
#include "modules/catm_gnss/catm_gnss_module.h"
#include "modules/catm_gnss/catm_gnss_task.h"
#include "config/system_config.h"
#include "ui/theme.h"
#include "ui/components.h"
#include "ui/ui_constants.h"
#include "ui/ui_types.h"
#include "ui/pages/landing_page.h"
#include "ui/pages/gnss_page.h"
#include "ui/pages/cellular_page.h"
#include "ui/pages/system_page.h"
#include "ui/boot_screen.h"

// ============================================================================
// RECOVERY CONFIGURATION
// ============================================================================
// Simplified task architecture
#define RECOVERY_TASK_COUNT 3
#define MAX_RETRY_ATTEMPTS 3
#define MEMORY_EMERGENCY_THRESHOLD 1024  // 1KB free words (4KB)
#define WATCHDOG_TIMEOUT_MS 30000

// ============================================================================
// GLOBAL VARIABLES (Minimized)
// ============================================================================
// Module instances (essential only)
BasicStampPLC* stampPLC = nullptr;
CatMGNSSModule* catmGnssModule = nullptr;

// Task handles (simplified)
TaskHandle_t mainTaskHandle = nullptr;
TaskHandle_t communicationTaskHandle = nullptr;
TaskHandle_t watchdogTaskHandle = nullptr;

// System state (simplified)
enum class RecoveryState {
    INITIALIZING,
    RUNNING,
    CELLULAR_ISSUE,
    MEMORY_ISSUE,
    SYSTEM_ERROR,
    RECOVERY_MODE
};
volatile RecoveryState currentState = RecoveryState::INITIALIZING;

// Essential UI state
volatile DisplayPage currentPage = DisplayPage::LANDING_PAGE;
volatile bool pageChanged = true;
volatile uint32_t lastActivity = 0;

// Error tracking (simplified)
struct ErrorState {
    uint32_t cellularFailures = 0;
    uint32_t memoryWarnings = 0;
    uint32_t systemErrors = 0;
    uint32_t lastResetTime = 0;
};
static ErrorState errorState;

// Communication state (no shared mutexes)
struct CommState {
    bool cellularConnected = false;
    bool gnssEnabled = false;
    uint32_t lastCellularCheck = 0;
    uint32_t lastGnssCheck = 0;
    String lastError = "";
};
static CommState commState;

// ============================================================================
// MEMORY MANAGEMENT (Emergency)
// ============================================================================
namespace MemoryManager {
    void checkEmergencyMemory() {
        UBaseType_t freeHeap = uxTaskGetStackHighWaterMark(NULL);
        if (freeHeap < MEMORY_EMERGENCY_THRESHOLD) {
            Serial.printf("EMERGENCY: Low memory detected: %u words\n", freeHeap);
            errorState.memoryWarnings++;
            
            // Emergency memory recovery
            if (errorState.memoryWarnings > 5) {
                Serial.println("EMERGENCY: Triggering system reset for memory recovery");
                ESP.restart();
            }
        }
    }
    
    void logMemoryUsage() {
        static uint32_t lastLog = 0;
        if (millis() - lastLog > 30000) { // Every 30 seconds
            lastLog = millis();
            
            size_t freeHeap = ESP.getFreeHeap();
            size_t minFreeHeap = ESP.getMinFreeHeap();
            UBaseType_t taskStack = uxTaskGetStackHighWaterMark(NULL);
            
            Serial.printf("Memory: Heap=%u, Min=%u, Stack=%u\n", 
                        freeHeap, minFreeHeap, taskStack * sizeof(StackType_t));
        }
    }
}

// ============================================================================
// SIMPLIFIED TASK FUNCTIONS
// ============================================================================

// Main Task: UI, Buttons, StampPLC, System Management
void vTaskMainRecovery(void* pvParameters) {
    Serial.println("[Main] Recovery task started");
    
    const TickType_t taskDelay = pdMS_TO_TICKS(50); // 20Hz update rate
    TickType_t lastWake = xTaskGetTickCount();
    
    for (;;) {
        // Memory monitoring (every cycle)
        MemoryManager::checkEmergencyMemory();
        MemoryManager::logMemoryUsage();
        
        // Update StampPLC if available
        if (stampPLC && stampPLC->isReady()) {
            stampPLC->update();
            
            // Simple button handling (no complex UI events)
            auto* plc = stampPLC->getStamPLC();
            if (plc) {
                bool btnA = plc->BtnA.wasPressed();
                bool btnB = plc->BtnB.wasPressed();
                bool btnC = plc->BtnC.wasPressed();
                
                // Update activity timestamp
                if (btnA || btnB || btnC) {
                    lastActivity = millis();
                }
                
                // Simple navigation
                if (btnA) {
                    currentPage = DisplayPage::LANDING_PAGE;
                    pageChanged = true;
                }
                if (btnB && currentPage != DisplayPage::LANDING_PAGE) {
                    currentPage = (DisplayPage)((int)currentPage - 1);
                    if (currentPage < DisplayPage::LANDING_PAGE) {
                        currentPage = DisplayPage::SYSTEM_PAGE;
                    }
                    pageChanged = true;
                }
                if (btnC) {
                    currentPage = (DisplayPage)((int)currentPage + 1);
                    if (currentPage > DisplayPage::SYSTEM_PAGE) {
                        currentPage = DisplayPage::LANDING_PAGE;
                    }
                    pageChanged = true;
                }
            }
        }
        
        // UI updates (only when page changes)
        if (pageChanged && !M5StamPLC.Display.isSleep()) {
            // Simple page rendering
            M5StamPLC.Display.fillScreen(BLACK);
            
            switch (currentPage) {
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
                default:
                    drawLandingPage();
                    break;
            }
            
            // Draw simple status
            M5StamPLC.Display.setCursor(0, 0);
            M5StamPLC.Display.setTextColor(WHITE);
            M5StamPLC.Display.printf("State: %d | Cell: %s | Mem: %u", 
                                  (int)currentState,
                                  commState.cellularConnected ? "OK" : "FAIL",
                                  ESP.getFreeHeap());
            
            pageChanged = false;
        }
        
        vTaskDelayUntil(&lastWake, taskDelay);
    }
}

// Communication Task: Cellular, GNSS, Storage (isolated from UI)
void vTaskCommunicationRecovery(void* pvParameters) {
    Serial.println("[Comm] Recovery communication task started");
    
    const TickType_t taskDelay = pdMS_TO_TICKS(1000); // 1Hz update
    TickType_t lastWake = xTaskGetTickCount();
    
    for (;;) {
        // Simple cellular status check
        if (catmGnssModule && millis() - commState.lastCellularCheck > 5000) {
            commState.lastCellularCheck = millis();
            
            bool wasConnected = commState.cellularConnected;
            commState.cellularConnected = catmGnssModule->isModuleInitialized();
            
            if (wasConnected && !commState.cellularConnected) {
                errorState.cellularFailures++;
                Serial.printf("[Comm] Cellular connection lost (failure #%u)\n", 
                           errorState.cellularFailures);
                
                if (errorState.cellularFailures > 3) {
                    currentState = RecoveryState::CELLULAR_ISSUE;
                }
            }
        }
        
        vTaskDelayUntil(&lastWake, taskDelay);
    }
}

// Watchdog Task: System monitoring and recovery
void vTaskWatchdogRecovery(void* pvParameters) {
    Serial.println("[Watchdog] Recovery watchdog task started");
    
    const TickType_t taskDelay = pdMS_TO_TICKS(5000); // 5Hz check
    TickType_t lastWake = xTaskGetTickCount();
    
    for (;;) {
        // Check if main task is responsive
        uint32_t timeSinceActivity = millis() - lastActivity;
        if (timeSinceActivity > WATCHDOG_TIMEOUT_MS) {
            Serial.printf("[Watchdog] Main task unresponsive for %lu ms\n", timeSinceActivity);
            
            // Attempt recovery
            if (timeSinceActivity > WATCHDOG_TIMEOUT_MS * 2) {
                Serial.println("[Watchdog] Forced system reset");
                ESP.restart();
            }
        }
        
        vTaskDelayUntil(&lastWake, taskDelay);
    }
}

// ============================================================================
// SETUP AND LOOP
// ============================================================================

void setup() {
    // Initialize serial
    Serial.begin(115200);
    delay(2000);
    Serial.println("\n=== GENPLC Recovery Mode ===");
    Serial.println("Emergency stabilization build");
    
    // Suppress noisy logs
    esp_log_level_set("vfs_api", ESP_LOG_NONE);
    esp_log_level_set("vfs", ESP_LOG_NONE);
    esp_log_level_set("sd_diskio", ESP_LOG_NONE);
    esp_log_level_set("Wire", ESP_LOG_ERROR);
    
    // Initialize M5StamPLC (minimal)
    M5StamPLC.begin();
    M5StamPLC.Display.setBrightness(128);
    M5StamPLC.Display.fillScreen(BLACK);
    M5StamPLC.Display.setCursor(0, 0);
    M5StamPLC.Display.setTextColor(WHITE);
    M5StamPLC.Display.println("GENPLC Recovery Mode");
    M5StamPLC.Display.println("Initializing...");
    
    // Initialize essential modules only
    Serial.println("Initializing essential modules...");
    
    // StampPLC
    stampPLC = new BasicStampPLC();
    if (stampPLC && stampPLC->begin()) {
        Serial.println("StampPLC initialized");
    } else {
        Serial.println("StampPLC initialization failed");
        delete stampPLC;
        stampPLC = nullptr;
    }
    
    // CatM+GNSS (with error handling)
    catmGnssModule = new CatMGNSSModule();
    if (catmGnssModule && catmGnssModule->begin()) {
        Serial.println("CatM+GNSS initialized");
    } else {
        Serial.println("CatM+GNSS initialization failed");
        delete catmGnssModule;
        catmGnssModule = nullptr;
        currentState = RecoveryState::CELLULAR_ISSUE;
    }
    
    // Create simplified task architecture
    Serial.println("Creating recovery tasks...");
    
    // Main task (Core 1) - UI, buttons, system management
    BaseType_t result1 = xTaskCreatePinnedToCore(
        vTaskMainRecovery,
        "MainRecovery",
        8192,  // 32KB stack - conservative
        nullptr,
        2,      // Medium priority
        &mainTaskHandle,
        1       // Core 1
    );
    
    // Communication task (Core 0) - Cellular, GNSS, storage
    BaseType_t result2 = xTaskCreatePinnedToCore(
        vTaskCommunicationRecovery,
        "CommRecovery", 
        6144,  // 24KB stack - conservative
        nullptr,
        3,      // High priority for communication
        &communicationTaskHandle,
        0       // Core 0
    );
    
    // Watchdog task (Core 0) - System monitoring
    BaseType_t result3 = xTaskCreatePinnedToCore(
        vTaskWatchdogRecovery,
        "WatchdogRecovery",
        4096,  // 16KB stack - minimal
        nullptr,
        4,      // Highest priority
        &watchdogTaskHandle,
        0       // Core 0
    );
    
    if (result1 != pdPASS || result2 != pdPASS || result3 != pdPASS) {
        Serial.println("FATAL: Failed to create recovery tasks");
        ESP.restart();
    }
    
    Serial.println("Recovery tasks created successfully");
    currentState = RecoveryState::RUNNING;
    
    M5StamPLC.Display.fillScreen(BLACK);
    M5StamPLC.Display.setCursor(0, 0);
    M5StamPLC.Display.println("GENPLC Recovery Mode");
    M5StamPLC.Display.println("System Running");
    M5StamPLC.Display.println("A: Home | B: Prev | C: Next");
    
    // Initialize activity tracking
    lastActivity = millis();
}

void loop() {
    // Main loop is empty - FreeRTOS handles everything
    vTaskDelay(pdMS_TO_TICKS(1000));
}
