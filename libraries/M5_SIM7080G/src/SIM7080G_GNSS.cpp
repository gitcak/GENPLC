#include "SIM7080G_GNSS.h"

#include <stdlib.h>
#include <string.h>

static bool _nextField(const char *&p, const char *&start, size_t &len) {
  if (!p) return false;
  // Skip leading spaces/commas
  while (*p == ' ' || *p == '\t' || *p == ',') p++;
  if (!*p) return false;
  start = p;
  while (*p && *p != ',' && *p != '\r' && *p != '\n') p++;
  len = static_cast<size_t>(p - start);
  if (*p == ',') p++;
  return true;
}

static bool _toInt(const char *start, size_t len, int &out) {
  if (!start || len == 0) return false;
  char tmp[24];
  if (len >= sizeof(tmp)) len = sizeof(tmp) - 1;
  memcpy(tmp, start, len);
  tmp[len] = 0;
  char *end = nullptr;
  long v = strtol(tmp, &end, 10);
  if (end == tmp) return false;
  out = static_cast<int>(v);
  return true;
}

static bool _toDouble(const char *start, size_t len, double &out) {
  if (!start || len == 0) return false;
  char tmp[32];
  if (len >= sizeof(tmp)) len = sizeof(tmp) - 1;
  memcpy(tmp, start, len);
  tmp[len] = 0;
  char *end = nullptr;
  double v = strtod(tmp, &end);
  if (end == tmp) return false;
  out = v;
  return true;
}

bool SIM7080G_GNSS::powerOn(uint32_t timeout_ms) {
  auto r = _modem.sendCommand("AT+CGNSPWR=1", timeout_ms, true);
  return r.status == M5_SIM7080G::Status::Ok;
}

bool SIM7080G_GNSS::powerOff(uint32_t timeout_ms) {
  auto r = _modem.sendCommand("AT+CGNSPWR=0", timeout_ms, true);
  return r.status == M5_SIM7080G::Status::Ok;
}

bool SIM7080G_GNSS::setUpdateRate(uint32_t interval_ms, uint32_t timeout_ms) {
  // Best-effort: many SIMCom firmwares support AT+CGNSURC=0 / AT+CGNSURC=1,<sec>
  if (interval_ms == 0) {
    auto r = _modem.sendCommand("AT+CGNSURC=0", timeout_ms, true);
    return r.status == M5_SIM7080G::Status::Ok;
  }
  uint32_t sec = interval_ms / 1000;
  if (sec == 0) sec = 1;

#if !SIM7080G_USE_ESP_IDF
  SIM7080G_String cmd = SIM7080G_String("AT+CGNSURC=1,") + SIM7080G_String(sec);
#else
  SIM7080G_String cmd = "AT+CGNSURC=1," + std::to_string(sec);
#endif
  auto r = _modem.sendCommand(cmd, timeout_ms, true);
  return r.status == M5_SIM7080G::Status::Ok;
}

sim7080g::GNSSFix SIM7080G_GNSS::getFix(uint32_t timeout_ms) {
  sim7080g::GNSSFix fix{};
  auto r = _modem.sendCommand("AT+CGNSINF", timeout_ms, true);
  if (r.status != M5_SIM7080G::Status::Ok) return fix;

  const char *raw = r.raw.c_str();
  const char *p = strstr(raw, "+CGNSINF:");
  if (!p) return fix;
  p = strchr(p, ':');
  if (!p) return fix;
  p++;  // past ':'

  // Fields: <run>,<fix>,<utc>,<lat>,<lon>,<alt>,<speed>,<course>,...
  const char *s = nullptr;
  size_t len = 0;

  int run = 0;
  int fixStat = 0;
  if (!_nextField(p, s, len) || !_toInt(s, len, run)) return fix;
  if (!_nextField(p, s, len) || !_toInt(s, len, fixStat)) return fix;

  // UTC
  if (_nextField(p, s, len)) {
#if !SIM7080G_USE_ESP_IDF
    fix.utc = SIM7080G_String(s).substring(0, (int)len);
#else
    fix.utc = SIM7080G_String(s, len);
#endif
  }

  double lat = 0, lon = 0, alt = 0, spd = 0, cog = 0;
  if (_nextField(p, s, len)) (void)_toDouble(s, len, lat);
  if (_nextField(p, s, len)) (void)_toDouble(s, len, lon);
  if (_nextField(p, s, len)) (void)_toDouble(s, len, alt);
  if (_nextField(p, s, len)) (void)_toDouble(s, len, spd);
  if (_nextField(p, s, len)) (void)_toDouble(s, len, cog);

  // Skip Fix Mode, Reserved1
  (void)_nextField(p, s, len);
  (void)_nextField(p, s, len);

  double hd = 0.0, pd = 0.0, vd = 0.0;
  if (_nextField(p, s, len)) (void)_toDouble(s, len, hd);
  if (_nextField(p, s, len)) (void)_toDouble(s, len, pd);
  if (_nextField(p, s, len)) (void)_toDouble(s, len, vd);

  // Skip Reserved2
  (void)_nextField(p, s, len);

  int sats = 0;
  if (_nextField(p, s, len)) (void)_toInt(s, len, sats);

  fix.latitude = lat;
  fix.longitude = lon;
  fix.altitude_m = alt;
  fix.speed_kph = spd;
  fix.course_deg = cog;
  fix.hdop = static_cast<float>(hd);
  fix.pdop = static_cast<float>(pd);
  fix.vdop = static_cast<float>(vd);
  fix.satellites = static_cast<uint8_t>(sats < 0 ? 0 : (sats > 255 ? 255 : sats));
  fix.valid = (run != 0) && (fixStat != 0);
  return fix;
}

bool SIM7080G_GNSS::startNMEA(uint32_t timeout_ms) {
  auto r = _modem.sendCommand("AT+CGNSTST=1", timeout_ms, true);
  return r.status == M5_SIM7080G::Status::Ok;
}

bool SIM7080G_GNSS::stopNMEA(uint32_t timeout_ms) {
  auto r = _modem.sendCommand("AT+CGNSTST=0", timeout_ms, true);
  return r.status == M5_SIM7080G::Status::Ok;
}

SIM7080G_String SIM7080G_GNSS::getRawNMEA(uint32_t capture_ms) {
  return _modem.waitMsg(capture_ms);
}
