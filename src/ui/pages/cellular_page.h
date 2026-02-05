/*
 * Cellular Page
 * Cellular status page rendering
 */

#ifndef CELLULAR_PAGE_H
#define CELLULAR_PAGE_H

#include <stdint.h>

// Draw the cellular status page
void drawCellularPage();

// Get the content height for scroll calculations
// Returns the total height of page content in pixels
int16_t cellularPageContentHeight();

#endif // CELLULAR_PAGE_H
