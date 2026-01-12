#pragma once

#include <Arduino.h>
#include "generator_status.h"

/**
 * Generator calibration functions
 */

// Load calibration from NVS
GeneratorCalibration getGeneratorCalibration();

// Save calibration to NVS
bool saveGeneratorCalibration(const GeneratorCalibration& cal);

// Reset to default calibration
void resetGeneratorCalibrationToDefaults();
