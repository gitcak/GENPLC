#include "SIM7080G_HTTP.h"

#include <stdlib.h>
#include <string.h>

#if !SIM7080G_USE_ESP_IDF
static inline bool _contains(const SIM7080G_String &s, const char *tok) { return s.indexOf(tok) != -1; }
#else
static inline bool _contains(const SIM7080G_String &s, const char *tok) { return s.find(tok) != std::string::npos; }
#endif

SIM7080G_String SIM7080G_HTTP::exec(const SIM7080G_String &cmd, uint32_t timeout_ms) {
#if !SIM7080G_USE_ESP_IDF
  SIM7080G_String line = cmd;
  if (!line.endsWith("\r\n")) line += "\r\n";
#else
  SIM7080G_String line = cmd;
  if (!(line.size() >= 2 && line[line.size() - 2] == '\r' && line[line.size() - 1] == '\n')) line.append("\r\n");
#endif
  _modem.flushInput();
  _modem.sendMsg(line);
  return _modem.waitMsg(timeout_ms);
}

bool SIM7080G_HTTP::execOk(const SIM7080G_String &cmd, uint32_t timeout_ms) {
  const auto r = exec(cmd, timeout_ms);
  return _contains(r, "OK") && !_contains(r, "ERROR");
}

bool SIM7080G_HTTP::configure(const SIM7080G_String &baseUrl, uint16_t bodyLen, uint16_t headerLen, uint32_t timeout_ms) {
  // AT+SHCONF="URL","http://example.com"
#if !SIM7080G_USE_ESP_IDF
  SIM7080G_String cmd = SIM7080G_String("AT+SHCONF=\"URL\",\"") + baseUrl + "\"";
  if (!execOk(cmd, timeout_ms)) return false;
  if (!execOk(SIM7080G_String("AT+SHCONF=\"BODYLEN\",") + SIM7080G_String(bodyLen), timeout_ms)) return false;
  if (!execOk(SIM7080G_String("AT+SHCONF=\"HEADERLEN\",") + SIM7080G_String(headerLen), timeout_ms)) return false;
#else
  SIM7080G_String cmd = "AT+SHCONF=\"URL\",\"" + baseUrl + "\"";
  if (!execOk(cmd, timeout_ms)) return false;
  if (!execOk("AT+SHCONF=\"BODYLEN\"," + std::to_string(bodyLen), timeout_ms)) return false;
  if (!execOk("AT+SHCONF=\"HEADERLEN\"," + std::to_string(headerLen), timeout_ms)) return false;
#endif
  return true;
}

bool SIM7080G_HTTP::connect(uint32_t timeout_ms) { return execOk("AT+SHCONN", timeout_ms); }

bool SIM7080G_HTTP::disconnect(uint32_t timeout_ms) { return execOk("AT+SHDISC", timeout_ms); }

bool SIM7080G_HTTP::connected(uint32_t timeout_ms) {
  auto r = exec("AT+SHSTATE?", timeout_ms);
  // +SHSTATE: 1 => connected
  return _contains(r, "+SHSTATE: 1");
}

bool SIM7080G_HTTP::clearHeaders(uint32_t timeout_ms) { return execOk("AT+SHCHEAD", timeout_ms); }

bool SIM7080G_HTTP::addHeader(const SIM7080G_String &name, const SIM7080G_String &value, uint32_t timeout_ms) {
#if !SIM7080G_USE_ESP_IDF
  SIM7080G_String cmd = SIM7080G_String("AT+SHAHEAD=\"") + name + "\",\"" + value + "\"";
#else
  SIM7080G_String cmd = "AT+SHAHEAD=\"" + name + "\",\"" + value + "\"";
#endif
  return execOk(cmd, timeout_ms);
}

bool SIM7080G_HTTP::setHeaders(const SIM7080G_String &headersBlock, uint32_t timeout_ms) {
  // Parse "Name: Value" lines.
#if !SIM7080G_USE_ESP_IDF
  int start = 0;
  while (start < headersBlock.length()) {
    int end = headersBlock.indexOf('\n', start);
    if (end == -1) end = headersBlock.length();
    String line = headersBlock.substring(start, end);
    line.trim();
    if (line.length() > 0) {
      int colon = line.indexOf(':');
      if (colon > 0) {
        String k = line.substring(0, colon);
        String v = line.substring(colon + 1);
        k.trim();
        v.trim();
        if (!addHeader(k, v, timeout_ms)) return false;
      }
    }
    start = end + 1;
  }
  return true;
#else
  size_t start = 0;
  while (start < headersBlock.size()) {
    size_t end = headersBlock.find('\n', start);
    if (end == std::string::npos) end = headersBlock.size();
    std::string line = headersBlock.substr(start, end - start);
    // trim
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' ' || line.back() == '\t')) line.pop_back();
    size_t i = 0;
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) i++;
    line.erase(0, i);

    if (!line.empty()) {
      const size_t colon = line.find(':');
      if (colon != std::string::npos && colon > 0) {
        std::string k = line.substr(0, colon);
        std::string v = line.substr(colon + 1);
        // trim spaces
        while (!k.empty() && (k.back() == ' ' || k.back() == '\t')) k.pop_back();
        while (!v.empty() && (v.front() == ' ' || v.front() == '\t')) v.erase(0, 1);
        if (!addHeader(k, v, timeout_ms)) return false;
      }
    }
    start = end + 1;
  }
  return true;
#endif
}

