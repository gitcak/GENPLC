# M5_SIM7080G (SIM7080G) ESP32 Library

AT-command helper library for the **M5Stack Unit CatM GNSS (SIM7080G)** on ESP32.

- **Targets**: ESP32 (Arduino framework, PlatformIO, ESP-IDF component)
- **Features**: robust AT send/wait, **Network/PDP helpers**, **GNSS fix parsing**, **MQTT**, **HTTP (AT+SH*)**

## Quick start (Arduino / PlatformIO Arduino)

1) Wire UART (3.3V TTL) and power the module properly.

2) Include the library and talk to the modem:

```cpp
#include <M5_SIM7080G.h>

M5_SIM7080G modem;
SIM7080G_Network net(modem);

void setup() {
  Serial.begin(115200);
  modem.Init(&Serial2, 16, 17, 115200);

  if (!modem.wakeup(10, 1000, 300)) {
    Serial.println("No AT response");
  }

  net.waitForNetwork(60000);
  net.activatePDP(0);
}

void loop() {}
```

See `examples/BasicAT`, `examples/Network`, `examples/GNSS`, `examples/MQTT`, `examples/HTTP`.

## ESP-IDF (as a component)

This folder is also an ESP-IDF component (includes `idf_component.yml`, `Kconfig`, `CMakeLists.txt`).

### Option A: Copy into your project

- Copy this folder into your ESP-IDF project under `components/M5_SIM7080G/`
- `idf.py menuconfig` → `M5_SIM7080G (SIM7080G)` for default UART pin/baud values (optional)

### Option B: Use component manager

If you’re using ESP-IDF Component Manager, add this repo as a dependency (path/git URL as appropriate) and include:

```cpp
#include "M5_SIM7080G.h"
```

Then initialize with the ESP-IDF UART backend:

```cpp
M5_SIM7080G modem;
modem.Init(UART_NUM_1, /*rxPin=*/16, /*txPin=*/17, /*baud=*/115200);
modem.wakeup(10, 1000, 300);
```

## API notes

### Core modem (`M5_SIM7080G`)

- `Init(...)`: Arduino `HardwareSerial` backend or ESP-IDF `uart_port_t` backend
- `sendCommand(cmd, timeout_ms)`: sends AT, waits for `OK`/`ERROR` (timeout-safe)
- `wakeup(attempts, timeout_ms, delay_ms)`: retries `AT` until it answers
- Legacy helpers remain (`sendMsg`, `waitMsg`, `send_and_getMsg`) for quick scripts

### Network (`SIM7080G_Network`)

- `setAPN(apn, user, pass, cid)`
- `waitForNetwork(timeout_ms)` checks `AT+CEREG?`/`AT+CREG?`
- `activatePDP(profileId)` uses `AT+CNACT=<profileId>,1` (examples use `profileId=0`)
- `getSignalQuality()` parses `AT+CSQ`

### GNSS (`SIM7080G_GNSS`)

- `powerOn()` / `powerOff()` → `AT+CGNSPWR`
- `getFix()` parses `AT+CGNSINF` into `sim7080g::GNSSFix`
- NMEA streaming helpers: `startNMEA()` / `stopNMEA()` (best-effort: `AT+CGNSTST`)

### MQTT (`SIM7080G_MQTT`)

- Wraps common `AT+SMCONF`, `AT+SMCONN`, `AT+SMSUB`, `AT+SMPUB`
- `poll()` parses `+SMSUB:` URCs (best-effort across firmware variants)

### HTTP (`SIM7080G_HTTP`)

Implements the flow described in SIMCom’s HTTP application note:

- `AT+SHCONF="URL",...`, `AT+SHCONN`
- `AT+SHAHEAD` headers
- `AT+SHREQ` + `AT+SHREAD`
- POST body via `AT+SHBOD` then `AT+SHREQ`

## Gotchas (SIM7080G reality)

- **Power matters**: cellular bursts can brown out weak USB supplies.
- **APN may be required**: set `APN` in examples if your SIM/carrier needs it.
- **Firmware variance**: some commands/URCs differ slightly; helpers are designed to be tolerant, not magical.

## License

See [LICENSE](LICENSE).

