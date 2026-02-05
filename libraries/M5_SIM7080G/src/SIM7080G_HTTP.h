#pragma once

#include "M5_SIM7080G.h"

class SIM7080G_HTTP {
  public:
    explicit SIM7080G_HTTP(M5_SIM7080G &modem) : _modem(modem) {}

    bool configure(const SIM7080G_String &baseUrl, uint16_t bodyLen = 1024, uint16_t headerLen = 350, uint32_t timeout_ms = 5000);
    bool connect(uint32_t timeout_ms = 10000);
    bool disconnect(uint32_t timeout_ms = 3000);
    bool connected(uint32_t timeout_ms = 1500);

    bool clearHeaders(uint32_t timeout_ms = 1500);
    bool addHeader(const SIM7080G_String &name, const SIM7080G_String &value, uint32_t timeout_ms = 2000);
    // headersBlock like: "User-Agent: foo\r\nAccept: */*\r\n"
    bool setHeaders(const SIM7080G_String &headersBlock, uint32_t timeout_ms = 3000);

    sim7080g::HttpResponse get(const SIM7080G_String &path, uint32_t timeout_ms = 30000);
    sim7080g::HttpResponse post(const SIM7080G_String &path, const SIM7080G_String &body, const SIM7080G_String &contentType,
                                uint32_t timeout_ms = 30000);

  private:
    SIM7080G_String exec(const SIM7080G_String &cmd, uint32_t timeout_ms);
    bool execOk(const SIM7080G_String &cmd, uint32_t timeout_ms);

    bool setBody(const SIM7080G_String &body, uint32_t timeout_ms);
    bool parseShreq(const SIM7080G_String &resp, int &httpStatus, int &dataLen);
    sim7080g::HttpResponse readBody(int dataLen, uint32_t timeout_ms);

    M5_SIM7080G &_modem;
};

