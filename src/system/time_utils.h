/*
 * Time Utilities Header
 * NTP configuration, time synchronization, and timezone management
 */

#ifndef TIME_UTILS_H
#define TIME_UTILS_H

#include <time.h>

// NTP and time synchronization
bool fetchNtpTimeViaCellular(struct tm& timeInfo);
void ensureNtpConfigured();
void maybeUpdateTimeZoneFromCellular();
void syncRTCFromAvailableSources();

// Time formatting
void formatLocalFromUTC(const struct tm& utcIn, char* timeStr, char* dateStr);

#endif // TIME_UTILS_H

