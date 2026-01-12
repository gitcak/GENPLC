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

void drawCompactSignalBar(void* sprite, int16_t x, int16_t y, int8_t signal, uint16_t color) {
    // Enhanced null and validity checks
    if (!sprite) return;
    
    lgfx::LGFX_Sprite* spr = (lgfx::LGFX_Sprite*)sprite;
    if (!spr) return;
    
    // Validate sprite dimensions before drawing
    if (spr->width() <= 0 || spr->height() <= 0) {
        return;
    }
    
    // Ensure drawing coordinates are within sprite bounds
    if (x < 0 || y < 0 || x >= spr->width() || y >= spr->height()) {
        return;
    }
    
    // Draw mini signal bar (5 bars, 2px wide each)
    for (int i = 0; i < 5; i++) {
        int barHeight = (i + 1) * 2;
        int barX = x + i * 3;
        int barY = y + 10 - barHeight;
        
        // Check if bar is within sprite bounds
        if (barX >= spr->width() || barY >= spr->height()) continue;
        if (barX + 2 > spr->width() || barY + barHeight > spr->height()) continue;
        
        if (signal >= (i + 1) * 20) {
            spr->fillRect(barX, barY, 2, barHeight, color);
        } else {
            spr->drawRect(barX, barY, 2, barHeight, 0x4A4A4A);
        }
    }
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

void drawWrappedText(const String& text, int16_t x, int16_t y, int16_t maxWidth, int16_t lineHeight) {
    int start = 0;
    const int length = text.length();
    while (start < length) {
        int end = start;
        int lastSpace = -1;
        while (end < length) {
            if (text[end] == ' ') {
                lastSpace = end;
            }
            String slice = text.substring(start, end + 1);
            if (M5StamPLC.Display.textWidth(slice) > maxWidth) {
                if (lastSpace >= start) {
                    end = lastSpace;
                } else if (end == start) {
                    // single word longer than width; force break
                    while (end < length && text[end] != ' ') {
                        ++end;
                    }
                }
                break;
            }
            ++end;
        }
        if (end > length) end = length;
        String line = text.substring(start, end);
        line.trim();
        if (line.length()) {
            M5StamPLC.Display.setCursor(x, y);
            M5StamPLC.Display.print(line);
            y += lineHeight;
        }
        start = end;
        while (start < length && text[start] == ' ') {
            ++start;
        }
    }
}

void drawCardBox(lgfx::LGFX_Sprite* s, int x, int y, int w, int h, const char* title, lgfx::LGFX_Sprite* icon) {
    const auto& th = ui::theme();
    
    // Enhanced card styling with theme colors
    s->fillRoundRect(x, y, w, h, 8, th.card);
    s->drawRoundRect(x, y, w, h, 8, th.border);
    
    // Add subtle inner highlight with theme
    s->drawRoundRect(x + 1, y + 1, w - 2, h - 2, 7, th.accent);
    
    // Draw icon if provided
    if (icon != nullptr) {
        icon->pushSprite(s, x + 6, y + 6);
        s->setCursor(x + 18, y + 8);
    } else {
        s->setCursor(x + 10, y + 8);
    }
    
    // Draw title with theme colors
    s->setTextColor(th.text);
    s->setTextSize(1);
    s->print(title);
    
    // Add subtle bottom accent line with theme
    s->drawFastHLine(x + 8, y + h - 2, w - 16, th.textSecondary);
}
