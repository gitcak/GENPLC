#ifndef UI_FONTS_H
#define UI_FONTS_H

#include <M5GFX.h>

namespace ui { namespace fonts {

// Initialize font system (detect SD fonts)
bool init();

// Apply selected font to display or sprite; returns true if applied
bool applyToDisplay();
bool applyToSprite(lgfx::LGFX_Sprite* s);

} }

#endif // UI_FONTS_H