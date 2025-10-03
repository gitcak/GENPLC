#pragma once

#include <Arduino.h>
#include "generator_status.h"

/**
 * @file generator_calibration.h
 * @brief Functions for managing generator calibration data.
 *
 * This file declares functions for loading, saving, and resetting generator
 * calibration settings stored in Non-Volatile Storage (NVS).
 */

/**
 * @brief Loads the generator calibration settings from NVS.
 *
 * If no calibration data is found in NVS, it returns a default
 * calibration configuration.
 *
 * @return A GeneratorCalibration struct containing the loaded or default settings.
 */
GeneratorCalibration getGeneratorCalibration();

/**
 * @brief Saves the given generator calibration settings to NVS.
 *
 * @param cal A const reference to the GeneratorCalibration struct to be saved.
 * @return True if the calibration was saved successfully, false otherwise.
 */
bool saveGeneratorCalibration(const GeneratorCalibration& cal);

/**
 * @brief Resets the generator calibration settings in NVS to their default values.
 *
 * This function is useful for restoring the system to a known state.
 */
void resetGeneratorCalibrationToDefaults();