/*
 * System Page Implementation
 * Modern themed system status, memory, and hardware sensors display
 */

#include <Arduino.h>
#include <M5StamPLC.h>
#include "system_page.h"
#include "../ui_constants.h"
#include "../theme.h"
#include "../components/icon_manager.h"
#include "../../hardware/basic_stamplc.h"
#include "../../modules/catm_gnss/catm_gnss_module.h"
#include "../../modules/storage/sd_card_module.h"
#include "../components/ui_widgets.h"
#include <Esp.h>

// External globals
extern BasicStampPLC* stampPLC;
extern CatMGNSSModule* catmGnssModule;
extern SDCardModule* sdModule;
extern volatile int16_t scrollSYS;

// ═══════════════════════════════════════════════════════════════════════════
// Content height for scroll calculations
// Layout: Title + Memory(4 rows) + Modules(4 rows) + Sensors(4 rows)
// ═══════════════════════════════════════════════════════════════════════════
static constexpr int16_t SYS_CONTENT_ROWS = 16;
static constexpr int16_t SYS_CONTENT_PAD = 12;

int16_t systemPageContentHeight() {
    return LINE_H2 + (LINE_H1 * SYS_CONTENT_ROWS) + SYS_CONTENT_PAD;
}

// ═══════════════════════════════════════════════════════════════════════════
// HELPER: Draw section header with accent line
// ═══════════════════════════════════════════════════════════════════════════
static void drawSectionHeader(const char* label, int16_t x, int16_t y, int16_t width) {
    const auto& th = ui::theme();
    auto& d = M5StamPLC.Display;

    d.setTextColor(th.sectionHeader, th.bg);
    d.setTextSize(1);
    d.setCursor(x, y);
    d.print(label);

    int labelW = d.textWidth(label);
    d.drawFastHLine(x + labelW + 4, y + 4, width - labelW - 8, th.sectionLine);
}

// ═══════════════════════════════════════════════════════════════════════════
// HELPER: Draw a data row with label and value
// ═══════════════════════════════════════════════════════════════════════════
static void drawDataRow(const char* label, const char* value, int16_t x, int16_t y,
                        uint16_t valueColor) {
    const auto& th = ui::theme();
    auto& d = M5StamPLC.Display;

    d.setTextColor(th.textSecondary, th.bg);
    d.setCursor(x, y);
    d.print(label);

    d.setTextColor(valueColor, th.bg);
    d.setCursor(x + 70, y);
    d.print(value);
}

// ═══════════════════════════════════════════════════════════════════════════
// HELPER: Draw memory usage bar
// ═══════════════════════════════════════════════════════════════════════════
static void drawMemoryBar(int16_t x, int16_t y, int16_t w, int16_t h,
                          uint32_t used, uint32_t total) {
    const auto& th = ui::theme();
    auto& d = M5StamPLC.Display;

    // Background
    d.fillRoundRect(x, y, w, h, 3, th.barBg);
    d.drawRoundRect(x, y, w, h, 3, th.borderSubtle);

    if (total == 0) return;

    // Calculate percentage
    int percent = (int)((used * 100ULL) / total);
    percent = constrain(percent, 0, 100);

    // Color based on usage
    uint16_t fillColor = (percent < 60) ? th.green :
                         (percent < 80) ? th.yellow : th.red;

    // Fill bar
    int fillW = ((w - 4) * percent) / 100;
    if (fillW > 0) {
        d.fillRoundRect(x + 2, y + 2, fillW, h - 4, 2, fillColor);
    }

    // Percentage text (right-aligned)
    char buf[8];
    snprintf(buf, sizeof(buf), "%d%%", percent);
    d.setTextColor(th.text, th.bg);
    d.setCursor(x + w + 4, y + (h - 8) / 2);
    d.print(buf);
}

// ═══════════════════════════════════════════════════════════════════════════
// HELPER: Draw status pill (READY/ERROR style badge)
// ═══════════════════════════════════════════════════════════════════════════
static void drawStatusPill(int16_t x, int16_t y, const char* text, bool ok) {
    const auto& th = ui::theme();
    auto& d = M5StamPLC.Display;

    uint16_t bgColor = ok ? th.greenDim : th.redDim;
    uint16_t textColor = ok ? th.green : th.red;

    int textW = d.textWidth(text);
    d.fillRoundRect(x, y, textW + 8, 12, 3, bgColor);
    d.setTextColor(textColor, bgColor);
    d.setCursor(x + 4, y + 2);
    d.print(text);
}

