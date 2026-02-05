/*
 * Logs Page Implementation
 * Modern themed system log viewer with severity coloring
 */

#include <M5StamPLC.h>
#include "logs_page.h"
#include "../ui_constants.h"
#include "../theme.h"
#include "../components/icon_manager.h"
#include "../../modules/logging/log_buffer.h"
#include <cstring>

// External globals
extern volatile int16_t scrollLOGS;

// ═══════════════════════════════════════════════════════════════════════════
// Content height for scroll calculations
// This is dynamic based on log_count()
// ═══════════════════════════════════════════════════════════════════════════
static constexpr int16_t LOGS_PAD = 8;

int16_t logsPageContentHeight() {
    size_t cnt = log_count();
    // Title + log lines + padding
    return LINE_H2 + (int16_t)(cnt * LINE_H1) + LOGS_PAD;
}

// ═══════════════════════════════════════════════════════════════════════════
// HELPER: Detect log severity from line content
// ═══════════════════════════════════════════════════════════════════════════
enum class LogSeverity { INFO, WARNING, ERROR, DEBUG };

static LogSeverity detectSeverity(const char* line) {
    // Check for common severity patterns
    if (strstr(line, "ERROR") || strstr(line, "error") ||
        strstr(line, "FAIL") || strstr(line, "fail")) {
        return LogSeverity::ERROR;
    }
    if (strstr(line, "WARNING") || strstr(line, "warning") ||
        strstr(line, "WARN") || strstr(line, "warn")) {
        return LogSeverity::WARNING;
    }
    if (strstr(line, "DEBUG") || strstr(line, "debug") ||
        strstr(line, "[D]")) {
        return LogSeverity::DEBUG;
    }
    return LogSeverity::INFO;
}

