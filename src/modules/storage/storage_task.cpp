#include "storage_task.h"
#include "sd_card_module.h"
#include <SD.h>

QueueHandle_t g_storageQ = nullptr;
SemaphoreHandle_t g_sdMutex = nullptr;

extern SDCardModule* sdModule;

static bool sd_rotate_if_big(const char* path, size_t maxBytes) {
    File f = SD.open(path, "r");
    if (!f) return true;  // nothing to rotate yet
    size_t sz = f.size();
    f.close();
    if (sz <= maxBytes) return true;
    char bak[128];
    snprintf(bak, sizeof(bak), "%s.1", path);
    SD.remove(bak);
    SD.rename(path, bak);
    File nf = SD.open(path, "w");
    if (nf) nf.close();
    return true;
}

static void sd_append_jsonl(const char* path, const char* jsonLine) {
    // Check if SD card is available before attempting operations
    if (SD.cardType() == CARD_NONE) {
        return; // SD card not present, skip silently
    }
    
    xSemaphoreTake(g_sdMutex, portMAX_DELAY);
    SD.mkdir("/data");
    sd_rotate_if_big(path, 512 * 1024);
    File f = SD.open(path, "a");
    if (f) {
        f.println(jsonLine);
        f.close();
    }
    xSemaphoreGive(g_sdMutex);
}

extern "C" void vTaskStorage(void* pvParameters) {
    (void)pvParameters;
    g_sdMutex = xSemaphoreCreateMutex();
    g_storageQ = xQueueCreate(64, sizeof(LogRecord));
    for (;;) {
        LogRecord rec;
        if (xQueueReceive(g_storageQ, &rec, portMAX_DELAY) == pdTRUE) {
            if (rec.type == LogRecord::GNSS) {
                sd_append_jsonl("/data/gnss.jsonl", rec.line);
            } else if (rec.type == LogRecord::CELL) {
                sd_append_jsonl("/data/cellular.jsonl", rec.line);
            } else if (rec.type == LogRecord::SYSTEM) {
                sd_append_jsonl("/data/system.jsonl", rec.line);
            }
        }
    }
}


