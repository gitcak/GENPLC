#include "settings_store.h"
#include <Preferences.h>

static const char* NS = "stamplc";

static void clear(AppSettings& s){
    memset(&s, 0, sizeof(s));
}

static void ensureDefaultPreferences() {
    Preferences pw;
    if (!pw.begin(NS, false)) {
        return;
    }
    if (!pw.isKey("wssid")) pw.putString("wssid", "");
    if (!pw.isKey("wpass")) pw.putString("wpass", "");
    if (!pw.isKey("apn"))   pw.putString("apn",   "soracom.io");
    if (!pw.isKey("apnU"))  pw.putString("apnU",  "sora");
    if (!pw.isKey("apnP"))  pw.putString("apnP",  "sora");
    if (!pw.isKey("mHost")) pw.putString("mHost", "");
    if (!pw.isKey("mPort")) pw.putUInt("mPort", 1883);
    if (!pw.isKey("mUser")) pw.putString("mUser", "");
    if (!pw.isKey("mPass")) pw.putString("mPass", "");
    pw.end();
}

bool settingsLoad(AppSettings& s){
    clear(s);
    ensureDefaultPreferences();
    Preferences p;
    if(!p.begin(NS, true)) {
        strlcpy(s.wifiSsid, "", sizeof(s.wifiSsid));
        strlcpy(s.wifiPass, "", sizeof(s.wifiPass));
        strlcpy(s.apn,     "soracom.io", sizeof(s.apn));
        strlcpy(s.apnUser, "sora",       sizeof(s.apnUser));
        strlcpy(s.apnPass, "sora",       sizeof(s.apnPass));
        strlcpy(s.mqttHost, "", sizeof(s.mqttHost));
        s.mqttPort = 1883;
        strlcpy(s.mqttUser, "", sizeof(s.mqttUser));
        strlcpy(s.mqttPass, "", sizeof(s.mqttPass));
        return true;
    }
    strlcpy(s.wifiSsid, p.getString("wssid","").c_str(), sizeof(s.wifiSsid));
    strlcpy(s.wifiPass, p.getString("wpass","").c_str(), sizeof(s.wifiPass));
    strlcpy(s.apn,      p.getString("apn","soracom.io").c_str(),  sizeof(s.apn));
    strlcpy(s.apnUser,  p.getString("apnU","sora").c_str(), sizeof(s.apnUser));
    strlcpy(s.apnPass,  p.getString("apnP","sora").c_str(), sizeof(s.apnPass));
    strlcpy(s.mqttHost, p.getString("mHost","").c_str(), sizeof(s.mqttHost));
    s.mqttPort = (uint16_t)p.getUInt("mPort", 1883);
    strlcpy(s.mqttUser, p.getString("mUser","").c_str(), sizeof(s.mqttUser));
    strlcpy(s.mqttPass, p.getString("mPass","").c_str(), sizeof(s.mqttPass));
    p.end();
    return true;
}

bool settingsSave(const AppSettings& s){
    Preferences p; 
    if(!p.begin(NS, false)) {
        // Could not open/create namespace
        return false;
    }
    p.putString("wssid", s.wifiSsid);
    p.putString("wpass", s.wifiPass);
    p.putString("apn",   s.apn);
    p.putString("apnU",  s.apnUser);
    p.putString("apnP",  s.apnPass);
    p.putString("mHost", s.mqttHost);
    p.putUInt("mPort",   s.mqttPort ? s.mqttPort : 1883);
    p.putString("mUser", s.mqttUser);
    p.putString("mPass", s.mqttPass);
    p.end();
    return true;
}


