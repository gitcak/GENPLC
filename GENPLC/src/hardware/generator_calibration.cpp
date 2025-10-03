#include "generator_calibration.h"
#include <Preferences.h>

#define GEN_CALIB_NAMESPACE "gen_calib"

/**
 * @brief Gets the default calibration settings for the generator.
 *
 * This function provides a baseline linear calibration where sensor values from 0 to 1023
 * are mapped linearly to percentages from 0% to 100%. It also sets default service intervals.
 *
 * @return A GeneratorCalibration struct populated with default values.
 */
GeneratorCalibration GeneratorCalibration::getDefaults() {
    GeneratorCalibration cal;

    // Default calibration: linear 0-1023 mapping
    // 0%, 25%, 50%, 75%, 100% correspond to 0, 255, 511, 767, 1023
    for (int i = 0; i < 5; i++) {
        uint16_t val = (i * 1023) / 4;
        cal.fuelLevelCal[i] = val;
        cal.fuelFilterCal[i] = val;
        cal.oilLevelCal[i] = val;
        cal.oilFilterCal[i] = val;
    }

    // Default service intervals
    cal.fuelFilterIntervalHours = 500;  // 500 hours
    cal.oilFilterIntervalHours = 200;   // 200 hours
    cal.oilChangeIntervalHours = 100;   // 100 hours

    return cal;
}

/**
 * @brief Calibrates a raw sensor value using a 5-point calibration table.
 *
 * This function performs linear interpolation between the provided calibration points
 * to convert a raw sensor reading into a percentage (0-100).
 *
 * @param rawValue The raw sensor value to be calibrated.
 * @param calPoints An array of 5 calibration points representing 0%, 25%, 50%, 75%, and 100%.
 * @return The calibrated value as a percentage (0-100).
 */
uint16_t GeneratorCalibration::calibrateSensor(uint16_t rawValue, const uint16_t calPoints[5]) const {
    // Clamp raw value to valid range
    rawValue = constrain(rawValue, calPoints[0], calPoints[4]);

    // Find which calibration segment we're in
    for (int i = 0; i < 4; i++) {
        if (rawValue >= calPoints[i] && rawValue <= calPoints[i + 1]) {
            // Linear interpolation between calibration points
            uint16_t rawRange = calPoints[i + 1] - calPoints[i];
            uint16_t percentRange = 25; // Each segment is 25%
            uint16_t rawOffset = rawValue - calPoints[i];

            if (rawRange == 0) {
                return i * 25; // Avoid division by zero
            }

            return (i * 25) + (rawOffset * percentRange) / rawRange;
        }
    }

    // Fallback (should not reach here)
    return 50;
}

/**
 * @brief Retrieves the generator calibration settings from Non-Volatile Storage (NVS).
 *
 * If no settings are found in NVS, it loads and returns the default calibration settings.
 *
 * @return A GeneratorCalibration struct with the loaded or default settings.
 */
GeneratorCalibration getGeneratorCalibration() {
    Preferences prefs;
    GeneratorCalibration cal = GeneratorCalibration::getDefaults();

    if (prefs.begin(GEN_CALIB_NAMESPACE, true)) { // Read-only mode
        // Load calibration points
        for (int i = 0; i < 5; i++) {
            cal.fuelLevelCal[i] = prefs.getUShort(("fuel" + String(i)).c_str(), cal.fuelLevelCal[i]);
            cal.fuelFilterCal[i] = prefs.getUShort(("fuelF" + String(i)).c_str(), cal.fuelFilterCal[i]);
            cal.oilLevelCal[i] = prefs.getUShort(("oil" + String(i)).c_str(), cal.oilLevelCal[i]);
            cal.oilFilterCal[i] = prefs.getUShort(("oilF" + String(i)).c_str(), cal.oilFilterCal[i]);
        }

        // Load service intervals
        cal.fuelFilterIntervalHours = prefs.getULong("fuelFInt", cal.fuelFilterIntervalHours);
        cal.oilFilterIntervalHours = prefs.getULong("oilFInt", cal.oilFilterIntervalHours);
        cal.oilChangeIntervalHours = prefs.getULong("oilCInt", cal.oilChangeIntervalHours);

        prefs.end();
    }

    return cal;
}

/**
 * @brief Saves the provided generator calibration settings to NVS.
 *
 * @param cal A const reference to the GeneratorCalibration struct containing the settings to save.
 * @return True if the settings were saved successfully, false on failure.
 */
bool saveGeneratorCalibration(const GeneratorCalibration& cal) {
    Preferences prefs;

    if (!prefs.begin(GEN_CALIB_NAMESPACE, false)) { // Read-write mode
        return false;
    }

    // Save calibration points
    for (int i = 0; i < 5; i++) {
        prefs.putUShort(("fuel" + String(i)).c_str(), cal.fuelLevelCal[i]);
        prefs.putUShort(("fuelF" + String(i)).c_str(), cal.fuelFilterCal[i]);
        prefs.putUShort(("oil" + String(i)).c_str(), cal.oilLevelCal[i]);
        prefs.putUShort(("oilF" + String(i)).c_str(), cal.oilFilterCal[i]);
    }

    // Save service intervals
    prefs.putULong("fuelFInt", cal.fuelFilterIntervalHours);
    prefs.putULong("oilFInt", cal.oilFilterIntervalHours);
    prefs.putULong("oilCInt", cal.oilChangeIntervalHours);

    prefs.end();
    return true;
}

/**
 * @brief Resets the generator calibration settings in NVS to their default values.
 *
 * This function fetches the default settings and saves them to NVS, overwriting any
 * existing custom calibration.
 */
void resetGeneratorCalibrationToDefaults() {
    GeneratorCalibration cal = GeneratorCalibration::getDefaults();
    saveGeneratorCalibration(cal);
}