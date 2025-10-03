#ifndef SETTINGS_STORE_H
#define SETTINGS_STORE_H

#include <Arduino.h>

struct AppSettings {
    char wifiSsid[32];
    char wifiPass[64];
    char apn[48];
    char apnUser[32];
    char apnPass[32];
    char mqttHost[64];
    uint16_t mqttPort;
    char mqttUser[32];
    char mqttPass[32];
};

bool settingsLoad(AppSettings& s);
bool settingsSave(const AppSettings& s);

#endif // SETTINGS_STORE_H


