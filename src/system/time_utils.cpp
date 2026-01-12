/*
 * Time Utilities Implementation
 * NTP configuration, time synchronization, and timezone management
 */

#include "time_utils.h"
#include "rtc_manager.h"
#include "../modules/catm_gnss/catm_gnss_module.h"
#include "../modules/catm_gnss/gnss_status.h"
#include "../config/system_config.h"
#include <WiFi.h>
#include <time.h>
#include <sys/time.h>
#include <string.h>
#include <M5StamPLC.h>

// External globals (declared in main.cpp)
extern CatMGNSSModule* catmGnssModule;
extern bool g_cntpSyncedThisSession;
extern bool g_lastCellularNtpUsedCntp;

// Internal state
static bool g_ntpConfigured = false;
static uint32_t g_lastTZUpdateMs = 0;

bool fetchNtpTimeViaCellular(struct tm& timeInfo) {
    if (!catmGnssModule || !catmGnssModule->isModuleInitialized()) return false;

    CellularData cd = catmGnssModule->getCellularData();
    if (!cd.isConnected) return false;

    g_lastCellularNtpUsedCntp = false;

    // Try CNTP (AT+CNTPSTART) first - fast and efficient
    Serial.println("NTP: Attempting CNTP sync via Soracom NTP...");
    if (catmGnssModule->syncNetworkTime(timeInfo, 65000)) {
        int year = timeInfo.tm_year + 1900;
        int month = timeInfo.tm_mon + 1;
        Serial.printf("NTP: CNTP sync success - %04d-%02d-%02d %02d:%02d:%02d UTC\n",
                      year, month, timeInfo.tm_mday, timeInfo.tm_hour, 
                      timeInfo.tm_min, timeInfo.tm_sec);
        g_cntpSyncedThisSession = true;
        g_lastCellularNtpUsedCntp = true;
        return true;
    }
    
    Serial.printf("NTP: CNTP sync failed (%s), falling back to HTTP...\n", 
                  catmGnssModule->getLastError().c_str());

    // Fallback: HTTP-based time fetch via worldtimeapi.org
    String url = "http://worldtimeapi.org/api/timezone/Etc/UTC";
    String response;

    if (!catmGnssModule->sendHTTP(url, "", response)) {
        Serial.println("NTP: Failed to fetch time from cellular HTTP fallback");
        return false;
    }

    // Parse JSON response
    // Expected format: {"datetime": "2024-01-15T19:30:25.123456+00:00", ...}
    int datetimeStart = response.indexOf("\"datetime\":\"");
    if (datetimeStart < 0) {
        Serial.println("NTP: Invalid JSON response from time API");
        return false;
    }

    datetimeStart += 12; // Skip "datetime":" 
    int datetimeEnd = response.indexOf("\"", datetimeStart);
    if (datetimeEnd < 0) {
        Serial.println("NTP: Malformed datetime in JSON response");
        return false;
    }

    String datetime = response.substring(datetimeStart, datetimeEnd);

    // Parse ISO 8601 datetime: 2024-01-15T19:30:25.123456+00:00
    // Extract: YYYY-MM-DDTHH:MM:SS
    if (datetime.length() < 19) {
        Serial.println("NTP: Datetime string too short");
        return false;
    }

    // Parse year, month, day
    int year = datetime.substring(0, 4).toInt();
    int month = datetime.substring(5, 7).toInt();
    int day = datetime.substring(8, 10).toInt();
    int hour = datetime.substring(11, 13).toInt();
    int minute = datetime.substring(14, 16).toInt();
    int second = datetime.substring(17, 19).toInt();

    // Validate parsed values
    if (year < 2020 || year > 2030 || month < 1 || month > 12 ||
        day < 1 || day > 31 || hour < 0 || hour > 23 ||
        minute < 0 || minute > 59 || second < 0 || second > 59) {
        Serial.println("NTP: Invalid time values parsed from API");
        return false;
    }

    // Fill tm structure (now in UTC)
    timeInfo.tm_year = year - 1900;
    timeInfo.tm_mon = month - 1;
    timeInfo.tm_mday = day;
    timeInfo.tm_hour = hour;
    timeInfo.tm_min = minute;
    timeInfo.tm_sec = second;
    timeInfo.tm_isdst = 0; // UTC doesn't observe DST

    Serial.printf("NTP: Fetched UTC time via HTTP fallback: %04d-%02d-%02d %02d:%02d:%02d\n",
                  year, month, day, hour, minute, second);
    return true;
}

void ensureNtpConfigured() {
    if (g_ntpConfigured) return;

    // Configure NTP when either WiFi or cellular is connected
    bool hasConnectivity = false;

    // Check WiFi connectivity (use standard NTP)
    if ((WiFi.getMode() & WIFI_MODE_STA) && WiFi.status() == WL_CONNECTED) {
        hasConnectivity = true;
        Serial.println("NTP: Configuring via WiFi connection");
        // Timezone: Eastern with DST auto rules (EST/EDT). Adjust as needed.
        // Use multiple NTP servers for reliability
        configTzTime("EST5EDT,M3.2.0/2,M11.1.0/2",
                    "pool.ntp.org",
                    "time.nist.gov",
                    "time.google.com");
        g_ntpConfigured = true;
        Serial.println("NTP: Configuration completed via WiFi");
    }
    // Check cellular connectivity (NTP will be handled via HTTP)
    else if (catmGnssModule && catmGnssModule->isModuleInitialized()) {
        CellularData cd = catmGnssModule->getCellularData();
        if (cd.isConnected) {
            hasConnectivity = true;
            Serial.println("NTP: Cellular connection available (NTP via HTTP)");
            // For cellular, we don't configure standard NTP since it won't work
            // Instead, we'll use fetchNtpTimeViaCellular() in syncRTCFromAvailableSources()
            g_ntpConfigured = true; // Mark as configured so we don't keep checking
            Serial.println("NTP: HTTP-based NTP will be used via cellular");
        }
    }
}

