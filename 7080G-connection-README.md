# StamPLC-7080G (T-Mobile LTE-M)

This PlatformIO project contains the firmware that boots a StamPLC unit, powers the integrated SIM7080G modem, and attaches it to Soracom's plan-US-max subscription over T-Mobile LTE-M (Cat-M1). All of the work we did in this repo—the watchdog handling, deterministic AT command ordering, and improved diagnostics—has been consolidated into this build so it can be re-used as the connectivity layer for the broader StamPLC project.

## Repository Highlights

- `src/main.cpp` – Application entry point. Contains the simplified `attachTMobileCatM()` sequence and the `testConnectivity()` probe that validates the data path.
- `libraries/M5_SIM7080G` – Thin wrapper around the SIM7080G UART transport; `waitMsg()` now feeds the watchdog and terminates when OK/ERROR is seen.
- `platformio.ini` – Builds for the original StamPLC hardware profile (ESP32-S3, M5Stack board variant).

## Build & Flash

1. Install [PlatformIO](https://platformio.org/install).
2. Connect the StamPLC board to your workstation via USB-C.
3. From this directory, run `pio run -t upload` to compile and flash.
4. Open a serial monitor (`pio device monitor`) at 115200 baud to observe logs during bring-up.

## Soracom & T-Mobile Requirements

- Activate the SIM on **plan-US-max** in the Soracom console.
- Allow data service for the device group that contains the SIM (Harvest/Uni endpoints require the appropriate service checkboxes).
- Soracom uses APN `soracom.io` with PAP credentials `sora` / `sora`.

## Connection Flow Recap

1. `powerCycleSIM7080G()` toggles PWRKEY for a clean modem state.
2. `connectSoracom()` resets AT settings, enables verbose errors, and calls `attachTMobileCatM()`.
3. `attachTMobileCatM()`
   - Forces LTE-only (`AT+CNMP=38`) and Cat-M (`AT+CMNB=1`).
   - Disables PSM/eDRX to prevent delays during registration.
   - Locks the modem to T-Mobile bands (2/4/12/66).
   - Performs manual operator selection with `AT+COPS=1,2,"310260"`.
   - Waits (up to 120 s) for `+CEREG` status 1/5.
   - Activates the PDP context (`AT+CNACT=0,1`) and captures the IP address.
   - Runs `testConnectivity()` to confirm reachability of `http://httpbin.org`.

If any step fails, the firmware prints a descriptive message and re-enters a polling mode (`loop()` watches for `CEREG 0,5` and replays the activation steps).

## Working With the Code

- **Extend StamPLC features**: add application logic into `loop()` once `SORACOM_CONNECTED` is true. Use the provided status bar helpers (`drawStatusBar`) to surface telemetry.
- **Changing endpoints**: edit `testConnectivity()` or `postData()` for different HTTP targets. Adjust timeouts if the service responds slowly.
- **Adapting for other carriers**: duplicate `attachTMobileCatM()` into new helpers (e.g., `attachATT()`) and switch based on configuration. The previous multi-carrier logic has been intentionally trimmed for clarity but can be reintroduced using this helper pattern.
- **Watchdog considerations**: long-running AT interactions must feed the ESP task watchdog. Reuse `waitMsg()` for new sequences or follow its pattern.

## Troubleshooting

- **HTTP 503 / Portal HTML**: indicates Soracom has not granted outbound HTTP for the SIM. Confirm the subscription status and service flags in the console.
- **Persistent `+CEREG: 0,2/0,3`**: check Cat-M coverage or move closer to a window/antenna. Verify band locking matches local deployment (Band 12 is primary for T-Mobile in the US).
- **`AT+SHCONF` returning `ERROR`**: usually follows a 503 throttle. Power-cycle or wait a few minutes after fixing the subscription, then re-run the attach.

## Next Steps

- Merge this repo into your core StamPLC project as the connectivity module.
- Wrap the attach helper with configuration options (e.g., allow switching between private APNs per deployment).
- Add automated diagnostics (RSSI, `AT+CPSI?`, Soracom event logging) to surface network health in the StamPLC UI.

By starting from this cleaned and documented baseline, you can reuse the proven T-Mobile attach flow and extend it alongside the rest of the StamPLC features with minimal friction.


## Working Connection Reference

Below is a self-contained snippet showing the exact includes, GPIO usage, helper functions, and the end-to-end AT command flow that currently succeeds on T-Mobile (Soracom plan-US-max). You can drop this into a PlatformIO project targeting the StamPLC hardware; it assumes the SIM7080G PWRKEY is on GPIO12 and the modem UART is exposed on `Serial2` pins `(RX=4, TX=5)`.

```cpp
#include "M5StamPLC.h"
#include "M5GFX.h"
#include "M5_SIM7080G.h"
#include "esp_task_wdt.h"

M5GFX display;
M5Canvas canvas(&display);
M5_SIM7080G device;

bool SORACOM_CONNECTED = false;
String connectedCarrier = "";
int signalQuality = 0;
String readstr;

void log(String str) {
  Serial.print(str);
  canvas.print(str);
  canvas.pushSprite(0, 0);
}

void powerCycleSIM7080G() {
  const int PWRKEY_PIN = 12;
  pinMode(PWRKEY_PIN, OUTPUT);
  digitalWrite(PWRKEY_PIN, HIGH);
  delay(100);
  digitalWrite(PWRKEY_PIN, LOW);
  delay(1200);
  digitalWrite(PWRKEY_PIN, HIGH);
  delay(2000);
  digitalWrite(PWRKEY_PIN, LOW);
  delay(100);
  digitalWrite(PWRKEY_PIN, HIGH);
  delay(5000);
}

String getIpAddress(String str) {
  int start = str.indexOf("+CNACT:");
  String buf = str.substring(start + 13);
  int term = buf.indexOf("\\r");
  return buf.substring(0, term - 1);
}

bool isIpActive(String str) {
  int start = str.indexOf("+CNACT:");
  return str.substring(start + 10, start + 11) == "1";
}

int getSignalQuality() {
  device.sendMsg("AT+CSQ\\r\\n");
  String resp = device.waitMsg(1000);
  int idx = resp.indexOf("+CSQ: ");
  if (idx != -1) {
    int val = resp.substring(idx + 6, resp.indexOf(",", idx)).toInt();
    if (val != 99) return val;
  }
  return 0;
}

bool testConnectivity() {
  Serial.println("[TEST] Testing internet connectivity...");
  log(String("Testing connectivity...\\r\\n"));

  bool sessionOpened = false;
  auto closeSession = [&]() {
    if (sessionOpened) {
      device.sendMsg("AT+SHDISC\\r\\n");
      device.waitMsg(5000);
      sessionOpened = false;
    }
  };
  auto fail = [&](const char *reason) {
    closeSession();
    Serial.printf("[TEST] No internet response (%s)\\n", reason);
    log(String("No internet response\\r\\n"));
    return false;
  };
  auto sendExpectOk = [&](const String &cmd, unsigned long waitMs) -> bool {
    if (cmd.length() > 0) {
      device.sendMsg(cmd);
    }
    String resp = device.waitMsg(waitMs);
    return resp.indexOf("ERROR") == -1 && resp.indexOf("OK") != -1;
  };

  if (!sendExpectOk("AT+SHCONF=\\"URL\\",\\"http://httpbin.org\\"\\r\\n", 4000)) return fail("set URL");
  if (!sendExpectOk("AT+SHCONF=\\"BODYLEN\\",512\\r\\n", 2000)) return fail("set body len");
  if (!sendExpectOk("AT+SHCONF=\\"HEADERLEN\\",350\\r\\n", 2000)) return fail("set header len");

  device.sendMsg("AT+SHCONN\\r\\n");
  if (!sendExpectOk("", 20000)) return fail("open session");
  sessionOpened = true;

  device.sendMsg("AT+SHREQ=\\"/get\\",1\\r\\n");
  String accum = device.waitMsg(5000);
  unsigned long start = millis();
  while (accum.indexOf("+SHREQ:") == -1 && millis() - start < 20000) {
    accum += device.waitMsg(1000);
  }
  int shreqIdx = accum.indexOf("+SHREQ:");
  if (shreqIdx == -1) return fail("timeout waiting for HTTP response");

  int firstComma = accum.indexOf(",", shreqIdx);
  int secondComma = accum.indexOf(",", firstComma + 1);
  int lineEnd = accum.indexOf("\\r", secondComma);
  int statusCode = accum.substring(firstComma + 1, secondComma).toInt();
  int payloadLen = accum.substring(secondComma + 1, lineEnd).toInt();

  if (payloadLen > 0) {
    device.sendMsg(String("AT+SHREAD=0,") + payloadLen + "\\r\\n");
    device.waitMsg(5000);
  }

  closeSession();
  if (statusCode >= 200 && statusCode < 300) {
    Serial.println("[TEST] Internet connectivity confirmed");
    log(String("Internet working!\\r\\n"));
    return true;
  }
  Serial.printf("[TEST] HTTP status %d (len=%d)\\n", statusCode, payloadLen);
  log(String("HTTP status ") + String(statusCode) + String("\\r\\n"));
  return false;
}

bool attachTMobileCatM() {
  Serial.println("\\n[CONN] === T-Mobile LTE-M attach (plan-US-max) ===");
  device.sendMsg("AT+CFUN=0\\r\\n");
  device.waitMsg(5000);
  delay(1000);
  device.sendMsg("AT+COPS=2\\r\\n");
  device.waitMsg(5000);

  device.sendMsg("AT+CNMP=38\\r\\n");
  device.waitMsg(1000);
  device.sendMsg("AT+CMNB=1\\r\\n");
  device.waitMsg(2000);
  device.sendMsg("AT+CPSMS=0\\r\\n");
  device.waitMsg(500);
  device.sendMsg("AT+CEDRXS=0\\r\\n");
  device.waitMsg(500);
  device.sendMsg("AT+CBANDCFG=\\"CAT-M\\",2,4,12,66\\r\\n");
  device.waitMsg(2000);

  device.sendMsg("AT+CGDCONT=1,\\"IP\\",\\"soracom.io\\"\\r\\n");
  device.waitMsg(2000);
  device.sendMsg("AT+CGAUTH=1,1,\\"sora\\",\\"sora\\"\\r\\n");
  device.waitMsg(2000);

  device.sendMsg("AT+CFUN=1\\r\\n");
  device.waitMsg(5000);
  delay(2000);

  device.sendMsg("AT+COPS=1,2,\\"310260\\"\\r\\n");
  device.waitMsg(120000);

  bool registered = false;
  for (int i = 0; i < 24; i++) {
    device.sendMsg("AT+CEREG?\\r\\n");
    readstr = device.waitMsg(2000);
    if (readstr.indexOf(",1") != -1 || readstr.indexOf(",5") != -1) {
      registered = true;
      break;
    }
    delay(5000);
    esp_task_wdt_reset();
  }
  if (!registered) return false;

  signalQuality = getSignalQuality();
  device.sendMsg("AT+CPSI?\\r\\n");
  device.waitMsg(1000);

  device.sendMsg("AT+CGATT?\\r\\n");
  readstr = device.waitMsg(2000);
  if (readstr.indexOf("+CGATT: 0") != -1) {
    device.sendMsg("AT+CGATT=1\\r\\n");
    device.waitMsg(10000);
  }

  device.sendMsg("AT+CNACT=0,1\\r\\n");
  device.waitMsg(15000);
  device.sendMsg("AT+CNACT?\\r\\n");
  readstr = device.waitMsg(1000);
  if (!isIpActive(readstr)) return false;

  String ip = getIpAddress(readstr);
  Serial.printf("[CONN] Connected. IP: %s\\n", ip.c_str());
  log(String("Connected (T-Mobile)\\r\\n"));
  log(String("IP: ") + ip + String("\\r\\n"));
  connectedCarrier = "T-Mobile";

  return testConnectivity();
}

void connectSoracom() {
  log(String("Connecting to SORACOM...\\r\\n"));
  SORACOM_CONNECTED = false;
  connectedCarrier = "";
  signalQuality = 0;

  device.sendMsg("AT+GSN\\r\\n");
  device.waitMsg(1000);

  device.sendMsg("ATZ\\r\\n");
  device.waitMsg(5000);
  device.sendMsg("AT+CMEE=2\\r\\n");
  device.waitMsg(500);
  device.sendMsg("AT+CEREG=5\\r\\n");
  readstr = device.waitMsg(500);
  if (readstr.indexOf("ERROR") != -1) {
    device.sendMsg("AT+CEREG=2\\r\\n");
    device.waitMsg(500);
  }

  if (attachTMobileCatM()) {
    SORACOM_CONNECTED = true;
    return;
  }

  log(String("Connection failed\\r\\n"));
  log(String("Check Soracom subscription and coverage\\r\\n"));
}

void setup() {
  Serial.begin(115200);
  M5.begin();
  display.begin();
  canvas.createSprite(display.width(), display.height());
  canvas.setTextScroll(true);

  esp_task_wdt_init(60, true);
  esp_task_wdt_add(NULL);

  powerCycleSIM7080G();
  device.Init(&Serial2, 4, 5);

  connectSoracom();
}

void loop() {
  if (!SORACOM_CONNECTED) {
    device.sendMsg("AT+CEREG?\\r\\n");
    String resp = device.waitMsg(2000);
    if (resp.indexOf(",1") != -1 || resp.indexOf(",5") != -1) {
      device.sendMsg("AT+CNACT=0,1\\r\\n");
      device.waitMsg(15000);
      device.sendMsg("AT+CNACT?\\r\\n");
      String ipResp = device.waitMsg(1000);
      if (isIpActive(ipResp) && testConnectivity()) {
        connectedCarrier = "T-Mobile";
        signalQuality = getSignalQuality();
        SORACOM_CONNECTED = true;
      }
    }
    delay(5000);
  } else {
    delay(1000);
  }
}
```

### Command Sequence Summary

| Step | Purpose | AT Command(s) |
| --- | --- | --- |
| 1 | Reset modem state | `ATZ`, `AT+CMEE=2`, `AT+CEREG=5` (fallback to `=2` on ERROR) |
| 2 | Disable radio & detach | `AT+CFUN=0`, `AT+COPS=2` |
| 3 | Force LTE-M & disable low power | `AT+CNMP=38`, `AT+CMNB=1`, `AT+CPSMS=0`, `AT+CEDRXS=0` |
| 4 | Lock T-Mobile bands | `AT+CBANDCFG="CAT-M",2,4,12,66` |
| 5 | Configure Soracom APN | `AT+CGDCONT=1,"IP","soracom.io"`, `AT+CGAUTH=1,1,"sora","sora"` |
| 6 | Re-enable radio | `AT+CFUN=1` |
| 7 | Manual operator select | `AT+COPS=1,2,"310260"` |
| 8 | Registration polling | repeat `AT+CEREG?` until status `,1` or `,5` |
| 9 | Inspect network info | `AT+CPSI?` |
| 10 | Ensure packet attach | `AT+CGATT?` (optionally `AT+CGATT=1`) |
| 11 | Activate PDP context | `AT+CNACT=0,1`, verify with `AT+CNACT?` |
| 12 | Connectivity check | `AT+SHCONF=...`, `AT+SHCONN`, `AT+SHREQ="/get",1`, `AT+SHREAD` |

These are the bare-minimum steps we confirmed in the lab to obtain an IP address and produce a valid HTTP 200 response when the Soracom subscription permits internet access. Use them as the canonical recipe inside your StamPLC project.

