#include "settings_store.h"
#include <Preferences.h>
#include "config/system_config.h"

#if ENABLE_SD
#include <ArduinoJson.h>
#include <SD.h>
#include <SPI.h>
#include "../storage/sd_card_module.h"
extern SDCardModule* sdModule;
#endif

static const char* NS = "stamplc";

static void clear(AppSettings& s) {
    memset(&s, 0, sizeof(s));
}

static void ensureDefaultPreferences() {
    Preferences pw;
    if (!pw.begin(NS, false)) {
        return;
    }

    if (!pw.isKey("apn"))   pw.putString("apn",   "soracom.io");
    if (!pw.isKey("apnU"))  pw.putString("apnU",  "sora");
    if (!pw.isKey("apnP"))  pw.putString("apnP",  "sora");
    if (!pw.isKey("httpHost")) {
        if (pw.isKey("mHost")) {
            pw.putString("httpHost", pw.getString("mHost", "").c_str());
        } else {
            pw.putString("httpHost", "beam.soracom.io");
        }
    }
    if (!pw.isKey("httpPort")) {
        if (pw.isKey("mPort")) {
            pw.putUInt("httpPort", pw.getUInt("mPort", 8888));
        } else {
            pw.putUInt("httpPort", 8888);
        }
    }
    if (!pw.isKey("httpToken")) {
        if (pw.isKey("mUser")) {
            pw.putString("httpToken", pw.getString("mUser", "").c_str());
        } else {
            pw.putString("httpToken", "9751vqagkctha30lwow8");
        }
    }

    pw.end();
}

#if ENABLE_SD
static bool ensureSdAvailable() {
    if (SD.cardType() != CARD_NONE) {
        return true;
    }

    constexpr int SD_MOSI = 8;
    constexpr int SD_MISO = 9;
    constexpr int SD_SCK  = 7;
    constexpr int SD_CS   = 10;

    SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
    delay(10);
    if (SD.begin(SD_CS, SPI, 4000000)) return true;
    if (SD.begin(SD_CS, SPI, 1000000)) return true;
    if (SD.begin(SD_CS, SPI, 400000)) return true;
    return false;
}

static bool loadSettingsFromSd(AppSettings& s) {
    constexpr const char* kConfigPath = "/config/connection.json";

    bool sdReady = (sdModule && sdModule->isMounted()) || ensureSdAvailable();
    if (!sdReady) {
        return false;
    }

    if (!SD.exists(kConfigPath)) {
        return false;
    }

    File f = SD.open(kConfigPath, FILE_READ);
    if (!f) {
        Serial.println("settings: unable to open /config/connection.json");
        return false;
    }

    StaticJsonDocument<512> doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) {
        Serial.printf("settings: failed to parse connection.json (%s)\n", err.c_str());
        return false;
    }

    bool changed = false;
    auto applyString = [&](const char* key, char* dest, size_t destSize) {
        if (!doc.containsKey(key)) return;
        const char* val = doc[key];
        if (!val) val = "";
        if (strncmp(dest, val, destSize) != 0) {
            strlcpy(dest, val, destSize);
            changed = true;
        }
    };

    applyString("apn", s.apn, sizeof(s.apn));
    applyString("apnUser", s.apnUser, sizeof(s.apnUser));
    applyString("apnPass", s.apnPass, sizeof(s.apnPass));
    applyString("httpHost", s.httpHost, sizeof(s.httpHost));
    applyString("httpToken", s.httpToken, sizeof(s.httpToken));
    // Optional: persist PDP bring-up script into NVS so it can run without SD
    if (doc.containsKey("pdp_script") && doc["pdp_script"].is<JsonArray>()) {
        String script;
        for (JsonVariant v : doc["pdp_script"].as<JsonArray>()) {
            if (v.is<const char*>()) {
                script += v.as<const char*>();
                script += "\\n";
            }
        }
        Preferences p;
        if (p.begin("stamplc", false)) {
            p.putString("pdpScript", script);
            p.putBool("pdpScriptRan", false);
            p.end();
            Serial.println("settings: stored PDP script to NVS (pdpScript)");
        }
    }

    if (doc.containsKey("httpPort")) {
        uint16_t port = doc["httpPort"];
        if (port == 0) {
            port = 443;
        }
        if (s.httpPort != port) {
            s.httpPort = port;
            changed = true;
        }
    }

    if (changed) {
        Serial.println("settings: applied overrides from /config/connection.json");
    }
    return changed;
}
#else
static bool loadSettingsFromSd(AppSettings&) { return false; }
#endif