void maybeUpdateTimeZoneFromCellular() {
    // Update timezone from cellular offset if available, every 10 minutes
    // Only trigger after a successful CNTP sync to ensure fresh modem time
    if (!g_cntpSyncedThisSession) return;
    if (millis() - g_lastTZUpdateMs < 10UL * 60UL * 1000UL) return;
    if (!catmGnssModule || !catmGnssModule->isModuleInitialized()) return;
    if (!catmGnssModule->isNetworkTimeSynced()) return;
    CellularData cd = catmGnssModule->getCellularData();
    if (!cd.isConnected) return;
    
    int q = 0;
    if (catmGnssModule->getNetworkTimeZoneQuarters(q)) {
        int minutes = q * 15; // minutes offset from UTC
        int hours = minutes / 60; 
        int mins = abs(minutes % 60);
        
        // POSIX TZ sign is reversed relative to human-readable UTC+X
        char tzbuf[32];
        if (minutes >= 0)
            snprintf(tzbuf, sizeof(tzbuf), "UTC-%d:%02d", hours, mins);
        else
            snprintf(tzbuf, sizeof(tzbuf), "UTC+%d:%02d", -hours, mins);
        
        setenv("TZ", tzbuf, 1);
        tzset();
        g_lastTZUpdateMs = millis();
        
        Serial.printf("Timezone updated from cellular network: %s\n", tzbuf);
    }
}

void formatLocalFromUTC(const struct tm& utcIn, char* timeStr, char* dateStr) {
    char prevTZ[64] = {0};
    const char* cur = getenv("TZ");
    if (cur) { strncpy(prevTZ, cur, sizeof(prevTZ) - 1); prevTZ[sizeof(prevTZ) - 1] = '\0'; }
    setenv("TZ", "UTC0", 1); tzset();
    struct tm tmp = utcIn; // mktime() normalizes
    time_t epoch = mktime(&tmp);
    if (prevTZ[0]) setenv("TZ", prevTZ, 1); else unsetenv("TZ");
    tzset();
    struct tm lt;
    localtime_r(&epoch, &lt);
    char tbuf[16]; snprintf(tbuf, sizeof(tbuf), "%02d:%02d", lt.tm_hour, lt.tm_min);
    char dbuf[16]; snprintf(dbuf, sizeof(dbuf), "%02d/%02d/%04d", lt.tm_mon + 1, lt.tm_mday, lt.tm_year + 1900);
    strncpy(timeStr, tbuf, 15); timeStr[15] = '\0';
    strncpy(dateStr, dbuf, 15); dateStr[15] = '\0';
}

void syncRTCFromAvailableSources() {
    static uint32_t lastNtpSync = 0;
    static uint32_t lastCellularSync = 0;
    static uint32_t lastGpsSync = 0;
    uint32_t now = millis();

    // Ensure NTP is configured if we have connectivity
    ensureNtpConfigured();

    // 1. Try NTP sync (every 1 hour)
    if (now - lastNtpSync > 3600000UL) {
        // First try standard NTP (WiFi)
        struct tm lt;
        if (getLocalTime(&lt, 50)) { // NTP available via WiFi
            time_t now_time = time(nullptr);
            struct tm utc;
            gmtime_r(&now_time, &utc);
            if (M5StamPLC.RX8130.begin()) {
                M5StamPLC.RX8130.setTime(&utc);
                Serial.println("RTC synchronized from NTP (WiFi)");
                lastNtpSync = now;
                return; // Success, no need to try others
            }
        }
        // If WiFi NTP failed, try HTTP-based NTP via cellular
        else if (catmGnssModule && catmGnssModule->isModuleInitialized()) {
            CellularData cd = catmGnssModule->getCellularData();
            if (cd.isConnected) {
                struct tm ntpTime;
                if (fetchNtpTimeViaCellular(ntpTime)) {
                    if (M5StamPLC.RX8130.begin()) {
                        M5StamPLC.RX8130.setTime(&ntpTime);
                        if (g_lastCellularNtpUsedCntp) {
                            Serial.println("RTC synchronized from CNTP (Cellular)");
                        } else {
                            Serial.println("RTC synchronized from HTTP (Cellular)");
                        }
                        lastNtpSync = now;
                        return; // Success, no need to try others
                    }
                }
            }
        }
    }

    // 2. Try cellular network time (every 30 minutes)
    if (now - lastCellularSync > 1800000UL) {
        if (catmGnssModule && catmGnssModule->isModuleInitialized()) {
            CellularData cd = catmGnssModule->getCellularData();
            if (cd.isConnected && setRTCFromCellular()) {
                Serial.println("RTC synchronized from cellular network time");
                lastCellularSync = now;
                return; // Success, no need to try GPS
            }
        }
    }

    // 3. Try GPS time (every 15 minutes)
    if (now - lastGpsSync > 900000UL) {
        if (catmGnssModule && catmGnssModule->isModuleInitialized()) {
            GNSSData gnssData = catmGnssModule->getGNSSData();
            if (gnssData.isValid && gnssData.year > 2020 && setRTCFromGPS(gnssData)) {
                Serial.println("RTC synchronized from GPS");
                lastGpsSync = now;
            }
        }
    }
}
