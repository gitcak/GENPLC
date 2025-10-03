#pragma once

#include <Arduino.h>
#include "basic_stamplc.h"

/**
 * @file rs485_adapter.h
 * @brief Defines structures and classes for monitoring an RS485 adapter.
 *
 * This file provides the `Rs485AdapterStatus` struct for holding status information
 * and the `Rs485Adapter` class for managing and monitoring the RS485 bus health.
 */

/**
 * @struct Rs485AdapterStatus
 * @brief Holds the current status of the RS485 adapter.
 *
 * This struct aggregates various metrics like availability, error counts, uptime,
 * and provides formatted strings for display.
 */
struct Rs485AdapterStatus {
    bool available;             ///< True if the RS485 bus is considered available (recent communication).
    bool initialized;           ///< True if the adapter has been initialized.
    uint32_t errorCount;        ///< The total number of communication errors recorded.
    uint32_t lastFrameTime;     ///< The timestamp of the last successfully received frame.
    uint32_t uptimeSeconds;     ///< The total uptime of the adapter in seconds.
    float lineVoltage;          ///< The line voltage, if measurable via analog inputs.
    String statusText;          ///< A detailed text description of the current status.
    String formattedSummary;    ///< A concise, formatted summary of the status.

    /**
     * @brief Creates an Rs485AdapterStatus object from a BasicStampPLC instance.
     * @param stampPLC A pointer to the BasicStampPLC object.
     * @return A populated Rs485AdapterStatus object.
     */
    static Rs485AdapterStatus fromStampPLC(BasicStampPLC* stampPLC);
};

/**
 * @class Rs485Adapter
 * @brief Manages and monitors the health of an RS485 communication bus.
 *
 * This class tracks communication frames, errors, and uptime to determine the
 * overall status of the RS485 adapter.
 */
class Rs485Adapter {
private:
    BasicStampPLC* stampPLC;    ///< Pointer to the BasicStampPLC instance for hardware interaction.
    uint32_t lastFrameTime;     ///< Timestamp of the last successful frame reception.
    uint32_t startTime;         ///< Timestamp when the adapter was started.
    uint32_t errorCount;        ///< Counter for communication errors.

public:
    /**
     * @brief Construct a new Rs485Adapter object.
     */
    Rs485Adapter();

    /**
     * @brief Initializes the adapter with a reference to the StampPLC.
     * @param plc A pointer to the BasicStampPLC instance.
     */
    void setStampPLC(BasicStampPLC* plc);

    /**
     * @brief Gets the current status of the RS485 adapter.
     * @return An Rs485AdapterStatus struct containing the current status metrics.
     */
    Rs485AdapterStatus getStatus();

    /**
     * @brief Records that a frame has been successfully received.
     * Updates the last frame timestamp.
     */
    void recordFrameReceived();

    /**
     * @brief Records that a communication error has occurred.
     * Increments the error counter.
     */
    void recordError();

    /**
     * @brief Gets the time elapsed since the last frame was received.
     * @return The age of the last frame in seconds.
     */
    uint32_t getLastFrameAge() const;
};