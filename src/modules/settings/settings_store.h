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
};

bool settingsLoad(AppSettings& s);
bool settingsSave(const AppSettings& s);

#endif // SETTINGS_STORE_H

