// Simple in-RAM rolling log buffer for recent human-readable lines
#ifndef LOG_BUFFER_H
#define LOG_BUFFER_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#ifndef LOG_BUFFER_CAPACITY
#define LOG_BUFFER_CAPACITY 100
#endif

#ifndef LOG_BUFFER_LINE_LEN
#define LOG_BUFFER_LINE_LEN 160
#endif

void log_init();
void log_add(const char* line);
// printf-style logging into the ring buffer (name avoids clash with ESP-IDF's log_printf)
void logbuf_printf(const char* fmt, ...);

// Read API
size_t log_count();
// Get line by index from oldest (0) to newest (count-1). Returns false if out of range.
bool log_get_line(size_t idx_from_oldest, char* out, size_t outsz);

#endif // LOG_BUFFER_H
