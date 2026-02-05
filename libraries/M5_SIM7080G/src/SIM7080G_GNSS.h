#pragma once

#include "M5_SIM7080G.h"

class SIM7080G_GNSS {
  public:
    explicit SIM7080G_GNSS(M5_SIM7080G &modem) : _modem(modem) {}

    bool powerOn(uint32_t timeout_ms = 5000);
    bool powerOff(uint32_t timeout_ms = 2000);

    // Enable/disable GNSS URCs (best-effort; exact support varies by firmware).
    bool setUpdateRate(uint32_t interval_ms, uint32_t timeout_ms = 2000);

    // Fetch +CGNSINF and parse a usable fix.
    sim7080g::GNSSFix getFix(uint32_t timeout_ms = 2000);

    // NMEA streaming control (best-effort; common SIMCom command is CGNSTST).
    bool startNMEA(uint32_t timeout_ms = 2000);
    bool stopNMEA(uint32_t timeout_ms = 2000);
    SIM7080G_String getRawNMEA(uint32_t capture_ms = 1000);

  private:
    M5_SIM7080G &_modem;
};

