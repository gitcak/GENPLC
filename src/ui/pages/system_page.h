/*
 * System Page
 * System status page rendering
 */

#ifndef SYSTEM_PAGE_H
#define SYSTEM_PAGE_H

#include <stdint.h>

// Draw the system status page
void drawSystemPage();

// Get the content height for scroll calculations
// Returns the total height of page content in pixels
int16_t systemPageContentHeight();

#endif // SYSTEM_PAGE_H
