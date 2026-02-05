/*
 * Icon Management Implementation
 * Vector-style icons drawn with LGFX primitives
 */

#include "icon_manager.h"
#include <M5StamPLC.h>
#include <M5GFX.h>
#include "../theme.h"

// Forward declarations - sprites are defined in main.cpp
extern lgfx::LGFX_Sprite iconSatellite;
extern lgfx::LGFX_Sprite iconGear;
extern lgfx::LGFX_Sprite iconTower;
extern lgfx::LGFX_Sprite iconWrench;
extern lgfx::LGFX_Sprite iconGPS;
extern lgfx::LGFX_Sprite iconCellular;
extern lgfx::LGFX_Sprite iconLog;
extern lgfx::LGFX_Sprite iconSatelliteBig;
extern lgfx::LGFX_Sprite iconGearBig;
extern lgfx::LGFX_Sprite iconTowerBig;
extern lgfx::LGFX_Sprite iconWrenchBig;
extern lgfx::LGFX_Sprite iconGPSBig;
extern lgfx::LGFX_Sprite iconCellularBig;
extern lgfx::LGFX_Sprite iconLogBig;

// External state
extern bool iconsInitialized;
extern lgfx::LGFX_Sprite contentSprite;
extern bool contentSpriteInit;
extern lgfx::LGFX_Sprite statusSprite;
extern bool statusSpriteInit;

// ═══════════════════════════════════════════════════════════════════════════
// ICON DRAWING FUNCTIONS - Primitive-based vector icons
// ═══════════════════════════════════════════════════════════════════════════

// Satellite icon - orbiting satellite with solar panels
static void drawIconSatellite(lgfx::LGFX_Sprite& s, int size, uint16_t color) {
    s.fillSprite(0x0000);  // Transparent black
    int cx = size / 2;
    int cy = size / 2;
    int unit = size / 8;

    // Main body (rectangle)
    s.fillRect(cx - unit, cy - unit, unit * 2, unit * 2, color);

    // Solar panels (rectangles on sides)
    s.fillRect(cx - unit * 4, cy - unit / 2, unit * 2, unit, color);
    s.fillRect(cx + unit * 2, cy - unit / 2, unit * 2, unit, color);

    // Panel lines
    s.drawFastVLine(cx - unit * 3, cy - unit / 2, unit, 0x0000);
    s.drawFastVLine(cx + unit * 3, cy - unit / 2, unit, 0x0000);

    // Antenna
    s.drawLine(cx, cy - unit, cx, cy - unit * 2, color);
    s.fillCircle(cx, cy - unit * 2 - 1, 1, color);
}

// GPS/Location icon - pin marker
static void drawIconGPS(lgfx::LGFX_Sprite& s, int size, uint16_t color, uint16_t bgColor) {
    s.fillSprite(0x0000);
    int cx = size / 2;
    int unit = size / 8;

    // Pin body (circle at top)
    s.fillCircle(cx, unit * 3, unit * 2, color);
    s.fillCircle(cx, unit * 3, unit, bgColor);  // Inner hole

    // Pin point (triangle)
    s.fillTriangle(cx - unit * 2, unit * 4, cx + unit * 2, unit * 4, cx, size - unit, color);
}

// Cellular icon - signal bars
static void drawIconCellular(lgfx::LGFX_Sprite& s, int size, uint16_t color) {
    s.fillSprite(0x0000);
    int unit = size / 8;

    // Signal bars (ascending)
    for (int i = 0; i < 4; i++) {
        int barH = unit * (i + 1) + unit;
        int barX = unit + i * (unit + 2);
        int barY = size - unit - barH;
        s.fillRect(barX, barY, unit, barH, color);
    }
}

// System/Gear icon - cogwheel
static void drawIconGear(lgfx::LGFX_Sprite& s, int size, uint16_t color, uint16_t bgColor) {
    s.fillSprite(0x0000);
    int cx = size / 2;
    int cy = size / 2;
    int r = size / 3;

    // Outer gear circle
    s.fillCircle(cx, cy, r, color);
    s.fillCircle(cx, cy, r / 2, bgColor);

    // Gear teeth (6 teeth)
    for (int i = 0; i < 6; i++) {
        float angle = i * 3.14159f / 3.0f;
        int tx = cx + (int)((r + 2) * cos(angle));
        int ty = cy + (int)((r + 2) * sin(angle));
        s.fillCircle(tx, ty, size / 8, color);
    }
}

