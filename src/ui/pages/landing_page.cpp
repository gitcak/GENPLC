/*
 * Landing Page Implementation
 * System overview with status icons
 */

#include <Arduino.h>
#include <M5StamPLC.h>
#include "landing_page.h"
#include "../ui_constants.h"
#include "../theme.h"
#include "../components/icon_manager.h"
#include "../../modules/catm_gnss/catm_gnss_module.h"
#include "../../modules/storage/sd_card_module.h"
#include <Esp.h>
#include <cstring>

// External globals
extern CatMGNSSModule* catmGnssModule;
extern SDCardModule* sdModule;

// Layout constants
static constexpr int16_t ICON_SIZE = 12;
static constexpr int16_t ROW_H = 14;
static constexpr int16_t TEXT_OFFSET = ICON_SIZE + 4;

void drawLandingPage() {
    const auto& th = ui::theme();
    auto& d = M5StamPLC.Display;

    // ─── Page Title ───
    d.setTextSize(2);
    d.setTextColor(th.accent, th.bg);
    d.setCursor(COL1_X, CONTENT_TOP);
    d.print("Overview");

    d.setTextSize(1);
    int16_t y = CONTENT_TOP + LINE_H2 + 2;
    char buf[64];

    // ─── Time row (no icon, just clock symbol) ───
    struct tm rtcTime {};
    M5StamPLC.RX8130.getTime(&rtcTime);
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d",
             rtcTime.tm_year + 1900, rtcTime.tm_mon + 1, rtcTime.tm_mday,
             rtcTime.tm_hour, rtcTime.tm_min);
    d.setTextColor(th.textSecondary, th.bg);
    d.setCursor(COL1_X, y);
    d.print(buf);
    y += ROW_H;

    // ─── Cellular status with icon ───
    static char carrier[24];
    strncpy(carrier, "Offline", sizeof(carrier));
    int signalDbm = -120;
    bool cellConnected = false;
    if (catmGnssModule && catmGnssModule->isModuleInitialized()) {
        CellularData cd = catmGnssModule->getCellularData();
        if (cd.operatorName.length() > 0) {
            strncpy(carrier, cd.operatorName.c_str(), sizeof(carrier) - 1);
            carrier[sizeof(carrier) - 1] = '\0';
        }
        signalDbm = cd.signalStrength;
        cellConnected = cd.isConnected;
    }

    // Draw cellular icon
    uint16_t cellColor = cellConnected ? th.green : th.red;
    drawIconCellularDirect(COL1_X, y, ICON_SIZE, cellColor);

    // Cell text
    int bars = constrain(map(signalDbm, -120, -50, 0, 5), 0, 5);
    snprintf(buf, sizeof(buf), "%s %ddBm", carrier, signalDbm);
    d.setTextColor(cellColor, th.bg);
    d.setCursor(COL1_X + TEXT_OFFSET, y + 2);
    d.print(buf);

    // Signal bar visualization (right side)
    int barX = UI_DISPLAY_W - COL1_X - 30;
    for (int i = 0; i < 5; i++) {
        int barH = 2 + i * 2;
        uint16_t barCol = (i < bars && cellConnected) ? cellColor : th.borderSubtle;
        d.fillRect(barX + i * 6, y + ICON_SIZE - barH, 4, barH, barCol);
    }
    y += ROW_H;

    // ─── GNSS status with icon ───
    bool gnssLocked = false;
    uint8_t sats = 0;
    if (catmGnssModule && catmGnssModule->isModuleInitialized()) {
        GNSSData gnss = catmGnssModule->getGNSSData();
        gnssLocked = gnss.isValid;
        sats = gnss.satellites;
    }

    // Draw GPS icon
    uint16_t gnssColor = gnssLocked ? th.green : th.yellow;
    drawIconGPSDirect(COL1_X, y, ICON_SIZE, gnssColor);

    // GPS text
    snprintf(buf, sizeof(buf), "%s | %u sats", gnssLocked ? "FIX" : "Searching", sats);
    d.setTextColor(gnssColor, th.bg);
    d.setCursor(COL1_X + TEXT_OFFSET, y + 2);
    d.print(buf);
    y += ROW_H;

    // ─── SD card status with icon ───
    bool sdMounted = sdModule && sdModule->isMounted();
    float sdFreeMB = -1.0f;
    if (sdMounted) {
        uint64_t total = sdModule->totalBytes();
        uint64_t used = sdModule->usedBytes();
        if (total > 0 && used <= total) {
            sdFreeMB = (total - used) / (1024.0f * 1024.0f);
        }
    }

    // Draw log/storage icon
    uint16_t sdColor = sdMounted ? th.green : th.red;
    drawIconLogDirect(COL1_X, y, ICON_SIZE, sdColor);

    // SD text
    if (sdFreeMB >= 0.0f) {
        snprintf(buf, sizeof(buf), "SD: %.1f MB free", sdFreeMB);
        d.setTextColor(th.textSecondary, th.bg);
    } else {
        snprintf(buf, sizeof(buf), "SD: Not mounted");
        d.setTextColor(th.red, th.bg);
    }
    d.setCursor(COL1_X + TEXT_OFFSET, y + 2);
    d.print(buf);
    y += ROW_H;

    // ─── Memory status with icon ───
    uint32_t freeHeap = ESP.getFreeHeap();
    uint16_t memColor = (freeHeap > 50000) ? th.green :
                        (freeHeap > 20000) ? th.yellow : th.red;

    // Draw gear/system icon
    drawIconGearDirect(COL1_X, y, ICON_SIZE, th.cyan);

    // Memory text
    snprintf(buf, sizeof(buf), "Heap: %.1f KB", freeHeap / 1024.0f);
    d.setTextColor(memColor, th.bg);
    d.setCursor(COL1_X + TEXT_OFFSET, y + 2);
    d.print(buf);

    // ─── Navigation hint ───
    y = CONTENT_BOTTOM - 10;
    d.setTextColor(th.textMuted, th.bg);
    d.setCursor(COL1_X, y);
    d.print("Long press: A=Cell B=GPS C=Sys");
}
