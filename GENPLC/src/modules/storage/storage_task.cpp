#include "storage_task.h"
#include "sd_card_module.h"
#include <SD.h>

QueueHandle_t g_storageQ = nullptr;
SemaphoreHandle_t g_sdMutex = nullptr;

extern SDCardModule* sdModule;

static bool sd_rotate_if_big(const char* path, size_t maxBytes) {
    if (!sdModule || !sdModule->isMounted()) return false;
    File f = SD.open(path, FILE_READ);
    if (!f) return true;  // nothing to rotate yet
    size_t sz = f.size();
    f.close();
    if (sz <= maxBytes) return true;
    String bak = String(path) + ".1";
    SD.remove(bak.c_str());
    SD.rename(path, bak.c_str());
    File nf = SD.open(path, FILE_WRITE, true);
    if (nf) nf.close();
    return true;
}

static void sd_append_jsonl(const char* path, const char* jsonLine) {
    if (!sdModule || !sdModule->isMounted()) return;
    xSemaphoreTake(g_sdMutex, portMAX_DELAY);
    SD.mkdir("/data");
    sd_rotate_if_big(path, 512 * 1024);
    File f = SD.open(path, FILE_APPEND, true);
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
            if (rec.type == LogRecord::GNSS) sd_append_jsonl("/data/gnss.jsonl", rec.line);
            else sd_append_jsonl("/data/cellular.jsonl", rec.line);
        }
    }
}