// ═══════════════════════════════════════════════════════════════════════════
// HELPER: Get color for severity
// ═══════════════════════════════════════════════════════════════════════════
static uint16_t getSeverityColor(LogSeverity sev) {
    const auto& th = ui::theme();
    switch (sev) {
        case LogSeverity::ERROR:   return th.red;
        case LogSeverity::WARNING: return th.yellow;
        case LogSeverity::DEBUG:   return th.textMuted;
        default:                   return th.text;
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// HELPER: Get dim background for severity (for highlighting important logs)
// ═══════════════════════════════════════════════════════════════════════════
static uint16_t getSeverityBg(LogSeverity sev) {
    const auto& th = ui::theme();
    switch (sev) {
        case LogSeverity::ERROR:   return th.redDim;
        case LogSeverity::WARNING: return th.yellowDim;
        default:                   return th.bg;
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// HELPER: Draw severity indicator dot
// ═══════════════════════════════════════════════════════════════════════════
static void drawSeverityDot(int16_t x, int16_t y, LogSeverity sev) {
    const auto& th = ui::theme();
    uint16_t col = getSeverityColor(sev);

    // Small filled circle for errors/warnings
    if (sev == LogSeverity::ERROR || sev == LogSeverity::WARNING) {
        M5StamPLC.Display.fillCircle(x + 2, y + 3, 2, col);
    } else {
        // Subtle dot for info/debug
        M5StamPLC.Display.drawPixel(x + 2, y + 3, th.textMuted);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// HELPER: Truncate line with ellipsis if too long
// ═══════════════════════════════════════════════════════════════════════════
static void drawTruncatedLine(const char* line, int16_t x, int16_t y, int16_t maxWidth,
                              uint16_t color, uint16_t bgColor) {
    auto& d = M5StamPLC.Display;
    d.setTextColor(color, bgColor);

    int lineLen = strlen(line);
    int textWidth = d.textWidth(line);

    if (textWidth <= maxWidth) {
        // Fits - draw directly
        d.setCursor(x, y);
        d.print(line);
    } else {
        // Need to truncate with ellipsis
        // Find approximate cutoff
        static char truncBuf[128];
        int cutoff = (lineLen * maxWidth) / textWidth - 3;
        if (cutoff < 0) cutoff = 0;
        if (cutoff > (int)sizeof(truncBuf) - 4) cutoff = sizeof(truncBuf) - 4;

        strncpy(truncBuf, line, cutoff);
        truncBuf[cutoff] = '\0';
        strcat(truncBuf, "...");

        d.setCursor(x, y);
        d.print(truncBuf);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// MAIN: Draw Logs Page
// ═══════════════════════════════════════════════════════════════════════════
void drawLogsPage() {
    const auto& th = ui::theme();
    auto& d = M5StamPLC.Display;
    int16_t yOff = scrollLOGS;

    // ─── Page Title with Icon ───
    drawIconLogDirect(COL1_X, CONTENT_TOP - yOff + 2, 14, th.accent);
    d.setTextColor(th.accent, th.bg);
    d.setTextSize(2);
    d.setCursor(COL1_X + 18, CONTENT_TOP - yOff);
    d.print("Logs");

    // Log count badge
    size_t cnt = log_count();
    char cntBuf[16];
    snprintf(cntBuf, sizeof(cntBuf), "%u", (unsigned)cnt);
    int titleW = 18 + d.textWidth("Logs");  // Account for icon
    d.fillRoundRect(COL1_X + titleW + 8, CONTENT_TOP - yOff + 2, 32, 14, 4, th.cardAlt);
    d.setTextSize(1);
    d.setTextColor(th.textSecondary, th.cardAlt);
    d.setCursor(COL1_X + titleW + 12, CONTENT_TOP - yOff + 5);
    d.print(cntBuf);

    // Calculate scroll bounds
    const int pad = 2;
    int contentH = (int)cnt * LINE_H1 + pad * 2;
    scrollLOGS = clampScroll(scrollLOGS, contentH);

    // Content area bounds
    const int contentLeft = COL1_X;
    const int contentRight = UI_DISPLAY_W - COL1_X;
    const int contentWidth = contentRight - contentLeft - 8;  // Account for severity dot

    // Empty state
    if (cnt == 0) {
        int centerY = CONTENT_TOP + 40 - yOff;
        d.setTextColor(th.textMuted, th.bg);
        d.setCursor(COL1_X, centerY);
        d.print("No log entries");
        return;
    }

    d.setTextSize(1);

    // Draw log lines
    int y = CONTENT_TOP + LINE_H2 - yOff;
    char line[160];

    for (size_t i = 0; i < cnt; ++i) {
        // Only draw visible lines
        if (y > CONTENT_TOP - LINE_H1 && y < CONTENT_BOTTOM) {
            if (log_get_line(i, line, sizeof(line))) {
                LogSeverity sev = detectSeverity(line);

                // Background highlight for errors/warnings
                uint16_t bgCol = getSeverityBg(sev);
                if (sev == LogSeverity::ERROR || sev == LogSeverity::WARNING) {
                    d.fillRect(contentLeft - 2, y - 1, contentWidth + 12, LINE_H1, bgCol);
                }

                // Severity indicator
                drawSeverityDot(contentLeft, y, sev);

                // Log text (truncated if needed)
                uint16_t textCol = getSeverityColor(sev);
                drawTruncatedLine(line, contentLeft + 8, y, contentWidth, textCol, bgCol);
            }
        }
        y += LINE_H1;
    }

    // Scroll indicator (if content overflows)
    if (contentH > (CONTENT_BOTTOM - CONTENT_TOP)) {
        // Draw scroll track on right edge
        int trackH = CONTENT_BOTTOM - CONTENT_TOP - LINE_H2;
        int trackY = CONTENT_TOP + LINE_H2;
        int thumbH = max(10, (trackH * trackH) / contentH);
        int thumbY = trackY + ((trackH - thumbH) * scrollLOGS) / (contentH - trackH);

        // Track
        d.fillRect(UI_DISPLAY_W - 4, trackY, 2, trackH, th.borderSubtle);

        // Thumb
        d.fillRoundRect(UI_DISPLAY_W - 5, thumbY, 4, thumbH, 2, th.accent);
    }
}
