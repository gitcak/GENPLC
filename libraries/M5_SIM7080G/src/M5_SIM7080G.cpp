#include "M5_SIM7080G.h"

#if !SIM7080G_USE_ESP_IDF
static inline bool _endsWithCRLF(const SIM7080G_String &s) {
    const int n = s.length();
    return n >= 2 && s[n - 2] == '\r' && s[n - 1] == '\n';
}
static inline bool _isEmpty(const SIM7080G_String &s) {
    return s.length() == 0;
}
static inline bool _containsToken(const SIM7080G_String &s, const char *token) {
    return s.indexOf(token) != -1;
}
#else
static inline bool _endsWithCRLF(const SIM7080G_String &s) {
    const size_t n = s.size();
    return n >= 2 && s[n - 2] == '\r' && s[n - 1] == '\n';
}
static inline bool _isEmpty(const SIM7080G_String &s) {
    return s.empty();
}
static inline bool _containsToken(const SIM7080G_String &s, const char *token) {
    return s.find(token) != std::string::npos;
}
#endif

M5_SIM7080G::M5_SIM7080G() = default;

#if !SIM7080G_USE_ESP_IDF
void M5_SIM7080G::Init(HardwareSerial *serial, uint8_t RX, uint8_t TX, uint32_t baud) {
    _serial = serial;
    if (_serial) {
        _serial->begin(baud, SERIAL_8N1, RX, TX);
    }
}
#else
M5_SIM7080G::Status M5_SIM7080G::Init(uart_port_t port, int rxPin, int txPin, int baud, int rxBufferSize) {
    _uart_port = port;

    uart_config_t cfg{};
    cfg.baud_rate = baud;
    cfg.data_bits = UART_DATA_8_BITS;
    cfg.parity = UART_PARITY_DISABLE;
    cfg.stop_bits = UART_STOP_BITS_1;
    cfg.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    cfg.source_clk = UART_SCLK_DEFAULT;

    if (uart_param_config(_uart_port, &cfg) != ESP_OK) return Status::TransportError;
    if (uart_set_pin(_uart_port, txPin, rxPin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK) return Status::TransportError;
    if (uart_driver_install(_uart_port, rxBufferSize, 0, 0, nullptr, 0) != ESP_OK) return Status::TransportError;
    _idf_uart_ready = true;
    return Status::Ok;
}
#endif

uint32_t M5_SIM7080G::nowMs() const {
#if !SIM7080G_USE_ESP_IDF
    return ::millis();
#else
    return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
#endif
}

void M5_SIM7080G::delayMs(uint32_t ms) const {
#if !SIM7080G_USE_ESP_IDF
    ::delay(ms);
#else
    vTaskDelay(pdMS_TO_TICKS(ms));
#endif
}

int M5_SIM7080G::available() {
#if !SIM7080G_USE_ESP_IDF
    if (!_serial) return 0;
    return _serial->available();
#else
    if (!_idf_uart_ready) return 0;
    size_t len = 0;
    if (uart_get_buffered_data_len(_uart_port, &len) != ESP_OK) return 0;
    return static_cast<int>(len);
#endif
}

int M5_SIM7080G::readSome(uint8_t *buf, size_t maxLen, uint32_t timeout_ms) {
#if !SIM7080G_USE_ESP_IDF
    if (!_serial || !buf || maxLen == 0) return 0;

    const uint32_t start = nowMs();
    size_t n = 0;
    while (n < maxLen && (nowMs() - start) < timeout_ms) {
        int a = _serial->available();
        while (a-- > 0 && n < maxLen) {
            int b = _serial->read();
            if (b < 0) break;
            buf[n++] = static_cast<uint8_t>(b);
        }
        if (n > 0) break;
        delayMs(1);
    }
    return static_cast<int>(n);
#else
    if (!_idf_uart_ready || !buf || maxLen == 0) return 0;
    const int n = uart_read_bytes(_uart_port, buf, maxLen, pdMS_TO_TICKS(timeout_ms));
    return n > 0 ? n : 0;
#endif
}

bool M5_SIM7080G::writeAll(const uint8_t *buf, size_t len) {
    if (!buf || len == 0) return true;
#if !SIM7080G_USE_ESP_IDF
    if (!_serial) return false;
    const size_t n = _serial->write(buf, len);
    _serial->flush();
    return n == len;
#else
    if (!_idf_uart_ready) return false;
    const int n = uart_write_bytes(_uart_port, reinterpret_cast<const char *>(buf), len);
    return n == static_cast<int>(len);
#endif
}

void M5_SIM7080G::flushInput() {
#if !SIM7080G_USE_ESP_IDF
    if (!_serial) return;
    while (_serial->available() > 0) (void)_serial->read();
#else
    if (!_idf_uart_ready) return;
    (void)uart_flush_input(_uart_port);
#endif
}

M5_SIM7080G::AtResponse M5_SIM7080G::sendCommand(const SIM7080G_String &command, uint32_t timeout_ms, bool flush_input) {
    if (flush_input) flushInput();

#if !SIM7080G_USE_ESP_IDF
    SIM7080G_String cmd = command;
    if (!_endsWithCRLF(cmd)) cmd += "\r\n";
#else
    SIM7080G_String cmd = command;
    if (!_endsWithCRLF(cmd)) cmd.append("\r\n");
#endif

#if !SIM7080G_USE_ESP_IDF
    const uint8_t *cmdBytes = reinterpret_cast<const uint8_t *>(cmd.c_str());
    const size_t cmdLen = cmd.length();
#else
    const uint8_t *cmdBytes = reinterpret_cast<const uint8_t *>(cmd.data());
    const size_t cmdLen = cmd.size();
#endif

    if (!writeAll(cmdBytes, cmdLen)) {
        AtResponse out{};
        out.status = Status::TransportError;
        out.raw = SIM7080G_String{};
        return out;
    }

    SIM7080G_String resp;
    const uint32_t start = nowMs();
    uint32_t last_rx = start;

    while ((nowMs() - start) < timeout_ms) {
        uint8_t buf[256];
        const int n = readSome(buf, sizeof(buf), 50);
        if (n > 0) {
#if !SIM7080G_USE_ESP_IDF
            for (int i = 0; i < n; i++) resp += static_cast<char>(buf[i]);
#else
            resp.append(reinterpret_cast<const char *>(buf), static_cast<size_t>(n));
#endif
            last_rx = nowMs();
            if (_containsToken(resp, "\r\nOK\r\n") || _containsToken(resp, "\nOK\r\n") || _containsToken(resp, "\r\nOK\n") || _containsToken(resp, "OK\r\n")) {
                AtResponse out{};
                out.status = Status::Ok;
                out.raw = resp;
                return out;
            }
            if (_containsToken(resp, "ERROR")) {
                AtResponse out{};
                out.status = Status::Error;
                out.raw = resp;
                return out;
            }
        } else {
            // If we've received something and it's gone quiet briefly, return it (caller may parse URCs)
            if (!_isEmpty(resp) && (nowMs() - last_rx) > 120) {
                // Not a hard OK/ERROR, but don't block forever for chatty URCs.
                break;
            }
            delayMs(2);
        }
    }
    AtResponse out{};
    out.status = Status::Timeout;
    out.raw = resp;
    return out;
}

bool M5_SIM7080G::checkStatus(uint32_t timeout_ms) {
    const auto r = sendCommand("AT", timeout_ms, true);
    return r.status == Status::Ok;
}

bool M5_SIM7080G::wakeup(int attempts, uint32_t timeout_ms, uint32_t inter_attempt_delay_ms) {
    if (attempts < 1) attempts = 1;
    for (int i = 0; i < attempts; i++) {
        if (checkStatus(timeout_ms)) return true;
        delayMs(inter_attempt_delay_ms);
    }
    return false;
}

SIM7080G_String M5_SIM7080G::waitMsg(unsigned long time) {
    // Back-compat behavior:
    // - time == 0: non-blocking read (whatever is available right now)
    // - time > 0 : read until timeout, returning whatever arrived
    if (time == 0) {
        return getMsg();
    }

    SIM7080G_String resp;
    const uint32_t start = nowMs();
    uint32_t last_rx = start;
    while ((nowMs() - start) < static_cast<uint32_t>(time)) {
        uint8_t buf[256];
        const int n = readSome(buf, sizeof(buf), 50);
        if (n > 0) {
#if !SIM7080G_USE_ESP_IDF
            for (int i = 0; i < n; i++) resp += static_cast<char>(buf[i]);
#else
            resp.append(reinterpret_cast<const char *>(buf), static_cast<size_t>(n));
#endif
            last_rx = nowMs();
        } else {
            if (!_isEmpty(resp) && (nowMs() - last_rx) > 120) break;
            delayMs(2);
        }
    }
    return resp;
}

void M5_SIM7080G::sendMsg(SIM7080G_String command) {
#if !SIM7080G_USE_ESP_IDF
    if (command.length() == 0) return;
    (void)writeAll(reinterpret_cast<const uint8_t *>(command.c_str()), command.length());
#else
    if (command.empty()) return;
    (void)writeAll(reinterpret_cast<const uint8_t *>(command.data()), command.size());
#endif
}


SIM7080G_String M5_SIM7080G::getMsg() {
    SIM7080G_String resp;
    while (available() > 0) {
        uint8_t buf[256];
        const int n = readSome(buf, sizeof(buf), 10);
        if (n <= 0) break;
#if !SIM7080G_USE_ESP_IDF
        for (int i = 0; i < n; i++) resp += static_cast<char>(buf[i]);
#else
        resp.append(reinterpret_cast<const char *>(buf), static_cast<size_t>(n));
#endif
    }
    return resp;
}   

SIM7080G_String M5_SIM7080G::send_and_getMsg(SIM7080G_String str, uint32_t timeout_ms) {
    const auto r = sendCommand(str, timeout_ms, true);
    return r.raw;
}

