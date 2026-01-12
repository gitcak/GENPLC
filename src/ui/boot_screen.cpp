/*
 * Boot Screen Implementation
 * POST (Power-On Self-Test) boot screen display
 */

#include <M5StamPLC.h>
#include "boot_screen.h"
#include "../config/system_config.h"

struct BootLogEntry {
    String text;
    bool passed;
};

void drawBootScreen(const char* status, int progress, bool passed) {
    constexpr size_t BOOT_LOG_CAPACITY = 8;
    static BootLogEntry logEntries[BOOT_LOG_CAPACITY];
    static size_t logCount = 0;
    static int bootProgress = 0;

    if (progress >= 0) {
        if (progress > 100) progress = 100;
        if (progress < 0) progress = 0;
        bootProgress = progress;
    }

    if (status && status[0]) {
        BootLogEntry entry;
        entry.text = status;
        entry.passed = passed;
        if (logCount < BOOT_LOG_CAPACITY) {
            logEntries[logCount++] = entry;
        } else {
            for (size_t i = 0; i < BOOT_LOG_CAPACITY - 1; ++i) {
                logEntries[i] = logEntries[i + 1];
            }
            logEntries[BOOT_LOG_CAPACITY - 1] = entry;
        }
    }

    auto& display = M5StamPLC.Display;
    const int16_t width = display.width();
    const int16_t height = display.height();

    display.fillScreen(BLACK);

    display.setTextSize(2);
    display.setTextColor(CYAN, BLACK);
    display.setCursor(10, 8);
    display.print("StampPLC POST");

    display.setTextSize(1);
    display.setTextColor(WHITE, BLACK);
    display.setCursor(10, 26);
    display.printf("Firmware %s", STAMPLC_VERSION);

    const int barX = 10;
    const int barY = 38;
    const int barW = width - 20;
    const int barH = 10;
    display.drawRect(barX, barY, barW, barH, WHITE);
    int fillW = (barW - 2) * bootProgress / 100;
    if (fillW > 0) {
        display.fillRect(barX + 1, barY + 1, fillW, barH - 2, GREEN);
    }
    display.setCursor(width - 60, barY + 14);
    display.setTextColor(0x7BEF, BLACK);
    display.printf("%3d%%", bootProgress);

    display.setCursor(10, barY + 14);
    display.setTextColor(0x7BEF, BLACK);
    display.print("Diagnostics");

    const int logTop = barY + 26;
    const int lineHeight = 12;
    for (size_t i = 0; i < logCount; ++i) {
        const auto& entry = logEntries[i];
        display.setCursor(12, logTop + static_cast<int>(i) * lineHeight);
        display.setTextColor(entry.passed ? GREEN : RED, BLACK);
        display.printf("%s %s", entry.passed ? "[OK ]" : "[FAIL]", entry.text.c_str());
    }

    int logAreaBottom = logTop + static_cast<int>(logCount) * lineHeight;
    if (logAreaBottom < height) {
        display.fillRect(10, logAreaBottom, width - 20, height - logAreaBottom, BLACK);
    }
}

