#include "SIM7080G_Network.h"

#include <stdlib.h>
#include <string.h>

static bool _parseTwoIntsAfterColon(const char *s, int &a, int &b) {
  // Find ':' then parse "<a>,<b>"
  const char *p = s;
  while (*p && *p != ':') p++;
  if (!*p) return false;
  p++;  // past ':'
  while (*p == ' ' || *p == '\t') p++;
  char *end1 = nullptr;
  long v1 = strtol(p, &end1, 10);
  if (end1 == p) return false;
  p = end1;
  while (*p && (*p == ' ' || *p == '\t')) p++;
  if (*p != ',') return false;
  p++;
  while (*p == ' ' || *p == '\t') p++;
  char *end2 = nullptr;
  long v2 = strtol(p, &end2, 10);
  if (end2 == p) return false;
  a = static_cast<int>(v1);
  b = static_cast<int>(v2);
  return true;
}

static bool _registeredFromCeregLine(const char *line) {
  // +CEREG: <n>,<stat>[,...] or +CREG: <n>,<stat>
  int n = -1;
  int stat = -1;
  if (!_parseTwoIntsAfterColon(line, n, stat)) return false;
  return (stat == 1 || stat == 5);
}

bool SIM7080G_Network::setAPN(const SIM7080G_String &apn, SIM7080G_String user, SIM7080G_String pass, int cid) {
  // Basic CID/APN config. Auth parameters are module/firmware dependent; keep simple and common.
  // AT+CGDCONT=<cid>,"IP","<apn>"
#if !SIM7080G_USE_ESP_IDF
  SIM7080G_String cmd = SIM7080G_String("AT+CGDCONT=") + SIM7080G_String(cid) + ",\"IP\",\"" + apn + "\"";
#else
  SIM7080G_String cmd = "AT+CGDCONT=" + std::to_string(cid) + ",\"IP\",\"" + apn + "\"";
#endif
  auto r = _modem.sendCommand(cmd, 3000, true);
  if (r.status != M5_SIM7080G::Status::Ok) return false;

  // Optional auth setup (best-effort). Not all firmware supports these.
  bool hasAuth = false;
#if !SIM7080G_USE_ESP_IDF
  hasAuth = (user.length() > 0) || (pass.length() > 0);
#else
  hasAuth = (!user.empty()) || (!pass.empty());
#endif
  if (hasAuth) {
    // Try the common SIMCom form:
    // AT+CGAUTH=<cid>,<auth_type>,<user>,<pass>
    // auth_type: 1=PAP, 2=CHAP
#if !SIM7080G_USE_ESP_IDF
    SIM7080G_String cmd2 = SIM7080G_String("AT+CGAUTH=") + SIM7080G_String(cid) + ",1,\"" + user + "\",\"" + pass + "\"";
#else
    SIM7080G_String cmd2 = "AT+CGAUTH=" + std::to_string(cid) + ",1,\"" + user + "\",\"" + pass + "\"";
#endif
    (void)_modem.sendCommand(cmd2, 3000, true);
  }

  return true;
}

bool SIM7080G_Network::activatePDP(int profileId) {
#if !SIM7080G_USE_ESP_IDF
  SIM7080G_String cmd = SIM7080G_String("AT+CNACT=") + SIM7080G_String(profileId) + ",1";
#else
  SIM7080G_String cmd = "AT+CNACT=" + std::to_string(profileId) + ",1";
#endif
  auto r = _modem.sendCommand(cmd, 10000, true);
  return r.status == M5_SIM7080G::Status::Ok;
}

bool SIM7080G_Network::deactivatePDP(int profileId) {
#if !SIM7080G_USE_ESP_IDF
  SIM7080G_String cmd = SIM7080G_String("AT+CNACT=") + SIM7080G_String(profileId) + ",0";
#else
  SIM7080G_String cmd = "AT+CNACT=" + std::to_string(profileId) + ",0";
#endif
  auto r = _modem.sendCommand(cmd, 10000, true);
  return r.status == M5_SIM7080G::Status::Ok;
}

bool SIM7080G_Network::isPDPActive(int profileId) {
  auto r = _modem.sendCommand("AT+CNACT?", 2000, true);
  if (r.status != M5_SIM7080G::Status::Ok && r.status != M5_SIM7080G::Status::Timeout) return false;

  // Look for "+CNACT: <profileId>,1"
#if !SIM7080G_USE_ESP_IDF
  SIM7080G_String needle = SIM7080G_String("+CNACT: ") + SIM7080G_String(profileId) + ",1";
  return r.raw.indexOf(needle) != -1;
#else
  SIM7080G_String needle = "+CNACT: " + std::to_string(profileId) + ",1";
  return r.raw.find(needle) != std::string::npos;
#endif
}

sim7080g::SignalQuality SIM7080G_Network::getSignalQuality(uint32_t timeout_ms) {
  sim7080g::SignalQuality q{};
  auto r = _modem.sendCommand("AT+CSQ", timeout_ms, true);
  if (r.status != M5_SIM7080G::Status::Ok) return q;

  const char *s = r.raw.c_str();
  const char *p = strstr(s, "+CSQ:");
  if (!p) return q;
  int rssi = -1, ber = -1;
  if (!_parseTwoIntsAfterColon(p, rssi, ber)) return q;
  q.rssi = rssi;
  q.ber = ber;
  q.valid = true;
  return q;
}

SIM7080G_String SIM7080G_Network::getNetworkStatus(uint32_t timeout_ms) {
  auto r = _modem.sendCommand("AT+CEREG?", timeout_ms, true);
  if (r.status != M5_SIM7080G::Status::Ok && r.status != M5_SIM7080G::Status::Timeout) return SIM7080G_String{};

  // Prefer CEREG line; fallback to CREG.
  const char *s = r.raw.c_str();
  const char *p = strstr(s, "+CEREG:");
  if (p) {
    // return from p until end-of-line
    const char *e = p;
    while (*e && *e != '\r' && *e != '\n') e++;
#if !SIM7080G_USE_ESP_IDF
    return SIM7080G_String(p).substring(0, (int)(e - p));
#else
    return SIM7080G_String(p, static_cast<size_t>(e - p));
#endif
  }

  auto r2 = _modem.sendCommand("AT+CREG?", timeout_ms, true);
  const char *s2 = r2.raw.c_str();
  const char *p2 = strstr(s2, "+CREG:");
  if (p2) {
    const char *e = p2;
    while (*e && *e != '\r' && *e != '\n') e++;
#if !SIM7080G_USE_ESP_IDF
    return SIM7080G_String(p2).substring(0, (int)(e - p2));
#else
    return SIM7080G_String(p2, static_cast<size_t>(e - p2));
#endif
  }
  return SIM7080G_String{};
}

bool SIM7080G_Network::waitForNetwork(uint32_t timeout_ms) {
  const uint32_t start = sim7080g::nowMs();
  while ((sim7080g::nowMs() - start) < timeout_ms) {
    auto r = _modem.sendCommand("AT+CEREG?", 1500, true);
    if (r.status == M5_SIM7080G::Status::Ok) {
      const char *s = r.raw.c_str();
      const char *p = strstr(s, "+CEREG:");
      if (p && _registeredFromCeregLine(p)) return true;
    }

    auto r2 = _modem.sendCommand("AT+CREG?", 1500, true);
    if (r2.status == M5_SIM7080G::Status::Ok) {
      const char *s2 = r2.raw.c_str();
      const char *p2 = strstr(s2, "+CREG:");
      if (p2 && _registeredFromCeregLine(p2)) return true;
    }

    sim7080g::delayMs(1000);
  }
  return false;
}

