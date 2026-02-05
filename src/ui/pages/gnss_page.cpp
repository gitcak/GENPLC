/*
 * GNSS Page Implementation
 * Modern themed GNSS status and position data display
 */

#include <M5StamPLC.h>
#include "gnss_page.h"
#include "../ui_constants.h"
#include "../theme.h"
#include "../components/icon_manager.h"
#include "../../modules/catm_gnss/catm_gnss_module.h"

// External globals
extern CatMGNSSModule* catmGnssModule;
extern volatile int16_t scrollGNSS;

// ═══════════════════════════════════════════════════════════════════════════
// Content height for scroll calculations
// Layout: Title + Position(4 rows) + Motion(3 rows) + padding
// ═══════════════════════════════════════════════════════════════════════════
static constexpr int16_t GNSS_CONTENT_ROWS = 9;  // Title + sections
static constexpr int16_t GNSS_CONTENT_PAD = 8;   // Extra padding

int16_t gnssPageContentHeight() {
    return LINE_H2 + (LINE_H1 * GNSS_CONTENT_ROWS) + GNSS_CONTENT_PAD;
}

// ═══════════════════════════════════════════════════════════════════════════
// HELPER: Draw a mini section header with accent line
// ═══════════════════════════════════════════════════════════════════════════
static void drawSectionHeader(const char* label, int16_t x, int16_t y, int16_t width) {
    const auto& th = ui::theme();
    auto& d = M5StamPLC.Display;

    // Section label
    d.setTextColor(th.sectionHeader, th.bg);
    d.setTextSize(1);
    d.setCursor(x, y);
    d.print(label);

    // Subtle underline extending to width
    int labelW = d.textWidth(label);
    d.drawFastHLine(x + labelW + 4, y + 4, width - labelW - 8, th.sectionLine);
}

// ═══════════════════════════════════════════════════════════════════════════
// HELPER: Draw a data row (label + value) with consistent styling
// ═══════════════════════════════════════════════════════════════════════════
static void drawDataRow(const char* label, const char* value, int16_t x, int16_t y,
                        uint16_t valueColor, bool highlight = false) {
    const auto& th = ui::theme();
    auto& d = M5StamPLC.Display;

    // Optional highlight background for important values
    if (highlight) {
        int w = d.textWidth(label) + d.textWidth(value) + 8;
        d.fillRoundRect(x - 2, y - 1, w + 4, 10, th.radiusSmall, th.cardAlt);
    }

    // Label (muted)
    d.setTextColor(th.textSecondary, th.bg);
    d.setCursor(x, y);
    d.print(label);

    // Value (colored)
    d.setTextColor(valueColor, th.bg);
    d.setCursor(x + 56, y);  // Fixed column for alignment
    d.print(value);
}

