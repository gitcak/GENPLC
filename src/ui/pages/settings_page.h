/*
 * Settings Page
 * Interactive settings with brightness and sleep controls
 */

#ifndef SETTINGS_PAGE_H
#define SETTINGS_PAGE_H

#include <stdint.h>

// Settings selection index (for button navigation)
enum class SettingsItem : uint8_t {
    BRIGHTNESS = 0,
    SLEEP_TOGGLE,
    SLEEP_TIMEOUT,
    ITEM_COUNT
};

// Get/set the currently selected settings item
SettingsItem settingsGetSelected();
void settingsSetSelected(SettingsItem item);

// Adjust the selected setting (direction: -1 = decrease, +1 = increase)
void settingsAdjustValue(int8_t direction);

// Draw the settings page
void drawSettingsPage();

// Get the content height for scroll calculations
// Returns the total height of page content in pixels
int16_t settingsPageContentHeight();

#endif // SETTINGS_PAGE_H
