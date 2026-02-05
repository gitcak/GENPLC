#pragma once

#include <stdint.h>

// Decide backend:
// - ESP-IDF: ESP_IDF_VERSION / ESP_PLATFORM defined and ARDUINO not defined
// - Arduino: ARDUINO defined
// - Allow explicit override via SIM7080G_USE_ESP_IDF
#if !defined(SIM7080G_USE_ESP_IDF)
  #if (defined(ESP_IDF_VERSION) || defined(ESP_PLATFORM)) && !defined(ARDUINO)
    #define SIM7080G_USE_ESP_IDF 1
  #else
    #define SIM7080G_USE_ESP_IDF 0
  #endif
#endif

#if SIM7080G_USE_ESP_IDF
  #include <string>
  #include "esp_timer.h"
  #include "freertos/FreeRTOS.h"
  #include "freertos/task.h"
  namespace sim7080g {
    using String = std::string;
    inline uint32_t nowMs() { return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL); }
    inline void delayMs(uint32_t ms) { vTaskDelay(pdMS_TO_TICKS(ms)); }
  }  // namespace sim7080g
#else
  #if __has_include(<Arduino.h>)
    #include <Arduino.h>
  #else
    // Local stub (keeps IntelliSense/clangd happy on desktops)
    #include "../Arduino.h"
  #endif
  namespace sim7080g {
    using String = ::String;
    inline uint32_t nowMs() { return ::millis(); }
    inline void delayMs(uint32_t ms) { ::delay(ms); }
  }  // namespace sim7080g
#endif

// Back-compat alias used across this repo.
using SIM7080G_String = sim7080g::String;

namespace sim7080g {
  enum class Status : uint8_t {
    Ok = 0,
    Error,
    Timeout,
    TransportError,
  };

  struct AtResponse {
    Status status = Status::Timeout;
    String raw{};
  };

  struct SignalQuality {
    int rssi = -1;
    int ber = -1;
    bool valid = false;
  };

  struct GNSSFix {
    bool valid = false;
    double latitude = 0.0;
    double longitude = 0.0;
    double altitude_m = 0.0;
    double speed_kph = 0.0;
    double course_deg = 0.0;
    uint8_t satellites = 0;
    float hdop = 0.0f;
    float pdop = 0.0f;
    float vdop = 0.0f;
    // Raw timestamp as reported (module formats vary: yyMMddhhmmss.sss, etc.)
    String utc{};
  };

  struct HttpResponse {
    Status status = Status::Timeout;
    int http_status = -1;
    String body{};
  };
}  // namespace sim7080g
