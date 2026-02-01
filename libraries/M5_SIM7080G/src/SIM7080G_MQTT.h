#pragma once

#include "M5_SIM7080G.h"

class SIM7080G_MQTT {
  public:
    using MessageCallback = void (*)(const SIM7080G_String &topic, const SIM7080G_String &payload);

    explicit SIM7080G_MQTT(M5_SIM7080G &modem) : _modem(modem) {}

    bool configure(const SIM7080G_String &broker, uint16_t port, const SIM7080G_String &clientId,
                   uint16_t keepAliveSeconds = 60, bool cleanSession = true);
    bool connect(uint32_t timeout_ms = 15000);
    bool disconnect(uint32_t timeout_ms = 3000);

    bool subscribe(const SIM7080G_String &topic, int qos = 1, uint32_t timeout_ms = 3000);
    bool unsubscribe(const SIM7080G_String &topic, uint32_t timeout_ms = 3000);

    bool publish(const SIM7080G_String &topic, const SIM7080G_String &payload, int qos = 1, bool retain = false,
                 uint32_t timeout_ms = 10000);

    void setCallback(MessageCallback cb) { _cb = cb; }

    // Call periodically to process incoming +SMSUB URCs.
    void poll(uint32_t capture_ms = 0);

  private:
    bool processRxBuffer();

    M5_SIM7080G &_modem;
    MessageCallback _cb = nullptr;
    SIM7080G_String _rxbuf{};
};

