#include <Arduino.h>
#include "pwrcan_module.h"

// FreeRTOS task wrapper for PWRCAN polling
// Non-intrusive: only runs if module initialized successfully

extern "C" void vTaskPWRCAN(void* pvParameters) {
    PWRCANModule* canModule = reinterpret_cast<PWRCANModule*>(pvParameters);
    if (canModule == nullptr) {
        vTaskDelete(nullptr);
        return;
    }

    Serial.println("PWRCAN task started");

    // One-shot self-test transmit (may fail if no ACK on bus; that's fine)
    {
        const uint8_t testPayload[2] = { 0xCA, 0xFE };
        bool txOk = canModule->sendFrame(0x321, testPayload, 2, false, false, 10);
        Serial.printf("PWRCAN self-test TX: %s\n", txOk ? "OK" : "FAIL (no ACK or bus issue)" );
    }

    // Simple RX loop; adjust pins/bitrate as needed when wiring is finalized
    for (;;) {
        uint32_t id = 0;
        uint8_t data[8] = {0};
        uint8_t len = 0;
        bool ext = false;
        bool rtr = false;
        if (canModule->receiveFrame(id, data, len, ext, rtr, 0)) {
            // Minimal debug print to avoid flooding
            Serial.printf("CAN RX id=0x%08lx len=%u\n", (unsigned long)id, (unsigned)len);
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}


