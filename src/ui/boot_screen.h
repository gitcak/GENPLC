/*
 * Boot Screen Header
 * POST (Power-On Self-Test) boot screen display
 */

#ifndef BOOT_SCREEN_H
#define BOOT_SCREEN_H

void drawBootScreen(const char* status, int progress, bool passed = true);

#endif // BOOT_SCREEN_H

