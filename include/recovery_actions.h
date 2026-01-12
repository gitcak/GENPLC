/*
 * Recovery Actions Enum
 * Shared enumeration for system recovery actions
 */

#ifndef RECOVERY_ACTIONS_H
#define RECOVERY_ACTIONS_H

#include <stdint.h>

// ============================================================================
// RECOVERY ACTIONS ENUM
// ============================================================================
enum class RecoveryAction : uint8_t {
    RECOVERY_NONE = 0,
    RECOVERY_RETRY = 1,
    RESTART_TASK = 2,
    RESTART_MODULE = 3,
    SYSTEM_RESTART = 4,
    DEGRADE_SYSTEM = 5,
    CLEAR_BUFFERS = 6,
    RESET_HARDWARE = 7,
    SAFE_SHUTDOWN = 8
};

#endif // RECOVERY_ACTIONS_H
