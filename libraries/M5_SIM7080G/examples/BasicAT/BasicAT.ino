// BasicAT.ino - sanity check the UART + AT handshake

#include <M5_SIM7080G.h>

static constexpr uint8_t MODEM_RX = 16;
static constexpr uint8_t MODEM_TX = 17;

M5_SIM7080G modem;

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("\nSIM7080G BasicAT");

  modem.Init(&Serial2, MODEM_RX, MODEM_TX, 115200);

  Serial.println("Waking modem...");
  const bool ok = modem.wakeup(10, 1000, 300);
  Serial.print("AT OK? ");
  Serial.println(ok ? "yes" : "no");

  auto r = modem.sendCommand("ATI", 1500, true);
  Serial.println("ATI response:");
  Serial.println(r.raw);
}

void loop() {
  // Pass-through: type AT commands in Serial Monitor, see module output.
  if (Serial.available()) {
    String line = Serial.readStringUntil('\n');
    line.trim();
    if (line.length()) {
      auto r = modem.sendCommand(line, 3000, true);
      Serial.println(r.raw);
    }
  }
  delay(10);
}

