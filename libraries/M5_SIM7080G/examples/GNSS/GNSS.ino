// GNSS.ino - minimal GNSS fix polling example (no M5Stack deps)
//
// Wiring (M5Stack Unit CatM GNSS / SIM7080G):
// - RX/TX defaults below match many ESP32 examples: RX=16, TX=17 (ESP32 GPIOs)
// - Baud: 115200 8N1

#include <M5_SIM7080G.h>

static constexpr uint8_t MODEM_RX = 16; // ESP32 RX  <- SIM7080G TX
static constexpr uint8_t MODEM_TX = 17; // ESP32 TX  -> SIM7080G RX

M5_SIM7080G modem;
SIM7080G_GNSS gnss(modem);

static void printFix(const sim7080g::GNSSFix &fix) {
  if (!fix.valid) {
    Serial.println("GNSS: no fix yet");
    return;
  }
  Serial.print("GNSS: lat=");
  Serial.print(fix.latitude, 6);
  Serial.print(" lon=");
  Serial.print(fix.longitude, 6);
  Serial.print(" alt_m=");
  Serial.print(fix.altitude_m, 1);
  Serial.print(" spd_kph=");
  Serial.print(fix.speed_kph, 1);
  Serial.print(" utc=");
  Serial.println(fix.utc);
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("\nSIM7080G GNSS example");

  modem.Init(&Serial2, MODEM_RX, MODEM_TX, 115200);
  if (!modem.wakeup(10, 1000, 300)) {
    Serial.println("Modem not responding to AT");
  }

  // Optional reboot (comment out if you don't want it)
  modem.sendMsg("AT+CREBOOT\r\n");
  delay(5000);
  modem.wakeup(10, 1000, 300);

  Serial.println("Turning GNSS on...");
  if (!gnss.powerOn(5000)) {
    Serial.println("Failed to enable GNSS (AT+CGNSPWR=1)");
  }
}

void loop() {
  const auto fix = gnss.getFix(2000);
  printFix(fix);
  delay(1000);
}