// ═══════════════════════════════════════════════════════════════════════════
// MAIN: Draw System Page
// ═══════════════════════════════════════════════════════════════════════════
void drawSystemPage() {
    const auto& th = ui::theme();
    auto& d = M5StamPLC.Display;
    int16_t yOff = scrollSYS;

    // ─── Page Title with Icon ───
    drawIconGearDirect(COL1_X, CONTENT_TOP - yOff + 2, 14, th.accent);
    d.setTextColor(th.accent, th.bg);
    d.setTextSize(2);
    d.setCursor(COL1_X + 18, CONTENT_TOP - yOff);
    d.print("System");

    d.setTextSize(1);
    int16_t y = CONTENT_TOP + LINE_H2 + 4 - yOff;
    char buf[48];

    // ─── Memory Section ───
    drawSectionHeader("Memory", COL1_X, y, 120);
    y += LINE_H1 + 2;

    uint32_t freeHeap = ESP.getFreeHeap();
    uint32_t totalHeap = ESP.getHeapSize();
    uint32_t usedHeap = totalHeap - freeHeap;

    // Memory bar
    drawMemoryBar(COL1_X, y, 100, 12, usedHeap, totalHeap);
    y += LINE_H1 + 6;

    // Free RAM
    snprintf(buf, sizeof(buf), "%.1f KB", freeHeap / 1024.0f);
    uint16_t memColor = (freeHeap > 50000) ? th.green :
                        (freeHeap > 20000) ? th.yellow : th.red;
    drawDataRow("Free", buf, COL1_X, y, memColor);
    y += LINE_H1;

    // CPU frequency
    snprintf(buf, sizeof(buf), "%d MHz", ESP.getCpuFreqMHz());
    drawDataRow("CPU", buf, COL1_X, y, th.textSecondary);
    y += LINE_H1 + 4;

    // ─── Module Status Section ───
    drawSectionHeader("Modules", COL1_X, y, 120);
    y += LINE_H1 + 2;

    // StampPLC
    bool stampReady = stampPLC && stampPLC->isReady();
    d.setTextColor(th.textSecondary, th.bg);
    d.setCursor(COL1_X, y);
    d.print("PLC");
    drawStatusPill(COL1_X + 50, y - 1, stampReady ? "READY" : "ERROR", stampReady);
    y += LINE_H1;

    // CatM+GNSS
    bool catmReady = catmGnssModule && catmGnssModule->isModuleInitialized();
    d.setTextColor(th.textSecondary, th.bg);
    d.setCursor(COL1_X, y);
    d.print("CatM");
    drawStatusPill(COL1_X + 50, y - 1, catmReady ? "READY" : "OFFLINE", catmReady);
    y += LINE_H1;

    // SD Card
    bool sdReady = sdModule && sdModule->isMounted();
    d.setTextColor(th.textSecondary, th.bg);
    d.setCursor(COL1_X, y);
    d.print("SD");
    drawStatusPill(COL1_X + 50, y - 1, sdReady ? "MOUNTED" : "NO CARD", sdReady);

    // SD info on right side if mounted
    if (sdReady) {
        uint64_t total = sdModule->totalBytes();
        uint64_t used = sdModule->usedBytes();
        float freeMB = (total - used) / (1024.0f * 1024.0f);
        snprintf(buf, sizeof(buf), "%.1f MB free", freeMB);
        d.setTextColor(th.textSecondary, th.bg);
        d.setCursor(COL2_X, y);
        d.print(buf);
    }
    y += LINE_H1 + 4;

    // ─── Hardware Sensors Section ───
    drawSectionHeader("Sensors", COL1_X, y, 120);
    y += LINE_H1 + 2;

    // Temperature
    float temperature = M5StamPLC.getTemp();
    bool tempWarning = (temperature < -10.0f || temperature > 60.0f);
    snprintf(buf, sizeof(buf), "%.1f C", temperature);
    drawDataRow("Temp", buf, COL1_X, y, tempWarning ? th.red : th.text);
    y += LINE_H1;

    // Power monitoring
    float voltage = M5StamPLC.INA226.getBusVoltage();
    float current = M5StamPLC.INA226.getShuntCurrent();
    bool powerWarning = (voltage < 3.0f || voltage > 5.5f);

    snprintf(buf, sizeof(buf), "%.2fV", voltage);
    drawDataRow("Volt", buf, COL1_X, y, powerWarning ? th.yellow : th.textSecondary);

    // Current on right side
    snprintf(buf, sizeof(buf), "%.0fmA", current * 1000);
    d.setTextColor(th.textSecondary, th.bg);
    d.setCursor(COL2_X, y);
    d.print(buf);
    y += LINE_H1;

    // RTC status
    struct tm rtcTime;
    bool rtcValid = false;
    if (M5StamPLC.RX8130.begin()) {
        M5StamPLC.RX8130.getTime(&rtcTime);
        rtcValid = (rtcTime.tm_year >= 100);  // Valid if year > 2000
    }
    d.setTextColor(th.textSecondary, th.bg);
    d.setCursor(COL1_X, y);
    d.print("RTC");
    drawStatusPill(COL1_X + 50, y - 1, rtcValid ? "VALID" : "NOT SET", rtcValid);

    // Time on right side if valid
    if (rtcValid) {
        snprintf(buf, sizeof(buf), "%02d:%02d:%02d",
                 rtcTime.tm_hour, rtcTime.tm_min, rtcTime.tm_sec);
        d.setTextColor(th.cyan, th.bg);
        d.setCursor(COL2_X, y);
        d.print(buf);
    }
}
