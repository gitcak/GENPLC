/*
 * GNSS Page Implementation
 * GNSS status and position data
 */

#include <M5StamPLC.h>
#include "gnss_page.h"
#include "../ui_constants.h"
#include "../../modules/catm_gnss/catm_gnss_module.h"

// External globals
extern CatMGNSSModule* catmGnssModule;
extern volatile int16_t scrollGNSS;

void drawGNSSPage() {
    int16_t yOffset = scrollGNSS;
    // Title
    M5StamPLC.Display.setTextColor(WHITE);
    M5StamPLC.Display.setTextSize(2);
    M5StamPLC.Display.setCursor(COL1_X, CONTENT_TOP - yOffset);
    M5StamPLC.Display.println("GNSS Status");
    
    // GNSS data
    if (catmGnssModule && catmGnssModule->isModuleInitialized()) {
        GNSSData data = catmGnssModule->getGNSSData();
        auto formatBytes = [](uint64_t bytes) -> String {
            static const char* units[] = {"B", "KB", "MB", "GB"};
            double value = static_cast<double>(bytes);
            size_t idx = 0;
            while (value >= 1024.0 && idx < 3) {
                value /= 1024.0;
                ++idx;
            }
            char buf[32];
            if (idx == 0 || value >= 100.0) {
                snprintf(buf, sizeof(buf), "%.0f %s", value, units[idx]);
            } else {
                snprintf(buf, sizeof(buf), "%.1f %s", value, units[idx]);
            }
            return String(buf);
        };

        auto formatRate = [](uint32_t bytesPerSec) -> String {
            if (bytesPerSec == 0) {
                return String("0 B/s");
            }
            static const char* units[] = {"B/s", "KB/s", "MB/s"};
            double value = static_cast<double>(bytesPerSec);
            size_t idx = 0;
            while (value >= 1024.0 && idx < 2) {
                value /= 1024.0;
                ++idx;
            }
            char buf[32];
            if (idx == 0 || value >= 100.0) {
                snprintf(buf, sizeof(buf), "%.0f %s", value, units[idx]);
            } else {
                snprintf(buf, sizeof(buf), "%.1f %s", value, units[idx]);
            }
            return String(buf);
        };

        
        M5StamPLC.Display.setTextSize(1);
        M5StamPLC.Display.setTextColor(YELLOW);
        M5StamPLC.Display.setCursor(COL1_X, CONTENT_TOP + LINE_H2 - yOffset);
        M5StamPLC.Display.println("Position Data:");
        
        M5StamPLC.Display.setTextColor(WHITE);
        if (data.isValid) {
            M5StamPLC.Display.setCursor(COL1_X, CONTENT_TOP + LINE_H2 + LINE_H1 - yOffset);
            M5StamPLC.Display.printf("Latitude:  %.6f", data.latitude);
            
            M5StamPLC.Display.setCursor(COL1_X, CONTENT_TOP + LINE_H2 + LINE_H1 * 2 - yOffset);
            M5StamPLC.Display.printf("Longitude: %.6f", data.longitude);
            
            M5StamPLC.Display.setCursor(COL1_X, CONTENT_TOP + LINE_H1 * 3 - yOffset);
            M5StamPLC.Display.printf("Altitude:  %.1f m", data.altitude);
            
            M5StamPLC.Display.setCursor(COL1_X, CONTENT_TOP + LINE_H1 * 4 - yOffset);
            M5StamPLC.Display.printf("Speed:     %.1f km/h", data.speed);
            
            M5StamPLC.Display.setCursor(COL1_X, CONTENT_TOP + LINE_H1 * 5 - yOffset);
            M5StamPLC.Display.printf("Course:    %.1f deg", data.course);
        } else {
            M5StamPLC.Display.setTextColor(RED);
            M5StamPLC.Display.setCursor(COL1_X, CONTENT_TOP + LINE_H2 + LINE_H1 - yOffset);
            M5StamPLC.Display.println("No valid fix");
        }
        
        M5StamPLC.Display.setTextColor(YELLOW);
        M5StamPLC.Display.setCursor(COL1_X, CONTENT_TOP + LINE_H1 * 6 - yOffset);
        M5StamPLC.Display.println("Satellites:");
        
        M5StamPLC.Display.setTextColor(WHITE);
        M5StamPLC.Display.setCursor(COL1_X, CONTENT_TOP + LINE_H1 * 7 - yOffset);
        M5StamPLC.Display.printf("In view: %d", data.satellites);
        
        // Time data
        if (data.isValid) {
            M5StamPLC.Display.setTextColor(CYAN);
            M5StamPLC.Display.setCursor(COL2_X, CONTENT_TOP - yOffset);
            M5StamPLC.Display.println("UTC Time:");
            
            M5StamPLC.Display.setTextColor(WHITE);
            M5StamPLC.Display.setCursor(COL2_X, CONTENT_TOP + LINE_H2 - yOffset);
            M5StamPLC.Display.printf("%04d-%02d-%02d", data.year, data.month, data.day);
            
            M5StamPLC.Display.setCursor(COL2_X, CONTENT_TOP + LINE_H1 * 2 - yOffset);
            M5StamPLC.Display.printf("%02d:%02d:%02d", data.hour, data.minute, data.second);
        }
    } else {
        M5StamPLC.Display.setTextSize(1);
        M5StamPLC.Display.setTextColor(RED);
        M5StamPLC.Display.setCursor(10, 60);
        M5StamPLC.Display.println("GNSS module not initialized");
    }
}

