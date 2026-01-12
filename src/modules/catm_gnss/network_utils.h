/*
 * Network Utilities
 * Network type detection and related utilities
 */

#ifndef NETWORK_UTILS_H
#define NETWORK_UTILS_H

#include <stdint.h>

// Get network type based on signal strength (simplified)
const char* getNetworkType(int8_t signalStrength);

#endif // NETWORK_UTILS_H

