/*
 * UI Theme Implementation
 * Modern dark theme optimized for 240x135 industrial display
 */

#include "theme.h"

namespace ui {

// ═══════════════════════════════════════════════════════════════════════════
// COLOR PALETTE - Modern Industrial Dark Theme
// ═══════════════════════════════════════════════════════════════════════════
// Design philosophy:
// - Deep navy/charcoal backgrounds for reduced eye strain
// - Cyan/teal as primary accent (industrial/tech aesthetic)
// - High contrast text for readability on small display
// - Semantic colors follow industry conventions (green=OK, red=error, etc.)

static Theme s_theme {
    // ─── Core backgrounds ───
    /*bg*/              0x0841,   // Deep charcoal navy (#0C1021)
    /*card*/            0x1082,   // Slightly lighter card (#182838)
    /*cardAlt*/         0x18C3,   // Alternate card (#1C3444)
    /*surface*/         0x2104,   // Elevated surface (#202040)

    // ─── Borders & accents ───
    /*border*/          0x3186,   // Soft gray border (#30303C)
    /*borderSubtle*/    0x2104,   // Very subtle border (#202028)
    /*accent*/          0x4E7C,   // Teal accent (#4CCCE0) - desaturated for comfort
    /*accentBright*/    0x07FF,   // Bright cyan (#00FFFF) - for highlights
    /*accentDim*/       0x1293,   // Dark teal tint (#102838) - selection bg

    // ─── Text hierarchy ───
    /*text*/            0xFFFF,   // Pure white
    /*textSecondary*/   0xB5B6,   // Light gray (#B0B0B0)
    /*textMuted*/       0x7BCF,   // Muted gray (#787878)

    // ─── Semantic colors ───
    /*green*/           0x47EA,   // Mint green (#44FD50) - modern, not harsh
    /*greenDim*/        0x0320,   // Dark green tint (#003C00)
    /*yellow*/          0xFE60,   // Warm amber (#FFD000)
    /*yellowDim*/       0x4200,   // Dark amber tint (#403000)
    /*red*/             0xF8A2,   // Coral red (#FF1414) - visible but not alarming
    /*redDim*/          0x4000,   // Dark red tint (#400000)
    /*cyan*/            0x07FF,   // Bright cyan (#00FFFF)
    /*cyanDim*/         0x0210,   // Dark cyan tint (#002828)
    /*orange*/          0xFC60,   // Warm orange (#FF8C00)

    // ─── Gradients (modern subtle gradients) ───
    /*gradTop*/         0x2124,   // Purple-ish top (#201844)
    /*gradBottom*/      0x0208,   // Teal-ish bottom (#001828)
    /*gradAccentTop*/   0x4A69,   // Accent gradient top
    /*gradAccentBottom*/0x0210,   // Accent gradient bottom

    // ─── Modal/overlay specific ───
    /*modalBg*/         0x18E3,   // Modal background (#1C1C28)
    /*modalBorder*/     0xFE60,   // Amber border for warnings
    /*overlay*/         0x0000,   // Pure black overlay base

    // ─── Interactive elements ───
    /*button*/          0x2945,   // Button background (#283848)
    /*buttonText*/      0x07FF,   // Cyan button text
    /*buttonActive*/    0x3A07,   // Pressed button (#384058)

    // ─── Data visualization ───
    /*barBg*/           0x1082,   // Bar background
    /*chartLine*/       0x4E7C,   // Chart lines (accent)

    // ─── Section headers ───
    /*sectionHeader*/   0xFE60,   // Amber section headers
    /*sectionLine*/     0x2945,   // Section separator

    // ─── Layout metrics ───
    /*radius*/          8,        // Standard corner radius
    /*radiusSmall*/     4,        // Small corner radius
    /*pad*/             8,        // Standard padding
    /*padSmall*/        4         // Small padding
};

const Theme& theme() { return s_theme; }

// ═══════════════════════════════════════════════════════════════════════════
// UTILITY: Color interpolation for gradients and animations
// ═══════════════════════════════════════════════════════════════════════════
uint16_t lerpColor(uint16_t a, uint16_t b, uint8_t t) {
    // Extract RGB565 components
    uint16_t aR = (a >> 11) & 0x1F;
    uint16_t aG = (a >> 5)  & 0x3F;
    uint16_t aB =  a        & 0x1F;
    uint16_t bR = (b >> 11) & 0x1F;
    uint16_t bG = (b >> 5)  & 0x3F;
    uint16_t bB =  b        & 0x1F;

    // Interpolate (t is 0-255)
    uint16_t r = aR + ((int32_t)(bR - aR) * t / 255);
    uint16_t g = aG + ((int32_t)(bG - aG) * t / 255);
    uint16_t bb = aB + ((int32_t)(bB - aB) * t / 255);

    return (r << 11) | (g << 5) | bb;
}

}
