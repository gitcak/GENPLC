/*
 * GNSS Page
 * GNSS status page rendering
 */

#ifndef GNSS_PAGE_H
#define GNSS_PAGE_H

#include <stdint.h>

// Draw the GNSS page
void drawGNSSPage();

// Get the content height for scroll calculations
// Returns the total height of page content in pixels
int16_t gnssPageContentHeight();

#endif // GNSS_PAGE_H
