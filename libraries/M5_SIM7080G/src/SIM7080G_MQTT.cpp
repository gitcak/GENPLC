#include "SIM7080G_MQTT.h"

#include <stdlib.h>
#include <string.h>

#if !SIM7080G_USE_ESP_IDF
static inline int _idxOf(const SIM7080G_String &s, const char *needle, int from = 0) { return s.indexOf(needle, from); }
static inline int _idxOfChar(const SIM7080G_String &s, char c, int from = 0) { return s.indexOf(c, from); }
static inline SIM7080G_String _substr(const SIM7080G_String &s, int start, int endExclusive) { return s.substring(start, endExclusive); }
static inline void _erasePrefix(SIM7080G_String &s, int n) { s.remove(0, n); }
static inline int _len(const SIM7080G_String &s) { return s.length(); }
#else
static inline int _idxOf(const SIM7080G_String &s, const char *needle, int from = 0) {
  const size_t pos = s.find(needle, static_cast<size_t>(from));
  return pos == std::string::npos ? -1 : static_cast<int>(pos);
}
static inline int _idxOfChar(const SIM7080G_String &s, char c, int from = 0) {
  const size_t pos = s.find(c, static_cast<size_t>(from));
  return pos == std::string::npos ? -1 : static_cast<int>(pos);
}
static inline SIM7080G_String _substr(const SIM7080G_String &s, int start, int endExclusive) {
  if (start < 0) start = 0;
  if (endExclusive < start) endExclusive = start;
  return s.substr(static_cast<size_t>(start), static_cast<size_t>(endExclusive - start));
}
static inline void _erasePrefix(SIM7080G_String &s, int n) { s.erase(0, static_cast<size_t>(n)); }
static inline int _len(const SIM7080G_String &s) { return static_cast<int>(s.size()); }
#endif

static bool _sendSmconf(M5_SIM7080G &m, const SIM7080G_String &k, const SIM7080G_String &v, uint32_t timeout_ms) {
#if !SIM7080G_USE_ESP_IDF
  SIM7080G_String cmd = SIM7080G_String("AT+SMCONF=\"") + k + "\",\"" + v + "\"";
#else
  SIM7080G_String cmd = "AT+SMCONF=\"" + k + "\",\"" + v + "\"";
#endif
  return m.sendCommand(cmd, timeout_ms, true).status == M5_SIM7080G::Status::Ok;
}

static bool _sendSmconfInt(M5_SIM7080G &m, const SIM7080G_String &k, int v, uint32_t timeout_ms) {
#if !SIM7080G_USE_ESP_IDF
  SIM7080G_String cmd = SIM7080G_String("AT+SMCONF=\"") + k + "\"," + SIM7080G_String(v);
#else
  SIM7080G_String cmd = "AT+SMCONF=\"" + k + "\"," + std::to_string(v);
#endif
  return m.sendCommand(cmd, timeout_ms, true).status == M5_SIM7080G::Status::Ok;
}

bool SIM7080G_MQTT::configure(const SIM7080G_String &broker, uint16_t port, const SIM7080G_String &clientId,
                              uint16_t keepAliveSeconds, bool cleanSession) {
  // URL: broker + port as separate args: AT+SMCONF="URL","broker","1883"
#if !SIM7080G_USE_ESP_IDF
  SIM7080G_String cmd = SIM7080G_String("AT+SMCONF=\"URL\",\"") + broker + "\",\"" + SIM7080G_String(port) + "\"";
#else
  SIM7080G_String cmd = "AT+SMCONF=\"URL\",\"" + broker + "\",\"" + std::to_string(port) + "\"";
#endif
  if (_modem.sendCommand(cmd, 5000, true).status != M5_SIM7080G::Status::Ok) return false;

  if (!_sendSmconfInt(_modem, "KEEPTIME", (int)keepAliveSeconds, 5000)) return false;
  if (!_sendSmconfInt(_modem, "CLEANSS", cleanSession ? 1 : 0, 5000)) return false;
  if (!_sendSmconf(_modem, "CLIENTID", clientId, 5000)) return false;
  return true;
}

bool SIM7080G_MQTT::connect(uint32_t timeout_ms) {
  auto r = _modem.sendCommand("AT+SMCONN", timeout_ms, true);
  return r.status == M5_SIM7080G::Status::Ok;
}

bool SIM7080G_MQTT::disconnect(uint32_t timeout_ms) {
  auto r = _modem.sendCommand("AT+SMDISC", timeout_ms, true);
  return r.status == M5_SIM7080G::Status::Ok;
}

bool SIM7080G_MQTT::subscribe(const SIM7080G_String &topic, int qos, uint32_t timeout_ms) {
#if !SIM7080G_USE_ESP_IDF
  SIM7080G_String cmd = SIM7080G_String("AT+SMSUB=\"") + topic + "\"," + SIM7080G_String(qos);
#else
  SIM7080G_String cmd = "AT+SMSUB=\"" + topic + "\"," + std::to_string(qos);
#endif
  auto r = _modem.sendCommand(cmd, timeout_ms, true);
  return r.status == M5_SIM7080G::Status::Ok;
}

