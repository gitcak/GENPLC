#include "log_buffer.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

static char s_lines[LOG_BUFFER_CAPACITY][LOG_BUFFER_LINE_LEN];
static size_t s_head = 0;   // next write position
static size_t s_count = 0;  // number of valid lines
static SemaphoreHandle_t s_mutex = nullptr;

void log_init() {
    if (!s_mutex) {
        s_mutex = xSemaphoreCreateMutex();
    }
}

static void log_add_unlocked(const char* line) {
    size_t i = s_head % LOG_BUFFER_CAPACITY;
    // Timestamp prefix [ms]
    int n = snprintf(s_lines[i], LOG_BUFFER_LINE_LEN, "[%lu] %s", (unsigned long)millis(), line ? line : "");
    if (n < 0) {
        s_lines[i][0] = '\0';
    } else {
        s_lines[i][LOG_BUFFER_LINE_LEN - 1] = '\0';
    }
    s_head = (s_head + 1) % LOG_BUFFER_CAPACITY;
    if (s_count < LOG_BUFFER_CAPACITY) s_count++;

}

void log_add(const char* line) {
    if (!s_mutex) log_init();
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        log_add_unlocked(line);
        xSemaphoreGive(s_mutex);
    }
}

void logbuf_printf(const char* fmt, ...) {
    if (!fmt) return;
    if (!s_mutex) log_init();
    char buf[LOG_BUFFER_LINE_LEN];
    va_list ap; va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        log_add_unlocked(buf);
        xSemaphoreGive(s_mutex);
    }
}

size_t log_count() {
    return s_count;
}

bool log_get_line(size_t idx_from_oldest, char* out, size_t outsz) {
    if (!out || outsz == 0) return false;
    out[0] = '\0';
    if (s_count == 0) return false;
    if (idx_from_oldest >= s_count) return false;
    if (!s_mutex) log_init();
    bool ok = false;
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        // Oldest index in buffer
        size_t oldest = (s_head + LOG_BUFFER_CAPACITY - s_count) % LOG_BUFFER_CAPACITY;
        size_t pos = (oldest + idx_from_oldest) % LOG_BUFFER_CAPACITY;
        strncpy(out, s_lines[pos], outsz - 1);
        out[outsz - 1] = '\0';
        ok = true;
        xSemaphoreGive(s_mutex);
    }
    return ok;
}
