/*
 * Cellular Page Implementation
 * Modern themed cellular network status and statistics
 */

#include <M5StamPLC.h>
#include "cellular_page.h"
#include "../ui_constants.h"
#include "../theme.h"
#include "../components/icon_manager.h"
#include "../../modules/catm_gnss/catm_gnss_module.h"
#include "../components/ui_widgets.h"

// External globals
extern CatMGNSSModule* catmGnssModule;
extern volatile int16_t scrollCELL;

// ═══════════════════════════════════════════════════════════════════════════
// Content height for scroll calculations
// Layout: Title + Network(5 rows) + Device(5 rows) + Data(2 rows)
// ═══════════════════════════════════════════════════════════════════════════
static constexpr int16_t CELL_CONTENT_ROWS = 12;
static constexpr int16_t CELL_CONTENT_PAD = 8;

int16_t cellularPageContentHeight() {
    return LINE_H2 + (LINE_H1 * CELL_CONTENT_ROWS) + CELL_CONTENT_PAD;
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
// HELPER: Draw modern signal strength indicator
// ═══════════════════════════════════════════════════════════════════════════
static void drawSignalIndicator(int16_t x, int16_t y, int8_t dbm, bool connected) {
    const auto& th = ui::theme();
    auto& d = M5StamPLC.Display;

    // Calculate signal percentage and bars
    int percent = constrain(map(dbm, -120, -50, 0, 100), 0, 100);
    int bars = (percent + 19) / 20;  // 0-5 bars

    // Background pill
    d.fillRoundRect(x, y, 60, 14, 4, th.cardAlt);

    // Signal bars
    uint16_t barColor = connected ?
        (percent > 60 ? th.green : (percent > 30 ? th.yellow : th.red)) : th.textMuted;

    for (int i = 0; i < 5; i++) {
        int barH = 4 + i * 2;
        int barX = x + 4 + i * 6;
        int barY = y + 12 - barH;
        uint16_t col = (i < bars && connected) ? barColor : th.borderSubtle;
        d.fillRect(barX, barY, 4, barH, col);
    }

    // dBm text
    char buf[12];
    snprintf(buf, sizeof(buf), "%d", dbm);
    d.setTextColor(th.textSecondary, th.cardAlt);
    d.setCursor(x + 36, y + 3);
    d.print(buf);
}

// ═══════════════════════════════════════════════════════════════════════════
// MAIN: Draw Cellular Page
// ═══════════════════════════════════════════════════════════════════════════
void drawCellularPage() {
    const auto& th = ui::theme();
    auto& d = M5StamPLC.Display;
    int16_t yOff = scrollCELL;

    // ─── Page Title with Icon ───
    drawIconCellularDirect(COL1_X, CONTENT_TOP - yOff + 2, 14, th.accent);
    d.setTextColor(th.accent, th.bg);
    d.setTextSize(2);
    d.setCursor(COL1_X + 18, CONTENT_TOP - yOff);
    d.print("Cellular");

    // Check module status
    bool hasData = catmGnssModule && catmGnssModule->isModuleInitialized();

    if (!hasData) {
        // Module not initialized - show error state
        int centerY = CONTENT_TOP + 40 - yOff;
        d.fillRoundRect(COL1_X, centerY, UI_DISPLAY_W - 2 * COL1_X, 36, th.radius, th.redDim);
        d.drawRoundRect(COL1_X, centerY, UI_DISPLAY_W - 2 * COL1_X, 36, th.radius, th.red);
        d.setTextColor(th.red, th.redDim);
        d.setTextSize(1);
        d.setCursor(COL1_X + 8, centerY + 8);
        d.print("Cellular module not initialized");
        d.setTextColor(th.textSecondary, th.redDim);
        d.setCursor(COL1_X + 8, centerY + 22);
        d.print("Check CatM unit connection");
        return;
    }

    CellularData data = catmGnssModule->getCellularData();

    // Status badge next to title
    const char* statusText = data.isConnected ? "ONLINE" : "OFFLINE";
    uint16_t statusColor = data.isConnected ? th.green : th.red;
    int titleW = 18 + d.textWidth("Cellular");  // Account for icon
    d.fillRoundRect(COL1_X + titleW + 8, CONTENT_TOP - yOff + 2, 52, 14, 4,
                    data.isConnected ? th.greenDim : th.redDim);
    d.setTextSize(1);
    d.setTextColor(statusColor, th.bg);
    d.setCursor(COL1_X + titleW + 12, CONTENT_TOP - yOff + 5);
    d.print(statusText);

    // Signal indicator (top right)
    drawSignalIndicator(UI_DISPLAY_W - 68, CONTENT_TOP - yOff, data.signalStrength, data.isConnected);

    d.setTextSize(1);
    int16_t y = CONTENT_TOP + LINE_H2 + 4 - yOff;
    char buf[64];

    // ─── Network Section ───
    drawSectionHeader("Network", COL1_X, y, 120);
    y += LINE_H1 + 2;

    // Operator
    drawDataRow("Operator", data.operatorName.c_str(), COL1_X, y, th.text);
    y += LINE_H1;

    // Signal strength
    snprintf(buf, sizeof(buf), "%d dBm", data.signalStrength);
    uint16_t sigColor = data.signalStrength > -80 ? th.green :
                        (data.signalStrength > -100 ? th.yellow : th.red);
    drawDataRow("Signal", buf, COL1_X, y, sigColor);
    y += LINE_H1;

    // Connection status
    drawDataRow("Status", data.isConnected ? "Connected" : "Disconnected",
                COL1_X, y, data.isConnected ? th.green : th.red);
    y += LINE_H1 + 4;

    // ─── Device Section ───
    drawSectionHeader("Device", COL1_X, y, 120);
    y += LINE_H1 + 2;

    // IMEI
    drawDataRow("IMEI", data.imei.c_str(), COL1_X, y, th.textSecondary);
    y += LINE_H1;

    // Errors
    snprintf(buf, sizeof(buf), "%d", data.errorCount);
    drawDataRow("Errors", buf, COL1_X, y, data.errorCount > 0 ? th.red : th.textSecondary);
    y += LINE_H1;

    // Last update
    uint32_t updateAge = (millis() - data.lastUpdate) / 1000;
    snprintf(buf, sizeof(buf), "%us ago", updateAge);
    drawDataRow("Updated", buf, COL1_X, y, updateAge < 10 ? th.green : th.yellow);
    y += LINE_H1 + 4;

    // ─── Data Section ───
    drawSectionHeader("Data Transfer", COL1_X, y, 120);
    y += LINE_H1 + 2;

    // TX
    if (data.txBytes > 1024 * 1024) {
        snprintf(buf, sizeof(buf), "%.1f MB", data.txBytes / (1024.0f * 1024.0f));
    } else if (data.txBytes > 1024) {
        snprintf(buf, sizeof(buf), "%.1f KB", data.txBytes / 1024.0f);
    } else {
        snprintf(buf, sizeof(buf), "%llu B", data.txBytes);
    }
    drawDataRow("TX", buf, COL1_X, y, th.cyan);
    y += LINE_H1;

    // RX
    if (data.rxBytes > 1024 * 1024) {
        snprintf(buf, sizeof(buf), "%.1f MB", data.rxBytes / (1024.0f * 1024.0f));
    } else if (data.rxBytes > 1024) {
        snprintf(buf, sizeof(buf), "%.1f KB", data.rxBytes / 1024.0f);
    } else {
        snprintf(buf, sizeof(buf), "%llu B", data.rxBytes);
    }
    drawDataRow("RX", buf, COL1_X, y, th.cyan);
}
