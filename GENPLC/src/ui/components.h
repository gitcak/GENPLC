#ifndef UI_COMPONENTS_H
#define UI_COMPONENTS_H

#include <M5Unified.h>
#include <M5GFX.h>
#include "theme.h"

namespace ui {

// Draw a titled card with optional small icon in the header
void drawCard(lgfx::LGFX_Sprite* s, int x, int y, int w, int h,
              const char* title, lgfx::LGFX_Sprite* icon = nullptr);

// Draw a card with centered title + icon (horizontally and roughly vertically)
void drawCardCentered(lgfx::LGFX_Sprite* s, int x, int y, int w, int h,
                      const char* title, lgfx::LGFX_Sprite* icon = nullptr);

// Key/Value row (single line)
void drawKV(lgfx::LGFX_Sprite* s, int x, int y,
            const char* key, const char* value, uint16_t color = theme().text);

// Horizontal percentage bar with border
void drawBar(lgfx::LGFX_Sprite* s, int x, int y, int w, int h,
             int percent, uint16_t color);

}

#endif // UI_COMPONENTS_H