bool SIM7080G_HTTP::setBody(const SIM7080G_String &body, uint32_t timeout_ms) {
  // Two-phase: AT+SHBOD=<len>,<timeout> then send body.
#if !SIM7080G_USE_ESP_IDF
  const int len = body.length();
  SIM7080G_String cmd = SIM7080G_String("AT+SHBOD=") + SIM7080G_String(len) + "," + SIM7080G_String(timeout_ms);
#else
  const int len = static_cast<int>(body.size());
  SIM7080G_String cmd = "AT+SHBOD=" + std::to_string(len) + "," + std::to_string(timeout_ms);
#endif

  _modem.flushInput();
#if !SIM7080G_USE_ESP_IDF
  _modem.sendMsg(cmd + "\r\n");
#else
  _modem.sendMsg(cmd + "\r\n");
#endif
  (void)_modem.waitMsg(500);  // prompt '>'
  _modem.sendMsg(body);

  const auto resp = _modem.waitMsg(timeout_ms);
  return _contains(resp, "OK") && !_contains(resp, "ERROR");
}

bool SIM7080G_HTTP::parseShreq(const SIM7080G_String &resp, int &httpStatus, int &dataLen) {
  httpStatus = -1;
  dataLen = -1;
  const char *s = resp.c_str();
  const char *p = strstr(s, "+SHREQ:");
  if (!p) return false;
  // +SHREQ: "GET",200,387
  const char *comma1 = strchr(p, ',');
  if (!comma1) return false;
  const char *comma2 = strchr(comma1 + 1, ',');
  if (!comma2) return false;
  httpStatus = atoi(comma1 + 1);
  dataLen = atoi(comma2 + 1);
  return httpStatus >= 0 && dataLen >= 0;
}

sim7080g::HttpResponse SIM7080G_HTTP::readBody(int dataLen, uint32_t timeout_ms) {
  sim7080g::HttpResponse out{};
  out.status = sim7080g::Status::Timeout;
  out.http_status = -1;

  if (dataLen <= 0) {
    out.status = sim7080g::Status::Ok;
    return out;
  }

#if !SIM7080G_USE_ESP_IDF
  SIM7080G_String cmd = SIM7080G_String("AT+SHREAD=0,") + SIM7080G_String(dataLen);
#else
  SIM7080G_String cmd = "AT+SHREAD=0," + std::to_string(dataLen);
#endif
  const auto resp = exec(cmd, timeout_ms);
  const char *s = resp.c_str();
  const char *p = strstr(s, "+SHREAD:");
  if (!p) return out;
  const char *nl = strchr(p, '\n');
  if (!nl) return out;
  nl++;  // body starts after this newline

  // Copy exactly dataLen bytes when possible; otherwise best-effort rest of buffer.
  const size_t available = strlen(nl);
  const size_t take = (available >= static_cast<size_t>(dataLen)) ? static_cast<size_t>(dataLen) : available;

#if !SIM7080G_USE_ESP_IDF
  out.body = SIM7080G_String(nl).substring(0, (int)take);
#else
  out.body = SIM7080G_String(nl, take);
#endif
  out.status = sim7080g::Status::Ok;
  return out;
}

sim7080g::HttpResponse SIM7080G_HTTP::get(const SIM7080G_String &path, uint32_t timeout_ms) {
  sim7080g::HttpResponse out{};
  out.status = sim7080g::Status::Error;

  // Ensure connection
  (void)connect(10000);
  (void)clearHeaders();
  (void)addHeader("Accept", "*/*");
  (void)addHeader("Connection", "keep-alive");

  // AT+SHREQ="<path>",1 (GET)
#if !SIM7080G_USE_ESP_IDF
  SIM7080G_String cmd = SIM7080G_String("AT+SHREQ=\"") + path + "\",1";
#else
  SIM7080G_String cmd = "AT+SHREQ=\"" + path + "\",1";
#endif
  const auto resp = exec(cmd, timeout_ms);
  int httpStatus = -1, dataLen = -1;
  if (!parseShreq(resp, httpStatus, dataLen)) {
    out.status = sim7080g::Status::Error;
    return out;
  }
  out.http_status = httpStatus;

  auto body = readBody(dataLen, timeout_ms);
  body.http_status = httpStatus;
  return body;
}

sim7080g::HttpResponse SIM7080G_HTTP::post(const SIM7080G_String &path, const SIM7080G_String &body, const SIM7080G_String &contentType,
                                           uint32_t timeout_ms) {
  sim7080g::HttpResponse out{};
  out.status = sim7080g::Status::Error;

  (void)connect(10000);
  (void)clearHeaders();
  (void)addHeader("Accept", "*/*");
  (void)addHeader("Connection", "keep-alive");
  (void)addHeader("Content-Type", contentType);

  if (!setBody(body, 10000)) {
    out.status = sim7080g::Status::Error;
    return out;
  }

  // AT+SHREQ="<path>",3 (POST)
#if !SIM7080G_USE_ESP_IDF
  SIM7080G_String cmd = SIM7080G_String("AT+SHREQ=\"") + path + "\",3";
#else
  SIM7080G_String cmd = "AT+SHREQ=\"" + path + "\",3";
#endif
  const auto resp = exec(cmd, timeout_ms);
  int httpStatus = -1, dataLen = -1;
  if (!parseShreq(resp, httpStatus, dataLen)) {
    out.status = sim7080g::Status::Error;
    return out;
  }
  out.http_status = httpStatus;

  auto bodyResp = readBody(dataLen, timeout_ms);
  bodyResp.http_status = httpStatus;
  return bodyResp;
}

