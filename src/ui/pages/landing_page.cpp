/*
 * Landing Page Implementation
 * System overview page
 */

#include <M5StamPLC.h>
#include "landing_page.h"
#include "../ui_constants.h"
#include "../theme.h"
#include "../../modules/catm_gnss/catm_gnss_module.h"
#include "../../modules/storage/sd_card_module.h"
#include <ESP.h>

// External globals
extern CatMGNSSModule* catmGnssModule;
extern SDCardModule* sdModule;

void drawLandingPage() {
    const auto& th = ui::theme();
    auto& display = M5StamPLC.Display;
    display.setTextSize(2);
    display.setTextColor(th.accent, th.bg);
    display.setCursor(COL1_X, CONTENT_TOP);
    display.print("System Overview");

    display.setTextSize(1);
    display.setTextColor(th.text, th.bg);
    int16_t y = CONTENT_TOP + 20;

    struct tm rtcTime {};
    M5StamPLC.RX8130.getTime(&rtcTime);
    char timeBuf[40];
    snprintf(timeBuf, sizeof(timeBuf), "%04d-%02d-%02d %02d:%02d:%02d",
             rtcTime.tm_year + 1900, rtcTime.tm_mon + 1, rtcTime.tm_mday,
             rtcTime.tm_hour, rtcTime.tm_min, rtcTime.tm_sec);
    display.setCursor(COL1_X, y);
    display.printf("Time: %s", timeBuf);
    y += LINE_H1;

    // Cellular status with theme colors
    String carrier = "Offline";
    int signalDbm = -120;
    bool cellConnected = false;
    if (catmGnssModule && catmGnssModule->isModuleInitialized()) {
        CellularData cd = catmGnssModule->getCellularData();
        if (cd.operatorName.length()) carrier = cd.operatorName;
        signalDbm = cd.signalStrength;
        cellConnected = cd.isConnected;
    }
    int signalPercent = (int)constrain(map((long)signalDbm, -120, -50, 0, 100), 0L, 100L);
    int bars = (signalPercent + 19) / 20;
    char barGraph[6];
    for (int i = 0; i < 5; ++i) {
        barGraph[i] = (i < bars && cellConnected) ? '#' : '.';
    }
    barGraph[5] = '\0';
    display.setCursor(COL1_X, y);
    display.setTextColor(cellConnected ? th.green : th.red, th.bg);
    display.printf("Cell: %s dBm:%d [%s]", carrier.c_str(), signalDbm, barGraph);
    display.setTextColor(th.text, th.bg);
    y += LINE_H1;

    // GNSS status with theme colors
    bool gnssLocked = false;
    uint8_t sats = 0;
    uint32_t fixAgeSec = 0;
    if (catmGnssModule && catmGnssModule->isModuleInitialized()) {
        GNSSData gnss = catmGnssModule->getGNSSData();
        gnssLocked = gnss.isValid;
        sats = gnss.satellites;
        if (gnssLocked && gnss.lastUpdate != 0) {
            fixAgeSec = (millis() - gnss.lastUpdate) / 1000U;
        }
    }
    display.setCursor(COL1_X, y);
    display.setTextColor(gnssLocked ? th.green : th.yellow, th.bg);
    display.printf("GPS: %s | sats:%u | last fix:%us",
                   gnssLocked ? "LOCK" : "search", sats, gnssLocked ? fixAgeSec : 0);
    display.setTextColor(th.text, th.bg);
    y += LINE_H1;

    // SD card status with theme colors
    float sdFreeMB = -1.0f;
    float sdTotalMB = 0.0f;
    if (sdModule && sdModule->isMounted()) {
        uint64_t total = sdModule->totalBytes();
        uint64_t used = sdModule->usedBytes();
        if (total > 0 && used <= total) {
            sdTotalMB = total / (1024.0f * 1024.0f);
            sdFreeMB = (total - used) / (1024.0f * 1024.0f);
        }
    }
    display.setCursor(COL1_X, y);
    if (sdFreeMB >= 0.0f) {
        display.setTextColor(th.textSecondary, th.bg);
        display.printf("SD: %.1f MB free / %.1f MB", sdFreeMB, sdTotalMB);
    } else {
        display.setTextColor(th.red, th.bg);
        display.print("SD: Not detected");
    }
    display.setTextColor(th.text, th.bg);
    y += LINE_H1;

    // Memory status with theme colors
    uint32_t freeHeap = ESP.getFreeHeap();
    display.setCursor(COL1_X, y);
    display.setTextColor(th.textSecondary, th.bg);
    display.printf("Memory: %.1f MB free", freeHeap / (1024.0f * 1024.0f));
}
