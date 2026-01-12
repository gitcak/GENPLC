/*
 * Cellular Page Implementation
 * Cellular network status and statistics
 */

#include "cellular_page.h"
#include "../ui_constants.h"
#include "../theme.h"
#include "../../modules/catm_gnss/catm_gnss_module.h"
#include "../components/ui_widgets.h"

// External globals
extern CatMGNSSModule* catmGnssModule;
extern volatile int16_t scrollCELL;

void drawCellularPage() {
    const auto& th = ui::theme();
    int16_t yOffset = scrollCELL;
    
    // Title with theme
    M5StamPLC.Display.setTextColor(th.text, th.bg);
    M5StamPLC.Display.setTextSize(2);
    M5StamPLC.Display.setCursor(COL1_X, CONTENT_TOP - yOffset);
    M5StamPLC.Display.println("Cellular Status");
    
    // Cellular data
    if (catmGnssModule && catmGnssModule->isModuleInitialized()) {
        CellularData data = catmGnssModule->getCellularData();
        char txSummary[64], rxSummary[64];
        snprintf(txSummary, sizeof(txSummary), "%llu bytes (%u bps)", data.txBytes, data.txBps);
        snprintf(rxSummary, sizeof(rxSummary), "%llu bytes (%u bps)", data.rxBytes, data.rxBps);
        
        M5StamPLC.Display.setTextSize(1);
        
        // Network Status section header
        M5StamPLC.Display.setTextColor(th.yellow, th.bg);
        M5StamPLC.Display.setCursor(COL1_X, CONTENT_TOP + LINE_H2 - yOffset);
        M5StamPLC.Display.println("Network Status:");
        
        // Connection status with color coding
        M5StamPLC.Display.setTextColor(data.isConnected ? th.green : th.red, th.bg);
        M5StamPLC.Display.setCursor(COL1_X, CONTENT_TOP + LINE_H1 - yOffset);
        M5StamPLC.Display.printf("Connected: %s", data.isConnected ? "YES" : "NO");
        
        // Operator info
        M5StamPLC.Display.setTextColor(th.text, th.bg);
        M5StamPLC.Display.setCursor(COL1_X, CONTENT_TOP + LINE_H1 * 2 - yOffset);
        M5StamPLC.Display.printf("Operator: %s", data.operatorName.c_str());
        
        // Signal strength bar
        drawSignalBar(COL1_X, CONTENT_TOP + LINE_H1 * 3 - yOffset, 120, 12, data.signalStrength);
        
        // Device Info section header
        M5StamPLC.Display.setTextColor(th.yellow, th.bg);
        M5StamPLC.Display.setCursor(COL1_X, CONTENT_TOP + LINE_H1 * 5 - yOffset);
        M5StamPLC.Display.println("Device Info:");
        
        // Device information
        M5StamPLC.Display.setTextColor(th.text, th.bg);
        M5StamPLC.Display.setCursor(COL1_X, CONTENT_TOP + LINE_H1 * 6 - yOffset);
        M5StamPLC.Display.printf("IMEI: %s", data.imei.c_str());
        
        M5StamPLC.Display.setCursor(COL1_X, CONTENT_TOP + LINE_H1 * 7 - yOffset);
        M5StamPLC.Display.printf("Last Update: %d ms ago", millis() - data.lastUpdate);
        
        // Error count with color coding
        M5StamPLC.Display.setTextColor(data.errorCount > 0 ? th.red : th.textSecondary, th.bg);
        M5StamPLC.Display.setCursor(COL1_X, CONTENT_TOP + LINE_H1 * 8 - yOffset);
        M5StamPLC.Display.printf("Errors: %d", data.errorCount);
        
        // Data transmission info
        M5StamPLC.Display.setTextColor(th.textSecondary, th.bg);
        M5StamPLC.Display.setCursor(COL1_X, CONTENT_TOP + LINE_H1 * 9 - yOffset);
        M5StamPLC.Display.printf("Tx: %s", txSummary);
        M5StamPLC.Display.setCursor(COL1_X, CONTENT_TOP + LINE_H1 * 10 - yOffset);
        M5StamPLC.Display.printf("Rx: %s", rxSummary);
        
        // Add some spacing at bottom
        M5StamPLC.Display.setCursor(COL1_X, CONTENT_BOTTOM - LINE_H1 * 2 - yOffset);
        M5StamPLC.Display.print(""); // Empty line for spacing
    } else {
        // Error state with theme colors
        M5StamPLC.Display.setTextSize(1);
        M5StamPLC.Display.setTextColor(th.red, th.bg);
        M5StamPLC.Display.setCursor(10, 60);
        M5StamPLC.Display.println("Cellular module not initialized");
    }
}
