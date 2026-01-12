/*
 * Logs Page Implementation
 * System log viewer
 */

#include <M5StamPLC.h>
#include "logs_page.h"
#include "../ui_constants.h"
#include "../../modules/logging/log_buffer.h"

// External globals
extern volatile int16_t scrollLOGS;

void drawLogsPage() {
    int16_t yOffset = scrollLOGS;
    M5StamPLC.Display.setTextColor(WHITE);
    M5StamPLC.Display.setTextSize(2);
    M5StamPLC.Display.setCursor(COL1_X, CONTENT_TOP - yOffset);
    M5StamPLC.Display.println("Logs");

    // Draw recent lines (oldest at top)
    M5StamPLC.Display.setTextSize(1);
    const int pad = 2;
    size_t cnt = log_count();
    int contentH = (int)cnt * LINE_H1 + pad * 2;
    scrollLOGS = clampScroll(scrollLOGS, contentH);

    int y = CONTENT_TOP + LINE_H2 - yOffset;
    char line[160];
    for (size_t i = 0; i < cnt; ++i) {
        if (log_get_line(i, line, sizeof(line))) {
            if (y > CONTENT_TOP && y < CONTENT_BOTTOM) {
                M5StamPLC.Display.setCursor(COL1_X, y);
                M5StamPLC.Display.print(line);
            }
            y += LINE_H1;
        }
    }
}