bool settingsLoad(AppSettings& s){
    clear(s);
    ensureDefaultPreferences();
    Preferences p;
    if(!p.begin(NS, true)) {
        strlcpy(s.apn,     "soracom.io", sizeof(s.apn));
        strlcpy(s.apnUser, "sora",       sizeof(s.apnUser));
        strlcpy(s.apnPass, "sora",       sizeof(s.apnPass));
        strlcpy(s.httpHost, "beam.soracom.io", sizeof(s.httpHost));
        s.httpPort = 8888;
        strlcpy(s.httpToken, "9751vqagkctha30lwow8", sizeof(s.httpToken));
        return true;
    }

    strlcpy(s.apn,      p.getString("apn",   "soracom.io").c_str(), sizeof(s.apn));
    strlcpy(s.apnUser,  p.getString("apnU",  "sora").c_str(), sizeof(s.apnUser));
    strlcpy(s.apnPass,  p.getString("apnP",  "sora").c_str(), sizeof(s.apnPass));
    strlcpy(s.httpHost, p.getString("httpHost", "beam.soracom.io").c_str(), sizeof(s.httpHost));
    s.httpPort = (uint16_t)p.getUInt("httpPort", 8888);
    strlcpy(s.httpToken, p.getString("httpToken", "9751vqagkctha30lwow8").c_str(), sizeof(s.httpToken));

    // Legacy migration: if httpHost empty but old keys exist, populate once
    if (s.httpHost[0] == '\0') {
        String legacyHost = p.getString("mHost", "");
        if (legacyHost.length()) {
            strlcpy(s.httpHost, legacyHost.c_str(), sizeof(s.httpHost));
        }
    }
    if (s.httpPort == 0) {
        s.httpPort = (uint16_t)p.getUInt("mPort", 443);
        if (s.httpPort == 0) s.httpPort = 443;
    }
    if (s.httpToken[0] == '\0') {
        String legacyToken = p.getString("mUser", "");
        strlcpy(s.httpToken, legacyToken.c_str(), sizeof(s.httpToken));
    }

    p.end();

    if (loadSettingsFromSd(s)) {
        settingsSave(s);  // Persist SD overrides into NVS so they survive without SD
    }
    return true;
}

bool settingsSave(const AppSettings& s){
    Preferences p;
    if(!p.begin(NS, false)) {
        // Could not open/create namespace
        return false;
    }
    p.putString("apn",   s.apn);
    p.putString("apnU",  s.apnUser);
    p.putString("apnP",  s.apnPass);
    p.putString("httpHost", s.httpHost);
    p.putUInt("httpPort",   s.httpPort ? s.httpPort : 443);
    p.putString("httpToken", s.httpToken);
    p.end();
    return true;
}

// ============================================================================
// Display Settings (separate namespace to avoid conflicts)
// ============================================================================
static const char* NS_DISPLAY = "display";

void displaySettingsLoad(uint8_t& brightness, bool& sleepEnabled, uint16_t& sleepSec) {
    Preferences p;
    if (!p.begin(NS_DISPLAY, true)) {
        // Defaults if NVS not available
        brightness = 100;
        sleepEnabled = true;
        sleepSec = 120;
        return;
    }
    brightness = p.getUChar("bright", 100);
    sleepEnabled = p.getBool("sleepEn", true);
    sleepSec = p.getUShort("sleepSec", 120);
    p.end();

    // Clamp values to valid ranges
    if (brightness == 0) brightness = 10;  // Minimum visible brightness
    if (sleepSec < 30) sleepSec = 30;
    if (sleepSec > 600) sleepSec = 600;
}

void displaySettingsSave(uint8_t brightness, bool sleepEnabled, uint16_t sleepSec) {
    Preferences p;
    if (!p.begin(NS_DISPLAY, false)) {
        return;
    }
    p.putUChar("bright", brightness);
    p.putBool("sleepEn", sleepEnabled);
    p.putUShort("sleepSec", sleepSec);
    p.end();
}
