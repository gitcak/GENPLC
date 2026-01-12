#include <Arduino.h>
#include <M5StamPLC.h>

static void printMemoryStats()
{
    Serial.printf("Heap: free=%u min=%u\n", ESP.getFreeHeap(), ESP.getMinFreeHeap());
}

void setup()
{
    Serial.begin(115200);
    delay(500);

    M5StamPLC.begin();
    delay(200);

    Serial.println();
    Serial.println("=== GENPLC Memory Test ===");
    printMemoryStats();
}

void loop()
{
    M5StamPLC.update();

    static uint32_t lastPrintMs = 0;
    const uint32_t now = millis();
    if (now - lastPrintMs >= 1000) {
        lastPrintMs = now;
        printMemoryStats();
    }

    delay(10);
}