// Settings/Wrench icon - adjustable wrench
static void drawIconWrench(lgfx::LGFX_Sprite& s, int size, uint16_t color, uint16_t bgColor) {
    s.fillSprite(0x0000);
    int unit = size / 8;

    // Handle (diagonal)
    for (int i = 0; i < unit * 2; i++) {
        s.drawLine(unit + i, size - unit * 2, size - unit * 3 + i, unit * 3, color);
    }

    // Head (open end wrench)
    s.fillCircle(size - unit * 2, unit * 2, unit * 2, color);
    s.fillCircle(size - unit * 2, unit * 2, unit, bgColor);
    // Notch cutout
    s.fillRect(size - unit * 2 - 1, 0, unit * 2 + 2, unit + 1, 0x0000);
}

// Log/Document icon - lines of text
static void drawIconLog(lgfx::LGFX_Sprite& s, int size, uint16_t color) {
    s.fillSprite(0x0000);
    int unit = size / 8;

    // Document outline
    s.drawRect(unit, unit, size - unit * 2, size - unit * 2, color);

    // Corner fold
    s.fillTriangle(size - unit * 3, unit, size - unit, unit, size - unit, unit * 3, color);
    s.drawLine(size - unit * 3, unit, size - unit * 3, unit * 3, color);
    s.drawLine(size - unit * 3, unit * 3, size - unit, unit * 3, color);

    // Text lines
    int lineSpacing = (size - unit * 5) / 4;
    for (int i = 0; i < 4; i++) {
        int lineW = (i == 2) ? size - unit * 5 : size - unit * 4;
        s.drawFastHLine(unit * 2, unit * 3 + i * lineSpacing, lineW, color);
    }
}

// Tower icon - transmission tower
static void drawIconTower(lgfx::LGFX_Sprite& s, int size, uint16_t color) {
    s.fillSprite(0x0000);
    int cx = size / 2;
    int unit = size / 8;

    // Main tower structure (tapered)
    s.fillTriangle(cx - unit * 2, size - unit, cx + unit * 2, size - unit, cx, unit * 2, color);

    // Cross beams
    s.drawFastHLine(cx - unit, unit * 3, unit * 2, color);
    s.drawFastHLine(cx - unit - unit / 2, unit * 5, unit * 3, color);

    // Antenna at top
    s.fillRect(cx - 1, 0, 2, unit * 2, color);
}

// ═══════════════════════════════════════════════════════════════════════════
// DIRECT ICON DRAWING (for pages that don't use sprites)
// ═══════════════════════════════════════════════════════════════════════════

void drawIconSatelliteDirect(int16_t x, int16_t y, int16_t size, uint16_t color) {
    auto& d = M5StamPLC.Display;
    int cx = x + size / 2;
    int cy = y + size / 2;
    int unit = size / 8;

    // Main body
    d.fillRect(cx - unit, cy - unit, unit * 2, unit * 2, color);
    // Solar panels
    d.fillRect(cx - unit * 4, cy - unit / 2, unit * 2, unit, color);
    d.fillRect(cx + unit * 2, cy - unit / 2, unit * 2, unit, color);
    // Antenna
    d.drawLine(cx, cy - unit, cx, cy - unit * 2, color);
    d.fillCircle(cx, cy - unit * 2 - 1, 1, color);
}

void drawIconGPSDirect(int16_t x, int16_t y, int16_t size, uint16_t color) {
    auto& d = M5StamPLC.Display;
    int cx = x + size / 2;
    int unit = size / 8;

    // Pin body
    d.fillCircle(cx, y + unit * 3, unit * 2, color);
    d.fillCircle(cx, y + unit * 3, unit, ui::theme().bg);
    // Pin point
    d.fillTriangle(cx - unit * 2, y + unit * 4, cx + unit * 2, y + unit * 4, cx, y + size - unit, color);
}

void drawIconCellularDirect(int16_t x, int16_t y, int16_t size, uint16_t color) {
    auto& d = M5StamPLC.Display;
    int unit = size / 8;

    // Signal bars (ascending)
    for (int i = 0; i < 4; i++) {
        int barH = unit * (i + 1) + unit;
        int barX = x + unit + i * (unit + 2);
        int barY = y + size - unit - barH;
        d.fillRect(barX, barY, unit, barH, color);
    }
}

