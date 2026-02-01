#ifndef SETTINGS_STORE_H
#define SETTINGS_STORE_H

#include <Arduino.h>

struct AppSettings {
    char apn[48];
    char apnUser[32];
    char apnPass[32];
    char httpHost[64];
    uint16_t httpPort;
    char httpToken[80];

    // Display settings
    uint8_t displayBrightness;  // 0-255
    bool displaySleepEnabled;   // Auto-sleep after timeout
    uint16_t displaySleepSec;   // Sleep timeout in seconds (30-600)
};

// Display settings functions
void displaySettingsLoad(uint8_t& brightness, bool& sleepEnabled, uint16_t& sleepSec);
void displaySettingsSave(uint8_t brightness, bool sleepEnabled, uint16_t sleepSec);

bool settingsLoad(AppSettings& s);
bool settingsSave(const AppSettings& s);

#endif // SETTINGS_STORE_H

