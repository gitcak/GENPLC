#ifndef UI_THEME_H
#define UI_THEME_H

#include <stdint.h>

namespace ui {

struct Theme {
    uint16_t bg;
    uint16_t card;
    uint16_t border;
    uint16_t accent;
    uint16_t text;
    uint16_t textSecondary;
    uint16_t green;
    uint16_t yellow;
    uint16_t red;
    // Gradient colors for cards (top -> bottom)
    uint16_t gradTop;
    uint16_t gradBottom;
    uint8_t radius;
    uint8_t pad;
};

// Default dark theme tuned for 240x135
const Theme& theme();

}

#endif // UI_THEME_H
