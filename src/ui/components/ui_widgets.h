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

// Compact signal bar (for sprite)
void drawCompactSignalBar(void* sprite, int16_t x, int16_t y, int8_t signal, uint16_t color);

// WiFi status bar
void drawWiFiBar(int16_t x, int16_t y, int16_t w, int16_t h);

// SD card info display
void drawSDInfo(int x, int y);

// Button indicators
void drawButtonIndicators();

// Wrapped text drawing
void drawWrappedText(const String& text, int16_t x, int16_t y, int16_t maxWidth, int16_t lineHeight);

// Card box drawing (for sprite-based UI)
void drawCardBox(lgfx::LGFX_Sprite* s, int x, int y, int w, int h, const char* title, lgfx::LGFX_Sprite* icon);

#endif // UI_WIDGETS_H

