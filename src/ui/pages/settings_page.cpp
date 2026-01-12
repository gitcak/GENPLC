/*
 * Settings Page Implementation
 * System settings and configuration
 */

#include "settings_page.h"
#include "../ui_constants.h"
#include "../../modules/storage/sd_card_module.h"
#include "../components/ui_widgets.h"

// External globals
extern SDCardModule* sdModule;
extern volatile int16_t scrollSETTINGS;

void drawSettingsPage() {
    int16_t yOffset = scrollSETTINGS;
    
    // Title
    M5StamPLC.Display.setTextColor(WHITE);
    M5StamPLC.Display.setTextSize(3);
    M5StamPLC.Display.setCursor(COL1_X, CONTENT_TOP - yOffset);
    M5StamPLC.Display.println("Settings");
    
    // SD Card information
    M5StamPLC.Display.setTextSize(2);
    M5StamPLC.Display.setTextColor(YELLOW);
    M5StamPLC.Display.setCursor(COL1_X, CONTENT_TOP + LINE_H2 * 2 - yOffset);
    M5StamPLC.Display.println("SD Card");
    
    M5StamPLC.Display.setTextSize(1);
    M5StamPLC.Display.setTextColor(WHITE);
    M5StamPLC.Display.setCursor(COL1_X, CONTENT_TOP + LINE_H2 * 4 - yOffset);
    if (sdModule && sdModule->isMounted()) {
        M5StamPLC.Display.println("Status: Mounted");
        
        // Show SD card info
        M5StamPLC.Display.setCursor(COL1_X, CONTENT_TOP + LINE_H2 * 5 - yOffset);
        drawSDInfo(COL1_X, CONTENT_TOP + LINE_H2 * 5 - yOffset);
    } else {
        M5StamPLC.Display.println("Status: Not mounted");
    }
}

