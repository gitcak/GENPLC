// MQTT.ino - minimal MQTT example (no M5Stack deps)
//
// NOTE: You still need your SIM/APN provisioned for data.

#include <M5_SIM7080G.h>

static constexpr uint8_t MODEM_RX = 16;
static constexpr uint8_t MODEM_TX = 17;

// If your carrier requires APN, set it here. Leave empty to skip AT+CGDCONT.
static const char *APN = "";

M5_SIM7080G modem;
SIM7080G_Network net(modem);
SIM7080G_MQTT mqtt(modem);

static void onMsg(const SIM7080G_String &topic, const SIM7080G_String &payload) {
  Serial.print("MQTT RX topic=");
  Serial.print(topic.c_str());
  Serial.print(" payload=");
  Serial.println(payload.c_str());
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("\nSIM7080G MQTT example");

  modem.Init(&Serial2, MODEM_RX, MODEM_TX, 115200);
  if (!modem.wakeup(10, 1000, 300)) {
    Serial.println("Modem not responding to AT");
  }

  if (APN && APN[0]) {
    Serial.print("Setting APN: ");
    Serial.println(APN);
    net.setAPN(APN);
  }

  Serial.println("Waiting for network registration...");
  net.waitForNetwork(60000);

  Serial.println("Activating PDP...");
  net.activatePDP(0);

  const auto csq = net.getSignalQuality();
  if (csq.valid) {
    Serial.print("CSQ rssi=");
    Serial.print(csq.rssi);
    Serial.print(" ber=");
    Serial.println(csq.ber);
  }

  mqtt.setCallback(onMsg);
  mqtt.configure("broker.emqx.io", 1883, "sim7080g_esp32");
  if (!mqtt.connect(15000)) {
    Serial.println("MQTT connect failed");
  }

  mqtt.subscribe("sub_topic", 1);
  mqtt.publish("pub_topic", "hello", 1, false);
}

void loop() {
  mqtt.poll(0);

  // Every ~10s publish a heartbeat
  static uint32_t last = 0;
  if (millis() - last > 10000) {
    last = millis();
    mqtt.publish("pub_topic", "ping", 1, false);
  }

  delay(10);
}
