#pragma once

#include "SIM7080G_Common.h"

#if SIM7080G_USE_ESP_IDF
  #include "driver/uart.h"
#endif

class M5_SIM7080G {
  public:
    using Status = sim7080g::Status;
    using AtResponse = sim7080g::AtResponse;

  public:
    M5_SIM7080G();

#if !SIM7080G_USE_ESP_IDF
    void Init(HardwareSerial *serial = &Serial2, uint8_t RX = 16, uint8_t TX = 17, uint32_t baud = 115200);
#else
    Status Init(uart_port_t port, int rxPin, int txPin, int baud = 115200, int rxBufferSize = 2048);
#endif

    bool checkStatus(uint32_t timeout_ms = 1000);
    AtResponse sendCommand(const SIM7080G_String &command, uint32_t timeout_ms = 1000, bool flush_input = true);
    bool wakeup(int attempts = 6, uint32_t timeout_ms = 1000, uint32_t inter_attempt_delay_ms = 200);

    // Back-compat API (kept for existing examples)
    SIM7080G_String waitMsg(unsigned long time_ms);
    void sendMsg(SIM7080G_String command);
    SIM7080G_String getMsg();
    SIM7080G_String send_and_getMsg(SIM7080G_String str, uint32_t timeout_ms = 1000);

    void flushInput();
    int available();

  private:
    uint32_t nowMs() const;
    void delayMs(uint32_t ms) const;
    int readSome(uint8_t *buf, size_t maxLen, uint32_t timeout_ms);
    bool writeAll(const uint8_t *buf, size_t len);

#if !SIM7080G_USE_ESP_IDF
    HardwareSerial *_serial = nullptr;
#else
    uart_port_t _uart_port = UART_NUM_1;
    bool _idf_uart_ready = false;
#endif
};

// Convenience includes so users can just `#include <M5_SIM7080G.h>`
#include "SIM7080G_Network.h"
#include "SIM7080G_GNSS.h"
#include "SIM7080G_MQTT.h"
#include "SIM7080G_HTTP.h"