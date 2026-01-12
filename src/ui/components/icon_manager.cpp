/*
 * Icon Management Implementation
 * Icon sprite initialization and cleanup
 */

#include "icon_manager.h"
#include <M5StamPLC.h>
#include <M5GFX.h>

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

void initializeIcons() {
    if (iconsInitialized) return;
    
    // Small icons (16x16) for pages
    iconSatellite.createSprite(16, 16);
    iconGear.createSprite(16, 16);
    iconTower.createSprite(16, 16);
    iconWrench.createSprite(16, 16);
    iconGPS.createSprite(16, 16);
    iconCellular.createSprite(16, 16);
    iconLog.createSprite(16, 16);
    
    // Large icons (32x32) for launcher
    iconSatelliteBig.createSprite(32, 32);
    iconGearBig.createSprite(32, 32);
    iconTowerBig.createSprite(32, 32);
    iconWrenchBig.createSprite(32, 32);
    iconGPSBig.createSprite(32, 32);
    iconCellularBig.createSprite(32, 32);
    iconLogBig.createSprite(32, 32);
    
    // Draw simple placeholder icons (can be replaced with actual artwork)
    // For now, just fill with a color to indicate they exist
    iconSatellite.fillSprite(0x4A4A4A);
    iconGear.fillSprite(0x4A4A4A);
    iconTower.fillSprite(0x4A4A4A);
    iconWrench.fillSprite(0x4A4A4A);
    iconGPS.fillSprite(0x4A4A4A);
    iconCellular.fillSprite(0x4A4A4A);
    iconLog.fillSprite(0x4A4A4A);
    
    iconSatelliteBig.fillSprite(0x4A4A4A);
    iconGearBig.fillSprite(0x4A4A4A);
    iconTowerBig.fillSprite(0x4A4A4A);
    iconWrenchBig.fillSprite(0x4A4A4A);
    iconGPSBig.fillSprite(0x4A4A4A);
    iconCellularBig.fillSprite(0x4A4A4A);
    iconLogBig.fillSprite(0x4A4A4A);
    
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
