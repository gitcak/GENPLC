/*
 * UI Widgets Implementation
 * Reusable UI drawing functions
 */

#include "ui_widgets.h"
#include "../ui_constants.h"
#include "../ui_types.h"
#include "../../modules/storage/sd_card_module.h"
#include <WiFi.h>

// External globals accessed by widgets
extern SDCardModule* sdModule;
extern volatile DisplayPage currentPage;

#include "../theme.h"

void drawSignalBar(int16_t x, int16_t y, int16_t w, int16_t h, int8_t signalDbm) {
    const auto& th = ui::theme();
    
    // Draw signal strength bar (dBm to percentage)
    int16_t signalPercent = map(signalDbm, -120, -50, 0, 100);
    signalPercent = constrain(signalPercent, 0, 100);
    
    // Background with theme colors
    M5StamPLC.Display.fillRect(x, y, w, h, th.card);
    M5StamPLC.Display.drawRect(x, y, w, h, th.border);
    
    // Signal level with theme colors
    uint16_t signalColor = (signalPercent > 70) ? th.green : 
                          (signalPercent > 40) ? th.yellow : th.red;
    int16_t fillWidth = (w - 2) * signalPercent / 100;
    if (fillWidth > 0) {
        M5StamPLC.Display.fillRect(x + 1, y + 1, fillWidth, h - 2, signalColor);
    }
    
    // Text label with theme colors
    M5StamPLC.Display.setTextSize(1);
    M5StamPLC.Display.setTextColor(th.text, th.bg);
    M5StamPLC.Display.setCursor(x + w + 5, y);
    M5StamPLC.Display.printf("%ddBm", signalDbm);
}

void drawWiFiBar(int16_t x, int16_t y, int16_t w, int16_t h) {
    const auto& th = ui::theme();
    
    // Background with theme colors
    M5StamPLC.Display.fillRect(x, y, w, h, th.card);
    M5StamPLC.Display.drawRect(x, y, w, h, th.border);

    wifi_mode_t mode = WIFI_MODE_NULL;
    // Guard against older cores without getMode signature
    #ifdef ESP_WIFI_INCLUDED
    mode = WiFi.getMode();
    #else
    mode = WiFi.getMode();
    #endif

    bool isSta = (mode & WIFI_MODE_STA) != 0;
    bool isAp  = (mode & WIFI_MODE_AP) != 0;

    if (isSta && WiFi.status() == WL_CONNECTED) {
        int8_t wifiRSSI = WiFi.RSSI();
        int16_t wifiPercent = map(wifiRSSI, -100, -30, 0, 100);
        wifiPercent = constrain(wifiPercent, 0, 100);
        uint16_t wifiColor = (wifiPercent > 70) ? th.green : (wifiPercent > 40) ? th.yellow : th.red;
        int16_t fillWidth = (w - 2) * wifiPercent / 100;
        if (fillWidth > 0) {
            M5StamPLC.Display.fillRect(x + 1, y + 1, fillWidth, h - 2, wifiColor);
        }
        M5StamPLC.Display.setTextSize(1);
        M5StamPLC.Display.setTextColor(th.text, th.bg);
        M5StamPLC.Display.setCursor(x + w + 5, y);
        M5StamPLC.Display.printf("STA:%ddBm", wifiRSSI);
        return;
    }

    if (isAp) {
        // Show AP active; client count if available
        uint16_t color = th.yellow;
        M5StamPLC.Display.fillRect(x + 1, y + 1, w - 2, h - 2, color);
        M5StamPLC.Display.setTextSize(1);
        M5StamPLC.Display.setTextColor(th.text, th.bg);
        M5StamPLC.Display.setCursor(x + w + 5, y);
        // softAPgetStationNum may not exist on all cores; guard with weak behavior
        #ifdef ARDUINO_ARCH_ESP32
        M5StamPLC.Display.printf("AP:%d", WiFi.softAPgetStationNum());
        #else
        M5StamPLC.Display.print("AP:ON");
        #endif
        return;
    }

    // No WiFi with theme colors
    M5StamPLC.Display.setTextSize(1);
    M5StamPLC.Display.setTextColor(th.red, th.bg);
    M5StamPLC.Display.setCursor(x + w + 5, y);
    M5StamPLC.Display.print("WiFi:OFF");
}

void drawSDInfo(int x, int y) {
    const auto& th = ui::theme();
    if (!sdModule || !sdModule->isMounted()) return;
    M5StamPLC.Display.setTextSize(1);
    M5StamPLC.Display.setTextColor(th.accent, th.bg);
    M5StamPLC.Display.setCursor(x, y);
    uint64_t total = sdModule->totalBytes();
    uint64_t used = sdModule->usedBytes();
    M5StamPLC.Display.printf("SD Total: %.1f MB", total ? (double)total / (1024.0 * 1024.0) : 0.0);
    M5StamPLC.Display.setCursor(x, y + 20);
    M5StamPLC.Display.printf("SD Used:  %.1f MB", used ? (double)used / (1024.0 * 1024.0) : 0.0);
}

void drawButtonIndicators() {
    const auto& th = ui::theme();
    M5StamPLC.Display.setTextSize(1);
    M5StamPLC.Display.setTextColor(th.text, th.bg);

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
