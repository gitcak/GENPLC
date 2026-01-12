#include <Arduino.h>

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("Recovery firmware - StampPLC CatM+GNSS");
  Serial.println("System recovered successfully!");
  
  // Basic initialization
  Serial.println("Initializing...");
  
  // Set LED to green to indicate recovery
  pinMode(2, OUTPUT);
  digitalWrite(2, HIGH);
  
  Serial.println("Recovery complete. System ready.");
}

void loop() {
  // Blink LED to show system is alive
  digitalWrite(2, !digitalRead(2));
  delay(1000);
  
  // Print status every 10 seconds
  static unsigned long lastPrint = 0;
  if (millis() - lastPrint > 10000) {
    Serial.printf("Uptime: %lu seconds\n", millis() / 1000);
    lastPrint = millis();
  }
}
