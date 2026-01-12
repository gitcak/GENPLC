/*
 * Network Utilities Implementation
 * Network type detection and related utilities
 */

#include "network_utils.h"

const char* getNetworkType(int8_t signalStrength) {
    // This is a simplified version - real detection would need AT commands
    if (signalStrength < -100) return "---";
    if (signalStrength < -85) return "2G";
    if (signalStrength < -75) return "3G";
    return "LTE";
}