bool SIM7080G_MQTT::unsubscribe(const SIM7080G_String &topic, uint32_t timeout_ms) {
#if !SIM7080G_USE_ESP_IDF
  SIM7080G_String cmd = SIM7080G_String("AT+SMUNSUB=\"") + topic + "\"";
#else
  SIM7080G_String cmd = "AT+SMUNSUB=\"" + topic + "\"";
#endif
  auto r = _modem.sendCommand(cmd, timeout_ms, true);
  return r.status == M5_SIM7080G::Status::Ok;
}

bool SIM7080G_MQTT::publish(const SIM7080G_String &topic, const SIM7080G_String &payload, int qos, bool retain, uint32_t timeout_ms) {
  // SMPUB is typically a two-phase command: send header, wait for '>' prompt, then send exactly <len> bytes.
#if !SIM7080G_USE_ESP_IDF
  const int len = payload.length();
  SIM7080G_String header = SIM7080G_String("AT+SMPUB=\"") + topic + "\"," + SIM7080G_String(len) + "," + SIM7080G_String(qos) + "," + SIM7080G_String(retain ? 1 : 0) + "\r\n";
#else
  const int len = static_cast<int>(payload.size());
  SIM7080G_String header = "AT+SMPUB=\"" + topic + "\"," + std::to_string(len) + "," + std::to_string(qos) + "," + std::to_string(retain ? 1 : 0) + "\r\n";
#endif

  _modem.flushInput();
  _modem.sendMsg(header);

  // Wait briefly for prompt (some firmwares don't echo a clear '>' prompt; tolerate).
  (void)_modem.waitMsg(500);

  _modem.sendMsg(payload);  // exact payload bytes

  // Now wait for OK/ERROR after payload
  SIM7080G_String resp = _modem.waitMsg(timeout_ms);
#if !SIM7080G_USE_ESP_IDF
  return resp.indexOf("OK") != -1 && resp.indexOf("ERROR") == -1;
#else
  return resp.find("OK") != std::string::npos && resp.find("ERROR") == std::string::npos;
#endif
}

void SIM7080G_MQTT::poll(uint32_t capture_ms) {
  SIM7080G_String chunk = (capture_ms == 0) ? _modem.getMsg() : _modem.waitMsg(capture_ms);
#if !SIM7080G_USE_ESP_IDF
  if (chunk.length() == 0) return;
#else
  if (chunk.empty()) return;
#endif

  // Append and process
#if !SIM7080G_USE_ESP_IDF
  _rxbuf += chunk;
#else
  _rxbuf.append(chunk);
#endif
  (void)processRxBuffer();
}

bool SIM7080G_MQTT::processRxBuffer() {
  if (!_cb) return false;

  bool any = false;
  while (true) {
    const int i = _idxOf(_rxbuf, "+SMSUB:", 0);
    if (i < 0) break;

    const int headerEnd = _idxOfChar(_rxbuf, '\n', i);
    if (headerEnd < 0) break;  // need more data

    const SIM7080G_String header = _substr(_rxbuf, i, headerEnd);

    // Topic between first pair of quotes
    int q1 = _idxOfChar(header, '"', 0);
    int q2 = (q1 >= 0) ? _idxOfChar(header, '"', q1 + 1) : -1;
    if (q1 < 0 || q2 < 0) {
      // malformed; drop up to headerEnd
      _erasePrefix(_rxbuf, headerEnd + 1);
      continue;
    }

    const SIM7080G_String topic = _substr(header, q1 + 1, q2);

    // Length is typically the last integer in the header.
    int payloadLen = -1;
    {
      const char *h = header.c_str();
      const char *lastComma = strrchr(h, ',');
      if (lastComma) {
        payloadLen = atoi(lastComma + 1);
      }
    }
    if (payloadLen < 0) {
      _erasePrefix(_rxbuf, headerEnd + 1);
      continue;
    }

    const int payloadStart = headerEnd + 1;
    if (_len(_rxbuf) < payloadStart + payloadLen) break;  // need more data

    const SIM7080G_String payload = _substr(_rxbuf, payloadStart, payloadStart + payloadLen);
    _cb(topic, payload);
    any = true;

    int consume = payloadStart + payloadLen;
    // Optional trailing CRLF after payload
    if (_len(_rxbuf) >= consume + 2) {
      const SIM7080G_String tail = _substr(_rxbuf, consume, consume + 2);
#if !SIM7080G_USE_ESP_IDF
      if (tail == "\r\n") consume += 2;
#else
      if (tail == "\r\n") consume += 2;
#endif
    }
    _erasePrefix(_rxbuf, consume);
  }
  return any;
}

