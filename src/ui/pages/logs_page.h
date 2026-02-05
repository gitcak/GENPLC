/*
 * Logs Page
 * System logs page rendering
 */

#ifndef LOGS_PAGE_H
#define LOGS_PAGE_H

#include <stdint.h>

// Draw the logs page
void drawLogsPage();

// Get the content height for scroll calculations
// This is dynamic based on log_count()
// Returns the total height of page content in pixels
int16_t logsPageContentHeight();

#endif // LOGS_PAGE_H
