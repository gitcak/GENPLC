/*
 * Icon Management Header
 * Vector-style icons drawn with LGFX primitives
 */

#ifndef ICON_MANAGER_H
#define ICON_MANAGER_H

#include <stdint.h>

// Icon sprite initialization (draws icons into pre-allocated sprites)
void initializeIcons();

// Sprite cleanup functions
void cleanupContentSprite();
void cleanupStatusSprite();
void cleanupIconSprites();
void cleanupAllSprites();

// ═══════════════════════════════════════════════════════════════════════════
// Direct icon drawing functions (draw directly to display, no sprites needed)
// Use these for inline icons in page headers or status displays
// ═══════════════════════════════════════════════════════════════════════════

// Draw satellite icon (GNSS) - satellite with solar panels
void drawIconSatelliteDirect(int16_t x, int16_t y, int16_t size, uint16_t color);

// Draw GPS/location pin icon
void drawIconGPSDirect(int16_t x, int16_t y, int16_t size, uint16_t color);

// Draw cellular signal bars icon
void drawIconCellularDirect(int16_t x, int16_t y, int16_t size, uint16_t color);

// Draw gear/system icon
void drawIconGearDirect(int16_t x, int16_t y, int16_t size, uint16_t color);

// Draw document/log icon
void drawIconLogDirect(int16_t x, int16_t y, int16_t size, uint16_t color);

// Draw settings icon (alias for gear)
void drawIconSettingsDirect(int16_t x, int16_t y, int16_t size, uint16_t color);

#endif // ICON_MANAGER_H
