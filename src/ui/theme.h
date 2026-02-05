#ifndef UI_THEME_H
#define UI_THEME_H

#include <stdint.h>

namespace ui {

struct Theme {
    // ─── Core backgrounds ───
    uint16_t bg;              // Main background
    uint16_t card;            // Card fill
    uint16_t cardAlt;         // Alternate card (for nested elements)
    uint16_t surface;         // Elevated surface (modals, overlays)

    // ─── Borders & accents ───
    uint16_t border;          // Standard border
    uint16_t borderSubtle;    // Subtle/muted border
    uint16_t accent;          // Primary accent
    uint16_t accentBright;    // Bright accent (highlights, focus)
    uint16_t accentDim;       // Dimmed accent (selection background)

    // ─── Text hierarchy ───
    uint16_t text;            // Primary text
    uint16_t textSecondary;   // Secondary/muted text
    uint16_t textMuted;       // Disabled/hint text

    // ─── Semantic colors ───
    uint16_t green;           // Success, connected, valid
    uint16_t greenDim;        // Dimmed success (background tint)
    uint16_t yellow;          // Warning, searching, pending
    uint16_t yellowDim;       // Dimmed warning
    uint16_t red;             // Error, disconnected, invalid
    uint16_t redDim;          // Dimmed error
    uint16_t cyan;            // Info, action, interactive
    uint16_t cyanDim;         // Dimmed info
    uint16_t orange;          // Caution, attention

    // ─── Gradients (top -> bottom) ───
    uint16_t gradTop;         // Card gradient top
    uint16_t gradBottom;      // Card gradient bottom
    uint16_t gradAccentTop;   // Accent gradient top
    uint16_t gradAccentBottom;// Accent gradient bottom

    // ─── Modal/overlay specific ───
    uint16_t modalBg;         // Modal background
    uint16_t modalBorder;     // Modal border
    uint16_t overlay;         // Semi-transparent overlay tint

    // ─── Interactive elements ───
    uint16_t button;          // Button background
    uint16_t buttonText;      // Button text
    uint16_t buttonActive;    // Button pressed/active

    // ─── Data visualization ───
    uint16_t barBg;           // Progress/signal bar background
    uint16_t chartLine;       // Chart/graph lines

    // ─── Section headers ───
    uint16_t sectionHeader;   // Section header text color
    uint16_t sectionLine;     // Section separator line

    // ─── Layout metrics ───
    uint8_t radius;           // Standard corner radius
    uint8_t radiusSmall;      // Small corner radius (chips, tags)
    uint8_t pad;              // Standard padding
    uint8_t padSmall;         // Small padding
};

// Default dark theme tuned for 240x135 industrial display
const Theme& theme();

// Convenience: interpolate between two RGB565 colors
uint16_t lerpColor(uint16_t a, uint16_t b, uint8_t t);  // t: 0-255

}

#endif // UI_THEME_H
