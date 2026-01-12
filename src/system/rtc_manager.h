/*
 * RTC Manager Header
 * Real-Time Clock management and synchronization
 */

#ifndef RTC_MANAGER_H
#define RTC_MANAGER_H

#include <time.h>
#include "../modules/catm_gnss/gnss_status.h"

// RTC synchronization functions
bool setRTCFromCellular();
bool setRTCFromGPS(const GNSSData& gnssData);
bool setRTCFromBuildTimestamp();
bool getRTCTime(struct tm& timeinfo);

#endif // RTC_MANAGER_H

