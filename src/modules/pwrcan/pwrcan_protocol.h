#ifndef PWRCAN_PROTOCOL_H
#define PWRCAN_PROTOCOL_H

#include <Arduino.h>
#include <stdint.h>

// Lightweight Generator MCU protocol skeleton over CAN (Classic, 8-byte frames)
// - No logging here
// - Pure pack/unpack helpers and message IDs

namespace GeneratorCAN {

static constexpr uint8_t PROTOCOL_VERSION_MAJOR = 1;
static constexpr uint8_t PROTOCOL_VERSION_MINOR = 0;

// 11-bit standard identifiers (example allocation)
enum class MessageId : uint16_t {
    Heartbeat       = 0x100,   // Device health + uptime
    Status          = 0x101,   // Discrete status bits / flags
    Telemetry       = 0x180,   // Basic telemetry snapshot
    Command         = 0x200,   // Control commands to Generator MCU
    CommandAck      = 0x201,   // Acknowledgement for Command
};

// Heartbeat status codes
enum class NodeStatus : uint8_t {
    Booting = 0,
    Ok      = 1,
    Warn    = 2,
    Error   = 3,
};

// Command set (expand as needed)
enum class CommandId : uint8_t {
    Noop   = 0,
    Start  = 1,
    Stop   = 2,
    Reset  = 3,
};

// Heartbeat payload (max 8 bytes)
struct HeartbeatPayload {
    uint8_t  versionMajor;     // protocol major
    uint8_t  versionMinor;     // protocol minor
    uint8_t  nodeId;           // sender node id
    uint8_t  status;           // NodeStatus
    uint16_t uptimeSeconds;    // truncated uptime seconds
    uint16_t reserved;         // alignment / future
};
static_assert(sizeof(HeartbeatPayload) <= 8, "HeartbeatPayload must fit in 8 bytes");

// Status payload (bitflags + error code)
struct StatusPayload {
    uint16_t flags;            // bit flags (custom)
    int16_t  lastError;        // last error code (signed)
    int16_t  temperatureC10;   // temperature x10 (e.g., 251 => 25.1C)
    uint16_t reserved;         // future
};
static_assert(sizeof(StatusPayload) <= 8, "StatusPayload must fit in 8 bytes");

// Telemetry payload (compact snapshot)
struct TelemetryPayload {
    uint16_t voltageC10;       // volts x10
    int16_t  currentC10;       // amps x10 (signed)
    uint16_t rpm;              // generator RPM
    uint16_t reserved;         // future
};
static_assert(sizeof(TelemetryPayload) <= 8, "TelemetryPayload must fit in 8 bytes");

// Command payload
struct CommandPayload {
    uint8_t  commandId;        // CommandId
    uint8_t  param8;           // small parameter
    uint16_t param16;          // parameter (16-bit)
    uint32_t param32;          // parameter (32-bit)
};
static_assert(sizeof(CommandPayload) <= 8, "CommandPayload must fit in 8 bytes");

// Command acknowledgement
struct CommandAckPayload {
    uint8_t  commandId;        // CommandId being acknowledged
    uint8_t  result;           // 0=OK, else error
    uint16_t detail;           // optional detail code
    uint32_t reserved;         // future
};
static_assert(sizeof(CommandAckPayload) <= 8, "CommandAckPayload must fit in 8 bytes");

// Pack helpers (memcpy-based; CAN is byte-oriented; order is little-endian here by layout)
template <typename T>
inline bool pack(const T &payload, uint8_t *outBuf, uint8_t &outLen) {
    if (outBuf == nullptr) return false;
    constexpr size_t sz = sizeof(T);
    if (sz > 8) return false;
    memcpy(outBuf, &payload, sz);
    outLen = static_cast<uint8_t>(sz);
    return true;
}

template <typename T>
inline bool unpack(const uint8_t *inBuf, uint8_t inLen, T &outPayload) {
    if (inBuf == nullptr) return false;
    constexpr size_t sz = sizeof(T);
    if (inLen < sz) return false;
    memcpy(&outPayload, inBuf, sz);
    return true;
}

// Convenience wrappers
inline bool packHeartbeat(uint8_t nodeId, NodeStatus status, uint16_t uptimeSeconds,
                          uint8_t *outBuf, uint8_t &outLen) {
    HeartbeatPayload p{};
    p.versionMajor = PROTOCOL_VERSION_MAJOR;
    p.versionMinor = PROTOCOL_VERSION_MINOR;
    p.nodeId       = nodeId;
    p.status       = static_cast<uint8_t>(status);
    p.uptimeSeconds= uptimeSeconds;
    p.reserved     = 0;
    return pack(p, outBuf, outLen);
}

inline bool unpackHeartbeat(const uint8_t *inBuf, uint8_t inLen, HeartbeatPayload &out) {
    return unpack(inBuf, inLen, out);
}

inline bool packCommand(CommandId cmd, uint8_t p8, uint16_t p16, uint32_t p32,
                        uint8_t *outBuf, uint8_t &outLen) {
    CommandPayload p{};
    p.commandId = static_cast<uint8_t>(cmd);
    p.param8    = p8;
    p.param16   = p16;
    p.param32   = p32;
    return pack(p, outBuf, outLen);
}

inline bool unpackCommand(const uint8_t *inBuf, uint8_t inLen, CommandPayload &out) {
    return unpack(inBuf, inLen, out);
}

} // namespace GeneratorCAN

#endif // PWRCAN_PROTOCOL_H


