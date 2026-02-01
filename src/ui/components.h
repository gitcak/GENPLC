/*
 * UI Components Header
 *
 * NOTE: This module previously contained sprite-based card drawing functions
 * (drawCard, drawCardCentered, drawKV, drawBar) that were never used.
 * They have been removed to reduce dead code and memory footprint.
 *
 * The page-specific drawing helpers now live in each page's .cpp file,
 * which is simpler and avoids unused abstractions.
 */

#ifndef UI_COMPONENTS_H
#define UI_COMPONENTS_H

#include <M5Unified.h>
#include <M5GFX.h>
#include "theme.h"

namespace ui {

// Currently empty - page-specific helpers are defined locally in each page

}

#endif // UI_COMPONENTS_H
