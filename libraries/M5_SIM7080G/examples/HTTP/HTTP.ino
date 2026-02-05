// HTTP.ino - simple HTTP GET via AT+SH* commands

#include <M5_SIM7080G.h>

static constexpr uint8_t MODEM_RX = 16;
static constexpr uint8_t MODEM_TX = 17;

// If your carrier requires APN, set it here.
static const char *APN = "";

M5_SIM7080G modem;
SIM7080G_Network net(modem);
SIM7080G_HTTP http(modem);

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("\nSIM7080G HTTP example");

  modem.Init(&Serial2, MODEM_RX, MODEM_TX, 115200);
  modem.wakeup(10, 1000, 300);

  if (APN && APN[0]) net.setAPN(APN);
  net.waitForNetwork(60000);
  net.activatePDP(0);

  // Base URL (host only) per SIMCom HTTP flow.
  http.configure("http://httpbin.org", 1024, 350);
  http.connect();

  const auto resp = http.get("/get?user=jack&password=123", 30000);
  Serial.print("HTTP status: ");
  Serial.println(resp.http_status);
  Serial.println("Body:");
  Serial.println(resp.body.c_str());

  http.disconnect();
}

void loop() {
  delay(1000);
}

