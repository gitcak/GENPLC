#include "components.h"

static inline uint16_t lerp565(uint16_t a, uint16_t b, uint16_t t, uint16_t tmax) {
    // Extract RGB565
    uint16_t aR = (a >> 11) & 0x1F;
    uint16_t aG = (a >> 5)  & 0x3F;
    uint16_t aB =  a        & 0x1F;
    uint16_t bR = (b >> 11) & 0x1F;
    uint16_t bG = (b >> 5)  & 0x3F;
    uint16_t bB =  b        & 0x1F;
    uint16_t r = aR + (int32_t)(bR - aR) * t / tmax;
    uint16_t g = aG + (int32_t)(bG - aG) * t / tmax;
    uint16_t bb = aB + (int32_t)(bB - aB) * t / tmax;
    return (r << 11) | (g << 5) | (bb);
}

namespace ui {

void drawCard(lgfx::LGFX_Sprite* s, int x, int y, int w, int h,
              const char* title, lgfx::LGFX_Sprite* icon) {
    const auto& th = theme();
    // Base rounded container
    s->fillRoundRect(x, y, w, h, th.radius, th.card);

    // Gradient interior rectangle (inset to preserve rounded corners)
    int gx = x + 2;
    int gy = y + 2;
    int gw = w - 4;
    int gh = h - 4;
    if (gw > 0 && gh > 0) {
        for (int i = 0; i < gh; ++i) {
            uint16_t c = lerp565(th.gradTop, th.gradBottom, i, gh - 1);
            s->drawFastHLine(gx, gy + i, gw, c);
        }
    }

    // Border overlays
    s->drawRoundRect(x, y, w, h, th.radius, th.border);
    s->drawRoundRect(x+1, y+1, w-2, h-2, th.radius-1, th.accent);

    // Layout: larger icon and title
    int iconW = 0, iconH = 0;
    if (icon) { iconW = icon->width(); iconH = icon->height(); }
    // Fallback if icon not set or 0 size
    if (iconW <= 0 || iconH <= 0) { iconW = 0; iconH = 0; }

    int tx = x + th.pad + (iconW ? iconW + 8 : 0);
    int ty = y + (h >= 56 ? 12 : 8);

    if (iconW && iconH && icon) {
        int ix = x + th.pad;
        int iy = y + (h - iconH) / 2;
        icon->pushSprite(s, ix, iy);
    }

    s->setTextColor(th.text);
    // Scale title text based on card height
    int titleSize = (h >= 64) ? 3 : (h >= 48 ? 2 : 1);
    s->setTextSize(titleSize);
    s->setCursor(tx, ty);
    s->print(title ? title : "");
}

void drawKV(lgfx::LGFX_Sprite* s, int x, int y,
            const char* key, const char* value, uint16_t color) {
    const auto& th = theme();
    s->setTextSize(1);
    s->setTextColor(th.textSecondary);
    s->setCursor(x, y);
    s->print(key ? key : "");
    s->setTextColor(color);
    s->setCursor(x + 64, y); // simple two-column layout
    s->print(value ? value : "");
}

void drawBar(lgfx::LGFX_Sprite* s, int x, int y, int w, int h,
             int percent, uint16_t color) {
    const auto& th = theme();
    if (percent < 0) percent = 0; if (percent > 100) percent = 100;
    s->fillRect(x, y, w, h, th.card);
    s->drawRect(x, y, w, h, th.text);
    int fill = (w - 2) * percent / 100;
    if (fill > 0) s->fillRect(x + 1, y + 1, fill, h - 2, color);
}

void drawCardCentered(lgfx::LGFX_Sprite* s, int x, int y, int w, int h,
                      const char* title, lgfx::LGFX_Sprite* icon) {
    const auto& th = theme();
    // Same container and gradient as drawCard
    s->fillRoundRect(x, y, w, h, th.radius, th.card);
    int gx = x + 2, gy = y + 2, gw = w - 4, gh = h - 4;
    if (gw > 0 && gh > 0) {
        for (int i = 0; i < gh; ++i) {
            uint16_t c = lerp565(th.gradTop, th.gradBottom, i, gh - 1);
            s->drawFastHLine(gx, gy + i, gw, c);
        }
    }
    s->drawRoundRect(x, y, w, h, th.radius, th.border);
    s->drawRoundRect(x+1, y+1, w-2, h-2, th.radius-1, th.accent);

    // Determine icon dimensions
    int iconW = 0, iconH = 0;
    if (icon) { iconW = icon->width(); iconH = icon->height(); }
    if (iconW <= 0 || iconH <= 0) { iconW = 0; iconH = 0; icon = nullptr; }

    // Choose largest title size that fits horizontally with icon
    const char* t = title ? title : "";
    int gap = icon ? 8 : 0;
    int maxTextW = w - 2 * th.pad - iconW - gap;
    int chosenSize = 3; // try big first
    for (; chosenSize >= 1; --chosenSize) {
        s->setTextSize(chosenSize);
        int tw = s->textWidth(t);
        if (tw <= maxTextW) break;
    }
    if (chosenSize < 1) { chosenSize = 1; s->setTextSize(1); }
    int tw = s->textWidth(t);

    // Total width of icon + text
    int totalW = (icon ? iconW + gap : 0) + tw;
    int startX = x + (w - totalW) / 2;

    // Vertical placement: align around center of the card
    int approxTextH = 8 * chosenSize; // approximate baseline height
    int centerY = y + h / 2;
    int iy = centerY - (iconH / 2);
    int ty = centerY - (approxTextH / 2);

    // Draw icon then text
    if (icon) {
        icon->pushSprite(s, startX, iy);
    }
    s->setTextColor(th.text);
    s->setCursor(startX + (icon ? (iconW + gap) : 0), ty);
    s->print(t);
}

}
