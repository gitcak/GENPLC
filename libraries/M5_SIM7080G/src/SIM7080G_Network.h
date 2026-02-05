#pragma once

#include "M5_SIM7080G.h"

class SIM7080G_Network {
  public:
    explicit SIM7080G_Network(M5_SIM7080G &modem) : _modem(modem) {}

    // Configure PDP context APN (CID defaults to 1).
    bool setAPN(const SIM7080G_String &apn, SIM7080G_String user = "", SIM7080G_String pass = "", int cid = 1);

    // Activate/deactivate data context. For SIM7080G, examples commonly use profileId=0.
    bool activatePDP(int profileId = 0);
    bool deactivatePDP(int profileId = 0);
    bool isPDPActive(int profileId = 0);

    // Parse +CSQ: <rssi>,<ber>
    sim7080g::SignalQuality getSignalQuality(uint32_t timeout_ms = 1000);

    // Returns raw CEREG/CREG line if present.
    SIM7080G_String getNetworkStatus(uint32_t timeout_ms = 1000);

    // Wait until registered (stat 1 or 5) based on CEREG/CREG.
    bool waitForNetwork(uint32_t timeout_ms = 60000);

  private:
    M5_SIM7080G &_modem;
};