void drawIconGearDirect(int16_t x, int16_t y, int16_t size, uint16_t color) {
    auto& d = M5StamPLC.Display;
    int cx = x + size / 2;
    int cy = y + size / 2;
    int r = size / 3;

    // Outer gear circle
    d.fillCircle(cx, cy, r, color);
    d.fillCircle(cx, cy, r / 2, ui::theme().bg);

    // Gear teeth
    for (int i = 0; i < 6; i++) {
        float angle = i * 3.14159f / 3.0f;
        int tx = cx + (int)((r + 2) * cos(angle));
        int ty = cy + (int)((r + 2) * sin(angle));
        d.fillCircle(tx, ty, 2, color);
    }
}

void drawIconLogDirect(int16_t x, int16_t y, int16_t size, uint16_t color) {
    auto& d = M5StamPLC.Display;
    int unit = size / 8;

    // Document outline
    d.drawRect(x + unit, y + unit, size - unit * 2, size - unit * 2, color);

    // Text lines
    for (int i = 0; i < 3; i++) {
        d.drawFastHLine(x + unit * 2, y + unit * 3 + i * unit * 2, size - unit * 5, color);
    }
}

void drawIconSettingsDirect(int16_t x, int16_t y, int16_t size, uint16_t color) {
    drawIconGearDirect(x, y, size, color);
}

// ═══════════════════════════════════════════════════════════════════════════
// INITIALIZATION
// ═══════════════════════════════════════════════════════════════════════════

void initializeIcons() {
    if (iconsInitialized) return;

    const auto& th = ui::theme();
    uint16_t iconColor = th.accent;
    uint16_t bgColor = 0x0000;  // Transparent for sprites

    // Small icons (16x16) for status bars
    iconSatellite.createSprite(16, 16);
    iconGear.createSprite(16, 16);
    iconTower.createSprite(16, 16);
    iconWrench.createSprite(16, 16);
    iconGPS.createSprite(16, 16);
    iconCellular.createSprite(16, 16);
    iconLog.createSprite(16, 16);

    drawIconSatellite(iconSatellite, 16, iconColor);
    drawIconGear(iconGear, 16, iconColor, bgColor);
    drawIconTower(iconTower, 16, iconColor);
    drawIconWrench(iconWrench, 16, iconColor, bgColor);
    drawIconGPS(iconGPS, 16, iconColor, bgColor);
    drawIconCellular(iconCellular, 16, iconColor);
    drawIconLog(iconLog, 16, iconColor);

    // Large icons (32x32) for launcher cards
    iconSatelliteBig.createSprite(32, 32);
    iconGearBig.createSprite(32, 32);
    iconTowerBig.createSprite(32, 32);
    iconWrenchBig.createSprite(32, 32);
    iconGPSBig.createSprite(32, 32);
    iconCellularBig.createSprite(32, 32);
    iconLogBig.createSprite(32, 32);

    drawIconSatellite(iconSatelliteBig, 32, iconColor);
    drawIconGear(iconGearBig, 32, iconColor, bgColor);
    drawIconTower(iconTowerBig, 32, iconColor);
    drawIconWrench(iconWrenchBig, 32, iconColor, bgColor);
    drawIconGPS(iconGPSBig, 32, iconColor, bgColor);
    drawIconCellular(iconCellularBig, 32, iconColor);
    drawIconLog(iconLogBig, 32, iconColor);

    iconsInitialized = true;
}

void cleanupContentSprite() {
    if (contentSpriteInit) {
        contentSprite.deleteSprite();
        contentSpriteInit = false;
    }
}

void cleanupStatusSprite() {
    if (statusSpriteInit) {
        statusSprite.deleteSprite();
        statusSpriteInit = false;
    }
}

void cleanupIconSprites() {
    if (iconsInitialized) {
        iconSatellite.deleteSprite();
        iconGear.deleteSprite();
        iconTower.deleteSprite();
        iconWrench.deleteSprite();
        iconGPS.deleteSprite();
        iconCellular.deleteSprite();
        iconLog.deleteSprite();
        iconSatelliteBig.deleteSprite();
        iconGearBig.deleteSprite();
        iconTowerBig.deleteSprite();
        iconWrenchBig.deleteSprite();
        iconGPSBig.deleteSprite();
        iconCellularBig.deleteSprite();
        iconLogBig.deleteSprite();
        iconsInitialized = false;
    }
}

void cleanupAllSprites() {
    cleanupContentSprite();
    cleanupStatusSprite();
    cleanupIconSprites();
}
