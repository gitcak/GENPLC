// Network.ino - register + PDP + signal quality

#include <M5_SIM7080G.h>

static constexpr uint8_t MODEM_RX = 16;
static constexpr uint8_t MODEM_TX = 17;

// If your carrier requires APN, set it here.
static const char *APN = "";

M5_SIM7080G modem;
SIM7080G_Network net(modem);

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("\nSIM7080G Network example");

  modem.Init(&Serial2, MODEM_RX, MODEM_TX, 115200);
  modem.wakeup(10, 1000, 300);

  if (APN && APN[0]) {
    Serial.print("Setting APN: ");
    Serial.println(APN);
    net.setAPN(APN);
  }

  Serial.println("Waiting for network...");
  const bool regOk = net.waitForNetwork(60000);
  Serial.print("Registered? ");
  Serial.println(regOk ? "yes" : "no");

  Serial.println("Activating PDP...");
  const bool pdpOk = net.activatePDP(0);
  Serial.print("PDP active? ");
  Serial.println(pdpOk ? "yes" : "no");
}

void loop() {
  const auto csq = net.getSignalQuality();
  if (csq.valid) {
    Serial.print("CSQ rssi=");
    Serial.print(csq.rssi);
    Serial.print(" ber=");
    Serial.println(csq.ber);
  } else {
    Serial.println("CSQ: unavailable");
  }

  Serial.print("CEREG/CREG: ");
  Serial.println(net.getNetworkStatus().c_str());

  Serial.print("CNACT active? ");
  Serial.println(net.isPDPActive(0) ? "yes" : "no");

  delay(5000);
}

