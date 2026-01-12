/*
 * RTC Manager Implementation
 * Real-Time Clock management and synchronization
 */

#include "rtc_manager.h"
#include "../modules/catm_gnss/catm_gnss_module.h"
#include "../modules/catm_gnss/gnss_status.h"
#include "../config/system_config.h"
#include <M5StamPLC.h>
#include <sys/time.h>
#include <time.h>

// External globals (declared in main.cpp)
extern CatMGNSSModule* catmGnssModule;
extern bool g_cntpSyncedThisSession;
extern bool g_lastCellularNtpUsedCntp;

bool setRTCFromCellular() {
    if (!catmGnssModule || !catmGnssModule->isModuleInitialized()) return false;
    
    CellularData cd = catmGnssModule->getCellularData();
    if (!cd.isConnected) {
        Serial.println("RTC: Cellular data session not active; attempting to read network clock anyway");
    }
    
    // Get network time from cellular module, trigger CNTP if clock not ready
    struct tm networkTime;
    if (!catmGnssModule->getNetworkTime(networkTime)) {
        Serial.println("RTC: +CCLK? did not return time, attempting CNTP sync...");
        if (!catmGnssModule->syncNetworkTime(networkTime, 65000)) {
            Serial.printf("RTC: CNTP sync failed: %s\n", catmGnssModule->getLastError().c_str());
            return false;
        }
        g_cntpSyncedThisSession = true;
        g_lastCellularNtpUsedCntp = true;
    }
    
    // Set RTC time (UTC)
    if (M5StamPLC.RX8130.begin()) {
        M5StamPLC.RX8130.setTime(&networkTime);
        Serial.printf("RTC synchronized from cellular network: %04d-%02d-%02d %02d:%02d:%02d UTC\n", 
                     networkTime.tm_year + 1900, networkTime.tm_mon + 1, networkTime.tm_mday,
                     networkTime.tm_hour, networkTime.tm_min, networkTime.tm_sec);
        return true;
    }
    return false;
}

bool setRTCFromBuildTimestamp() {
    const char* buildDate = FIRMWARE_BUILD_DATE; // example: "Jan  7 2025"
    const char* buildTime = FIRMWARE_BUILD_TIME; // example: "15:04:05"

    char monthStr[4] = {0};
    int day = 0;
    int year = 0;
    if (sscanf(buildDate, "%3s %d %d", monthStr, &day, &year) != 3) {
        return false;
    }

    static const char* months[] = {
        "Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"
    };
    int month = -1;
    for (int i = 0; i < 12; ++i) {
        if (strcasecmp(monthStr, months[i]) == 0) {
            month = i + 1;
            break;
        }
    }
    if (month == -1) return false;

    int hour = 0, minute = 0, second = 0;
    if (sscanf(buildTime, "%d:%d:%d", &hour, &minute, &second) != 3) {
        return false;
    }

    struct tm tmUtc {};
    tmUtc.tm_year = year - 1900;
    tmUtc.tm_mon = month - 1;
    tmUtc.tm_mday = day;
    tmUtc.tm_hour = hour;
    tmUtc.tm_min = minute;
    tmUtc.tm_sec = second;
    tmUtc.tm_isdst = 0;

    time_t epoch = mktime(&tmUtc);
    if (epoch <= 0) return false;

    struct timeval tv;
    tv.tv_sec = epoch;
    tv.tv_usec = 0;
    settimeofday(&tv, nullptr);

    if (M5StamPLC.RX8130.begin()) {
        M5StamPLC.RX8130.setTime(&tmUtc);
        Serial.printf("RTC seeded from firmware build timestamp: %04d-%02d-%02d %02d:%02d:%02d\n",
                      year, month, day, hour, minute, second);
        return true;
    }
    return false;
}

bool setRTCFromGPS(const GNSSData& gnssData) {
    if (!gnssData.isValid || gnssData.year < 2020) return false;
    
    // Create tm structure for RTC (store as UTC)
    struct tm rtcTime;
    rtcTime.tm_year = gnssData.year - 1900;  // tm_year is years since 1900
    rtcTime.tm_mon = gnssData.month - 1;     // tm_mon is 0-11
    rtcTime.tm_mday = gnssData.day;
    rtcTime.tm_hour = gnssData.hour;
    rtcTime.tm_min = gnssData.minute;
    rtcTime.tm_sec = gnssData.second;
    
    // Calculate day of week
    int m = gnssData.month;
    int y = gnssData.year;
    if (m < 3) {
        m += 12;
        y -= 1;
    }
    int dayOfWeek = (gnssData.day + (13 * (m + 1)) / 5 + y + y / 4 - y / 100 + y / 400) % 7;
    rtcTime.tm_wday = (dayOfWeek + 5) % 7; // Adjust to 0=Sunday
    
    // Set RTC time (UTC)
    if (M5StamPLC.RX8130.begin()) {
        M5StamPLC.RX8130.setTime(&rtcTime);
        Serial.printf("RTC synchronized from GPS: %04d-%02d-%02d %02d:%02d:%02d UTC\n", 
                     gnssData.year, gnssData.month, gnssData.day, 
                     gnssData.hour, gnssData.minute, gnssData.second);
        return true;
    }
    return false;
}

bool getRTCTime(struct tm& timeinfo) {
    if (M5StamPLC.RX8130.begin()) {
        M5StamPLC.RX8130.getTime(&timeinfo);
        // Fix tm structure (RTC returns year as 2-digit, month as 1-12)
        if (timeinfo.tm_year < 100) timeinfo.tm_year += 100; // Convert to years since 1900
        timeinfo.tm_mon -= 1; // Convert to 0-11 range
        return true;
    }
    return false;
}

