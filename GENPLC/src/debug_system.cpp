#include "../include/debug_system.h"

// Default log level
static LogLevel current_log_level = DEBUG_LOG_LEVEL_INFO;

void set_log_level(LogLevel level) {
    current_log_level = level;
}

LogLevel get_log_level() {
    return current_log_level;
}