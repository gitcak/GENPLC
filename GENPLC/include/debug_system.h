/*
 * Debug System Header
 * StampPLC CatM+GNSS FreeRTOS Modularization
 * 
 * Comprehensive debugging for CATM+GNSS modules
 * UART I/O monitoring, statistics, and display integration
 * 
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2025 M5Stack Technology CO LTD
 */

#ifndef DEBUG_SYSTEM_H
#define DEBUG_SYSTEM_H

#include <Arduino.h>
#include "config/system_config.h"

// Define log levels
typedef enum {
    DEBUG_LOG_LEVEL_NONE,
    DEBUG_LOG_LEVEL_ERROR,
    DEBUG_LOG_LEVEL_WARN,
    DEBUG_LOG_LEVEL_INFO,
    DEBUG_LOG_LEVEL_DEBUG,
    DEBUG_LOG_LEVEL_VERBOSE
} LogLevel;

// Function to set the current log level
void set_log_level(LogLevel level);

// Function to get the current log level
LogLevel get_log_level();

// Main logging macro
#if ENABLE_DEBUG_SYSTEM
    #define LOG_MACRO(level, fmt, ...) \
        do { \
            if (get_log_level() >= level) { \
                char buf[256]; \
                snprintf(buf, sizeof(buf), "[%s] " fmt, __func__, ##__VA_ARGS__); \
                Serial.println(buf); \
            } \
        } while(0)
#else
    #define LOG_MACRO(level, fmt, ...) do {} while(0)
#endif

// Specific-level log macros
#define LOG_ERROR(fmt, ...)   LOG_MACRO(DEBUG_LOG_LEVEL_ERROR,   "ERROR: " fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)    LOG_MACRO(DEBUG_LOG_LEVEL_WARN,    "WARN: " fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...)    LOG_MACRO(DEBUG_LOG_LEVEL_INFO,    "INFO: " fmt, ##__VA_ARGS__)
#define LOG_DEBUG(fmt, ...)   LOG_MACRO(DEBUG_LOG_LEVEL_DEBUG,   "DEBUG: " fmt, ##__VA_ARGS__)
#define LOG_VERBOSE(fmt, ...) LOG_MACRO(DEBUG_LOG_LEVEL_VERBOSE, "VERBOSE: " fmt, ##__VA_ARGS__)


// Legacy debug macros - map to new system
#define DEBUG_LOG_TASK_START(name) LOG_INFO("Task %s starting...", name)
#define DEBUG_LOG_HEAP(label)      LOG_DEBUG("Heap at %s: %u bytes", label, ESP.getFreeHeap())
#define DEBUG_LOG_BUTTON_PRESS(btn, action) LOG_DEBUG("Button %s pressed: %s", btn, action)

#endif // DEBUG_SYSTEM_H