// ═══════════════════════════════════════════════════════════════════════════
// HELPER: Draw satellite signal indicator (modern mini bar)
// ═══════════════════════════════════════════════════════════════════════════
static void drawSatIndicator(int16_t x, int16_t y, uint8_t sats, bool hasLock) {
    const auto& th = ui::theme();
    auto& d = M5StamPLC.Display;

    // Background pill
    d.fillRoundRect(x, y, 44, 14, 4, th.cardAlt);

    // Satellite count
    char buf[8];
    snprintf(buf, sizeof(buf), "%u", sats);
    d.setTextColor(hasLock ? th.green : th.yellow, th.cardAlt);
    d.setTextSize(1);
    d.setCursor(x + 4, y + 3);
    d.print(buf);

    // Mini signal bars (4 bars)
    int barX = x + 20;
    int barBaseY = y + 11;
    uint16_t barColor = hasLock ? th.green : (sats > 0 ? th.yellow : th.textMuted);
    for (int i = 0; i < 4; i++) {
        int barH = 3 + i * 2;
        int fillThreshold = (i + 1) * 3;  // Show bar if sats >= threshold
        uint16_t col = (sats >= fillThreshold) ? barColor : th.borderSubtle;
        d.fillRect(barX + i * 5, barBaseY - barH, 3, barH, col);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// MAIN: Draw GNSS Page
// ═══════════════════════════════════════════════════════════════════════════
void drawGNSSPage() {
    const auto& th = ui::theme();
    auto& d = M5StamPLC.Display;
    int16_t yOff = scrollGNSS;

    // ─── Page Title with Icon ───
    drawIconGPSDirect(COL1_X, CONTENT_TOP - yOff + 2, 14, th.accent);
    d.setTextColor(th.accent, th.bg);
    d.setTextSize(2);
    d.setCursor(COL1_X + 18, CONTENT_TOP - yOff);
    d.print("GNSS");

    // Lock status indicator (inline with title)
    bool hasData = catmGnssModule && catmGnssModule->isModuleInitialized();
    GNSSData data = {};
    if (hasData) {
        data = catmGnssModule->getGNSSData();
    }

    // Status badge next to title
    const char* statusText = !hasData ? "OFFLINE" : (data.isValid ? "LOCK" : "SEARCH");
    uint16_t statusColor = !hasData ? th.red : (data.isValid ? th.green : th.yellow);
    int titleW = 18 + d.textWidth("GNSS");  // Account for icon
    d.fillRoundRect(COL1_X + titleW + 8, CONTENT_TOP - yOff + 2, 50, 14, 4,
                    !hasData ? th.redDim : (data.isValid ? th.greenDim : th.yellowDim));
    d.setTextSize(1);
    d.setTextColor(statusColor, th.bg);
    d.setCursor(COL1_X + titleW + 12, CONTENT_TOP - yOff + 5);
    d.print(statusText);

    // Satellite indicator (top right)
    if (hasData) {
        drawSatIndicator(UI_DISPLAY_W - 52, CONTENT_TOP - yOff, data.satellites, data.isValid);
    }

    d.setTextSize(1);

    if (!hasData) {
        // Module not initialized - show error state
        int centerY = CONTENT_TOP + 40 - yOff;
        d.fillRoundRect(COL1_X, centerY, UI_DISPLAY_W - 2 * COL1_X, 36, th.radius, th.redDim);
        d.drawRoundRect(COL1_X, centerY, UI_DISPLAY_W - 2 * COL1_X, 36, th.radius, th.red);
        d.setTextColor(th.red, th.redDim);
        d.setCursor(COL1_X + 8, centerY + 8);
        d.print("GNSS module not initialized");
        d.setTextColor(th.textSecondary, th.redDim);
        d.setCursor(COL1_X + 8, centerY + 22);
        d.print("Check CatM unit connection");
        return;
    }

    int16_t y = CONTENT_TOP + LINE_H2 + 4 - yOff;
    char buf[32];

    // ─── Position Section ───
    drawSectionHeader("Position", COL1_X, y, 120);
    y += LINE_H1 + 2;

    if (data.isValid) {
        snprintf(buf, sizeof(buf), "%.6f", data.latitude);
        drawDataRow("Lat", buf, COL1_X, y, th.text);
        y += LINE_H1;

        snprintf(buf, sizeof(buf), "%.6f", data.longitude);
        drawDataRow("Lon", buf, COL1_X, y, th.text);
        y += LINE_H1;

        snprintf(buf, sizeof(buf), "%.1fm", data.altitude);
        drawDataRow("Alt", buf, COL1_X, y, th.textSecondary);
        y += LINE_H1;
    } else {
        d.setTextColor(th.yellow, th.bg);
        d.setCursor(COL1_X, y);
        d.print("Acquiring fix...");
        y += LINE_H1 * 3;
    }

    // ─── Motion Section ───
    drawSectionHeader("Motion", COL1_X, y, 120);
    y += LINE_H1 + 2;

    if (data.isValid) {
        snprintf(buf, sizeof(buf), "%.1f km/h", data.speed);
        drawDataRow("Spd", buf, COL1_X, y, data.speed > 5.0 ? th.cyan : th.textSecondary);
        y += LINE_H1;

        snprintf(buf, sizeof(buf), "%.0f deg", data.course);
        drawDataRow("Hdg", buf, COL1_X, y, th.textSecondary);
        y += LINE_H1;
    } else {
        d.setTextColor(th.textMuted, th.bg);
        d.setCursor(COL1_X, y);
        d.print("--");
        y += LINE_H1 * 2;
    }

    // ─── UTC Time Section (right column) ───
    int16_t colR = COL1_X + 125;
    int16_t yR = CONTENT_TOP + LINE_H2 + 4 - yOff;

    drawSectionHeader("UTC Time", colR, yR, 100);
    yR += LINE_H1 + 2;

    if (data.isValid) {
        // Date
        snprintf(buf, sizeof(buf), "%04d-%02d-%02d", data.year, data.month, data.day);
        d.setTextColor(th.cyan, th.bg);
        d.setCursor(colR, yR);
        d.print(buf);
        yR += LINE_H1;

        // Time (larger, highlighted)
        snprintf(buf, sizeof(buf), "%02d:%02d:%02d", data.hour, data.minute, data.second);
        d.fillRoundRect(colR - 2, yR - 1, 56, 12, 3, th.cyanDim);
        d.setTextColor(th.text, th.cyanDim);
        d.setCursor(colR, yR);
        d.print(buf);
        yR += LINE_H1 + 4;

        // Fix age
        uint32_t fixAgeSec = 0;
        if (data.lastUpdate != 0) {
            fixAgeSec = (millis() - data.lastUpdate) / 1000U;
        }
        snprintf(buf, sizeof(buf), "%us ago", fixAgeSec);
        d.setTextColor(fixAgeSec < 5 ? th.green : th.yellow, th.bg);
        d.setCursor(colR, yR);
        d.print(buf);
    } else {
        d.setTextColor(th.textMuted, th.bg);
        d.setCursor(colR, yR);
        d.print("No time data");
    }
}
