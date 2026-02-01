/*
 * UI Constants
 * Shared constants for UI layout and rendering
 */

#ifndef UI_CONSTANTS_H
#define UI_CONSTANTS_H

#include <stdint.h>

// Display dimensions - use macros for consistency with system_config.h
// Note: DISPLAY_WIDTH/DISPLAY_HEIGHT may be defined in system_config.h
// The StampPLC has 240 width x 135 height in landscape orientation
#ifndef UI_DISPLAY_W
#define UI_DISPLAY_W 240
#endif
#ifndef UI_DISPLAY_H
#define UI_DISPLAY_H 135
#endif

// UI Layout Constants
constexpr int16_t STATUS_BAR_H  = 14;   // status bar height
constexpr int16_t BUTTON_BAR_Y   = 123;  // y baseline for button labels
constexpr int16_t CONTENT_TOP    = STATUS_BAR_H;   // = 14
constexpr int16_t CONTENT_BOTTOM = BUTTON_BAR_Y;   // = 123
constexpr int16_t LINE_H1        = 10;   // line height for textSize=1
constexpr int16_t LINE_H2        = 16;   // line height for textSize=2
constexpr int16_t COL1_X         = 5;
constexpr int16_t COL2_X         = 120;

constexpr int16_t VIEW_HEIGHT = (CONTENT_BOTTOM - CONTENT_TOP);
constexpr int16_t SCROLL_STEP = LINE_H1; // pixels per scroll step

// Helper functions
inline int16_t y1(int row) { return CONTENT_TOP + row * LINE_H1; }
inline int16_t y2(int row) { return CONTENT_TOP + row * LINE_H2; }

inline int16_t clampScroll(int16_t value, int16_t contentHeight) {
    if (contentHeight <= VIEW_HEIGHT) return 0;
    int16_t maxScroll = contentHeight - VIEW_HEIGHT;
    if (value < 0) return 0;
    if (value > maxScroll) return maxScroll;
    return value;
}

#endif // UI_CONSTANTS_H

