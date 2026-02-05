/*
 * UI Widgets
 * Reusable UI drawing functions (signal bars, SD info, etc.)
 */

#ifndef UI_WIDGETS_H
#define UI_WIDGETS_H

#include <M5StamPLC.h>  // Includes M5GFX automatically
#include <M5GFX.h>      // For lgfx::LGFX_Sprite
#include <stdint.h>

// Signal strength bar (full display)
void drawSignalBar(int16_t x, int16_t y, int16_t w, int16_t h, int8_t signalDbm);

// WiFi status bar
void drawWiFiBar(int16_t x, int16_t y, int16_t w, int16_t h);

// SD card info display
void drawSDInfo(int x, int y);

// Button indicators
void drawButtonIndicators();

#endif // UI_WIDGETS_H

