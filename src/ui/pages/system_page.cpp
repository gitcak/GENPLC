/*
 * System Page Implementation
 * System status, memory, hardware sensors
 */

#include "system_page.h"
#include "../ui_constants.h"
#include "../theme.h"
#include "../../hardware/basic_stamplc.h"
#include "../../modules/catm_gnss/catm_gnss_module.h"
#include "../../modules/storage/sd_card_module.h"
#include "../components/ui_widgets.h"
#include <ESP.h>

// External globals
extern BasicStampPLC* stampPLC;
extern CatMGNSSModule* catmGnssModule;
extern SDCardModule* sdModule;
extern volatile int16_t scrollSYS;

void drawSystemPage() {
    const auto& th = ui::theme();
    int16_t yOffset = scrollSYS;
    
    // Title with theme
    M5StamPLC.Display.setTextColor(th.text, th.bg);
    M5StamPLC.Display.setTextSize(2);
    M5StamPLC.Display.setCursor(COL1_X, CONTENT_TOP - yOffset);
    M5StamPLC.Display.println("System Status");
    
    // System info - compact layout
    M5StamPLC.Display.setTextSize(1);
    
    // Memory section header
    M5StamPLC.Display.setTextColor(th.yellow, th.bg);
    M5StamPLC.Display.setCursor(COL1_X, CONTENT_TOP + LINE_H2 - yOffset);
    M5StamPLC.Display.println("Memory:");
    
    // Memory info with color coding
    uint32_t freeHeap = ESP.getFreeHeap();
    uint32_t totalHeap = ESP.getHeapSize();
    uint8_t memoryPercent = (totalHeap > 0) ? (uint8_t)((totalHeap - freeHeap) * 100 / totalHeap) : 0;
    
    M5StamPLC.Display.setTextColor(memoryPercent > 80 ? th.red : (memoryPercent > 60 ? th.yellow : th.textSecondary), th.bg);
    M5StamPLC.Display.setCursor(COL1_X, CONTENT_TOP + LINE_H1 - yOffset);
    M5StamPLC.Display.printf("Free RAM: %d KB", freeHeap / 1024);
    
    M5StamPLC.Display.setCursor(COL1_X, CONTENT_TOP + LINE_H1 * 2 - yOffset);
    M5StamPLC.Display.printf("Total RAM: %d KB", totalHeap / 1024);
    
    M5StamPLC.Display.setCursor(COL1_X, CONTENT_TOP + LINE_H1 * 3 - yOffset);
    M5StamPLC.Display.printf("CPU Freq: %d MHz", ESP.getCpuFreqMHz());
    
    // WiFi connection bar
    // Extra spacing before WiFi bar
    drawWiFiBar(COL1_X, CONTENT_TOP + LINE_H1 * 5 - yOffset, 120, 12);
    
    // Module status section with theme colors
    M5StamPLC.Display.setTextColor(th.yellow, th.bg);
    M5StamPLC.Display.setCursor(COL1_X, CONTENT_TOP + LINE_H1 * 7 - yOffset);
    M5StamPLC.Display.println("Module Status:");
    
    // StampPLC status with color coding
    bool stampReady = stampPLC && stampPLC->isReady();
    M5StamPLC.Display.setTextColor(stampReady ? th.green : th.red, th.bg);
    M5StamPLC.Display.setCursor(COL1_X, CONTENT_TOP + LINE_H1 * 8 - yOffset);
    M5StamPLC.Display.printf("StampPLC: %s", stampReady ? "READY" : "NOT READY");
    
    // CatM+GNSS status with color coding
    bool catmReady = catmGnssModule && catmGnssModule->isModuleInitialized();
    M5StamPLC.Display.setTextColor(catmReady ? th.green : th.red, th.bg);
    M5StamPLC.Display.setCursor(COL1_X, CONTENT_TOP + LINE_H1 * 9 - yOffset);
    M5StamPLC.Display.printf("CatM+GNSS: %s", catmReady ? "READY" : "NOT READY");
    
    // SD card status with color coding
    bool sdReady = sdModule && sdModule->isMounted();
    M5StamPLC.Display.setTextColor(sdReady ? th.green : th.red, th.bg);
    M5StamPLC.Display.setCursor(COL1_X, CONTENT_TOP + LINE_H1 * 10 - yOffset);
    M5StamPLC.Display.printf("SD: %s", sdReady ? "MOUNTED" : "NOT PRESENT");
    if (sdReady) {
        drawSDInfo(COL2_X, CONTENT_TOP + LINE_H1 * 9 - yOffset);
    }
    
    // Hardware sensor data section
    M5StamPLC.Display.setTextColor(th.yellow, th.bg);
    M5StamPLC.Display.setCursor(COL1_X, CONTENT_TOP + LINE_H1 * 12 - yOffset);
    M5StamPLC.Display.println("Hardware Sensors:");
    
    // Temperature with color coding for warnings
    float temperature = M5StamPLC.getTemp();
    bool tempWarning = (temperature < -10.0f || temperature > 60.0f);
    M5StamPLC.Display.setTextColor(tempWarning ? th.red : th.text, th.bg);
    M5StamPLC.Display.setCursor(COL1_X, CONTENT_TOP + LINE_H1 * 13 - yOffset);
    M5StamPLC.Display.printf("Temperature: %.1f°C", temperature);
    
    // Power monitoring with color coding for warnings
    float voltage = M5StamPLC.INA226.getBusVoltage();
    float current = M5StamPLC.INA226.getShuntCurrent();
    bool powerWarning = (voltage < 3.0f || voltage > 5.5f || current < -1.0f || current > 2.0f);
    M5StamPLC.Display.setTextColor(powerWarning ? th.yellow : th.textSecondary, th.bg);
    M5StamPLC.Display.setCursor(COL1_X, CONTENT_TOP + LINE_H1 * 14 - yOffset);
    M5StamPLC.Display.printf("Power: %.2fV, %.3fA", voltage, current);
    
    // RTC status with color coding
    struct tm rtcTime;
    bool rtcValid = false;
    if (M5StamPLC.RX8130.begin()) {
        M5StamPLC.RX8130.getTime(&rtcTime);
        rtcValid = (rtcTime.tm_year >= 100); // Valid if year > 2000
    }
    M5StamPLC.Display.setTextColor(rtcValid ? th.green : th.red, th.bg);
    M5StamPLC.Display.setCursor(COL1_X, CONTENT_TOP + LINE_H1 * 15 - yOffset);
    M5StamPLC.Display.printf("RTC: %s", rtcValid ? "VALID" : "NOT SET");
    
    // Uptime - moved up
    // Removed uptime per request
}
