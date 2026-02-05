/*
 * Settings Page Implementation
 * Interactive settings with brightness, sleep, and system info
 */

#include <M5StamPLC.h>
#include "settings_page.h"
#include "../ui_constants.h"
#include "../theme.h"
#include "../components/icon_manager.h"
#include "../../modules/storage/sd_card_module.h"
#include "../../modules/settings/settings_store.h"
#include "../../config/system_config.h"
#include "../components/ui_widgets.h"
#include <Esp.h>

// External globals
extern SDCardModule* sdModule;
extern volatile int16_t scrollSETTINGS;

// Display settings (shared with main.cpp via extern)
extern uint8_t displayBrightness;
extern bool displaySleepEnabled;
extern uint32_t displaySleepTimeoutMs;

// Currently selected settings item
static SettingsItem s_selectedItem = SettingsItem::BRIGHTNESS;

// ═══════════════════════════════════════════════════════════════════════════
// Settings item selection
// ═══════════════════════════════════════════════════════════════════════════
SettingsItem settingsGetSelected() {
    return s_selectedItem;
}

void settingsSetSelected(SettingsItem item) {
    if (item < SettingsItem::ITEM_COUNT) {
        s_selectedItem = item;
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Adjust the selected setting value
// ═══════════════════════════════════════════════════════════════════════════
void settingsAdjustValue(int8_t direction) {
    switch (s_selectedItem) {
        case SettingsItem::BRIGHTNESS: {
            // Adjust brightness in steps of 25 (10, 35, 60, 85, 110, 135, 160, 185, 210, 235, 255)
            int16_t newBright = displayBrightness + (direction * 25);
            if (newBright < 10) newBright = 10;
            if (newBright > 255) newBright = 255;
            displayBrightness = (uint8_t)newBright;
            M5StamPLC.Display.setBrightness(displayBrightness);
            // Save to NVS
            displaySettingsSave(displayBrightness, displaySleepEnabled,
                               (uint16_t)(displaySleepTimeoutMs / 1000));
            break;
        }
        case SettingsItem::SLEEP_TOGGLE: {
            // Toggle sleep enable
            displaySleepEnabled = !displaySleepEnabled;
            displaySettingsSave(displayBrightness, displaySleepEnabled,
                               (uint16_t)(displaySleepTimeoutMs / 1000));
            break;
        }
        case SettingsItem::SLEEP_TIMEOUT: {
            // Adjust timeout in 30s steps (30s to 10min)
            int32_t newTimeout = (int32_t)displaySleepTimeoutMs + (direction * 30000);
            if (newTimeout < 30000) newTimeout = 30000;      // Min 30s
            if (newTimeout > 600000) newTimeout = 600000;    // Max 10min
            displaySleepTimeoutMs = (uint32_t)newTimeout;
            displaySettingsSave(displayBrightness, displaySleepEnabled,
                               (uint16_t)(displaySleepTimeoutMs / 1000));
            break;
        }
        default:
            break;
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Content height for scroll calculations
// ═══════════════════════════════════════════════════════════════════════════
static constexpr int16_t SETTINGS_ROW_H = 18;
static constexpr int16_t SETTINGS_SECTION_GAP = 6;

int16_t settingsPageContentHeight() {
    // Title + Display section (3 items) + System section (4 items) + padding
    return LINE_H2 + 4 + (SETTINGS_ROW_H * 7) + (SETTINGS_SECTION_GAP * 2) + 12;
}

// ═══════════════════════════════════════════════════════════════════════════
// HELPER: Draw section header
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
// HELPER: Draw a settings row with selection highlight
// ═══════════════════════════════════════════════════════════════════════════
static void drawSettingsRow(const char* label, const char* value, int16_t x, int16_t y,
                            int16_t width, bool selected, uint16_t valueColor = 0) {
    const auto& th = ui::theme();
    auto& d = M5StamPLC.Display;

    if (valueColor == 0) valueColor = th.text;

    // Selection highlight background
    if (selected) {
        d.fillRoundRect(x - 2, y - 2, width + 4, SETTINGS_ROW_H, 3, th.accentDim);
        d.drawRoundRect(x - 2, y - 2, width + 4, SETTINGS_ROW_H, 3, th.accent);
    }

    // Label
    d.setTextSize(1);
    d.setTextColor(th.textSecondary, selected ? th.accentDim : th.bg);
    d.setCursor(x, y + 2);
    d.print(label);

    // Value (right-aligned)
    int valueW = d.textWidth(value);
    d.setTextColor(selected ? th.accent : valueColor, selected ? th.accentDim : th.bg);
    d.setCursor(x + width - valueW - 4, y + 2);
    d.print(value);

    // Adjustment arrows if selected
    if (selected) {
        d.setTextColor(th.accent, th.accentDim);
        d.setCursor(x + width - valueW - 14, y + 2);
        d.print("<");
        d.setCursor(x + width - 2, y + 2);
        d.print(">");
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// HELPER: Draw a static info row (non-selectable)
// ═══════════════════════════════════════════════════════════════════════════
static void drawInfoRow(const char* label, const char* value, int16_t x, int16_t y,
                        int16_t width, uint16_t valueColor = 0) {
    const auto& th = ui::theme();
    auto& d = M5StamPLC.Display;

    if (valueColor == 0) valueColor = th.textSecondary;

    d.setTextSize(1);
    d.setTextColor(th.textMuted, th.bg);
    d.setCursor(x, y + 2);
    d.print(label);

    int valueW = d.textWidth(value);
    d.setTextColor(valueColor, th.bg);
    d.setCursor(x + width - valueW, y + 2);
    d.print(value);
}

// ═══════════════════════════════════════════════════════════════════════════
// MAIN: Draw Settings Page
// ═══════════════════════════════════════════════════════════════════════════
void drawSettingsPage() {
    const auto& th = ui::theme();
    auto& d = M5StamPLC.Display;
    int16_t yOff = scrollSETTINGS;

    const int16_t contentWidth = UI_DISPLAY_W - 2 * COL1_X;

    // ─── Page Title with Icon ───
    drawIconSettingsDirect(COL1_X, CONTENT_TOP - yOff + 2, 14, th.accent);
    d.setTextColor(th.accent, th.bg);
    d.setTextSize(2);
    d.setCursor(COL1_X + 18, CONTENT_TOP - yOff);
    d.print("Settings");

    int16_t y = CONTENT_TOP + LINE_H2 + 4 - yOff;
    char buf[32];

    // ═══════════════════════════════════════════════════════════════════════
    // Display Section
    // ═══════════════════════════════════════════════════════════════════════
    drawSectionHeader("Display", COL1_X, y, contentWidth);
    y += LINE_H1 + 2;

    // Brightness
    snprintf(buf, sizeof(buf), "%d%%", (displayBrightness * 100) / 255);
    drawSettingsRow("Brightness", buf, COL1_X, y, contentWidth,
                    s_selectedItem == SettingsItem::BRIGHTNESS, th.cyan);
    y += SETTINGS_ROW_H;

    // Sleep enable
    drawSettingsRow("Auto Sleep", displaySleepEnabled ? "ON" : "OFF", COL1_X, y, contentWidth,
                    s_selectedItem == SettingsItem::SLEEP_TOGGLE,
                    displaySleepEnabled ? th.green : th.red);
    y += SETTINGS_ROW_H;

    // Sleep timeout
    uint32_t sleepSec = displaySleepTimeoutMs / 1000;
    if (sleepSec >= 60) {
        snprintf(buf, sizeof(buf), "%um %us", (unsigned)(sleepSec / 60), (unsigned)(sleepSec % 60));
    } else {
        snprintf(buf, sizeof(buf), "%us", (unsigned)sleepSec);
    }
    drawSettingsRow("Sleep After", buf, COL1_X, y, contentWidth,
                    s_selectedItem == SettingsItem::SLEEP_TIMEOUT, th.textSecondary);
    y += SETTINGS_ROW_H + SETTINGS_SECTION_GAP;

    // ═══════════════════════════════════════════════════════════════════════
    // System Info Section
    // ═══════════════════════════════════════════════════════════════════════
    drawSectionHeader("System", COL1_X, y, contentWidth);
    y += LINE_H1 + 2;

    // Firmware version
    drawInfoRow("Firmware", STAMPLC_VERSION, COL1_X, y, contentWidth, th.cyan);
    y += SETTINGS_ROW_H;

    // Free heap
    uint32_t freeHeap = ESP.getFreeHeap();
    snprintf(buf, sizeof(buf), "%.1f KB", freeHeap / 1024.0f);
    uint16_t heapColor = (freeHeap > 50000) ? th.green :
                         (freeHeap > 20000) ? th.yellow : th.red;
    drawInfoRow("Free Heap", buf, COL1_X, y, contentWidth, heapColor);
    y += SETTINGS_ROW_H;

    // SD Card status
    bool sdMounted = sdModule && sdModule->isMounted();
    if (sdMounted) {
        uint64_t total = sdModule->totalBytes();
        uint64_t used = sdModule->usedBytes();
        float freeMB = (total - used) / (1024.0f * 1024.0f);
        snprintf(buf, sizeof(buf), "%.1f MB free", freeMB);
        drawInfoRow("SD Card", buf, COL1_X, y, contentWidth, th.green);
    } else {
        drawInfoRow("SD Card", "Not mounted", COL1_X, y, contentWidth, th.red);
    }
    y += SETTINGS_ROW_H;

    // Uptime
    uint32_t uptimeSec = millis() / 1000;
    uint32_t hours = uptimeSec / 3600;
    uint32_t mins = (uptimeSec % 3600) / 60;
    snprintf(buf, sizeof(buf), "%uh %um", hours, mins);
    drawInfoRow("Uptime", buf, COL1_X, y, contentWidth, th.textSecondary);

    // ═══════════════════════════════════════════════════════════════════════
    // Navigation hint at bottom
    // ═══════════════════════════════════════════════════════════════════════
    y = CONTENT_BOTTOM - 12;
    d.setTextColor(th.textMuted, th.bg);
    d.setTextSize(1);
    d.setCursor(COL1_X, y);
    d.print("B/C:Select  Long:Adjust");
}
