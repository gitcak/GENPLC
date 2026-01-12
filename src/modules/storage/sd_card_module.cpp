#include "sd_card_module.h"
#include <SD.h>
#include <SPI.h>

SDCardModule::SDCardModule()
    : mounted(false) {}

bool SDCardModule::begin() {
    // Check if SD already mounted by M5StamPLC or other system
    if (SD.cardType() != CARD_NONE) {
        mounted = true;
        Serial.println("SDCardModule: SD already mounted by system");
        
        // Test write access first before trying to create directories
        File testWrite = SD.open("/sd_test.tmp", "w");
        bool canWrite = false;
        if (testWrite) {
            testWrite.close();
            SD.remove("/sd_test.tmp");
            canWrite = true;
        }
        
        // Only try to create directories if we have write access
        if (canWrite) {
            // Ensure directory structure exists even on pre-mounted cards
            if (!SD.exists("/logs") && !SD.mkdir("/logs")) {
                Serial.println("SDCardModule: Warning - /logs missing and creation failed");
            }
            if (!SD.exists("/data") && !SD.mkdir("/data")) {
                Serial.println("SDCardModule: Warning - /data missing and creation failed");
            }
            if (!SD.exists("/config") && !SD.mkdir("/config")) {
                Serial.println("SDCardModule: Warning - /config missing and creation failed");
            }
        } else {
            Serial.println("SDCardModule: Read-only card detected (pre-mounted)");
        }
        return true;
    }

    // For StampPLC, SD card uses dedicated SPI bus to avoid GPIO conflicts
    // Use correct StampPLC SD card pins from pin_config.h
    const int SD_MOSI = 8;   // STAMPLC_PIN_SD_MOSI
    const int SD_MISO = 9;   // STAMPLC_PIN_SD_MISO
    const int SD_SCK = 7;    // STAMPLC_PIN_SD_SCK
    const int SD_CS = 10;    // STAMPLC_PIN_SD_CS

    Serial.printf("SDCardModule: Attempting SD card init on pins CS=%d, MOSI=%d, MISO=%d, SCK=%d\n",
                  SD_CS, SD_MOSI, SD_MISO, SD_SCK);

    // Initialize SPI bus explicitly
    SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
    delay(100); // Allow SPI bus to stabilize

    // Attempt to mount with explicit SPI configuration
    mounted = SD.begin(SD_CS, SPI, 4000000); // 4MHz SPI speed

    if (!mounted) {
        Serial.println("SDCardModule: 4MHz failed, trying 1MHz...");
        // Try slower speed as fallback
        mounted = SD.begin(SD_CS, SPI, 1000000); // 1MHz SPI speed

        if (!mounted) {
            Serial.println("SDCardModule: 1MHz failed, trying 400kHz...");
            // Try even slower speed
            mounted = SD.begin(SD_CS, SPI, 400000); // 400kHz SPI speed

            if (!mounted) {
                Serial.println("SDCardModule: All SPI speeds failed, checking card presence...");
                // Final attempt: try opening root directory as presence check
                File f = SD.open("/");
                mounted = (bool)f;
                if (f) {
                    f.close();
                    Serial.println("SDCardModule: Root directory accessible but mount failed");
                }
            }
        }
    }


    if (mounted) {
        Serial.printf("SDCardModule: SD card mounted successfully (Type: %d)\n", SD.cardType());

        auto ensureDirectory = [](const char* path) -> bool {
            if (SD.exists(path)) return true;
            if (SD.mkdir(path)) return true;
            Serial.printf("SDCardModule: Failed to create %s directory\n", path);
            return false;
        };

        // Check if SD card needs initialization (first boot)
        // Use SD.exists() first to avoid VFS errors on read-only mounts
        bool needsFormat = false;
        bool hasWriteAccess = true;
        
        // Test write access before checking format marker
        File testWriteFile = SD.open("/test_write.tmp", "w");
        if (testWriteFile) {
            testWriteFile.close();
            SD.remove("/test_write.tmp");
            hasWriteAccess = true;
        } else {
            hasWriteAccess = false;
            Serial.println("SDCardModule: WARNING - SD card appears read-only (no write access)");
        }
        
        if (hasWriteAccess) {
            // Check for format marker file - use exists() first to avoid VFS errors
            // The VFS may prefix paths with /sd/, so check both locations
            bool markerExists = false;
            const char* markerPaths[] = {"/format_marker.txt", "/sd/format_marker.txt"};
            for (int i = 0; i < 2; i++) {
                if (SD.exists(markerPaths[i])) {
                    markerExists = true;
                    File formatMarker = SD.open(markerPaths[i], "r");
                    if (formatMarker) {
                        Serial.printf("SDCardModule: Format marker found at %s\n", markerPaths[i]);
                        formatMarker.close();
                    }
                    break;
                }
            }
            
            if (!markerExists) {
                // No marker file - check if card is empty or needs initialization
                File root = SD.open("/");
                if (!root) {
                    needsFormat = true;
                    Serial.println("SDCardModule: Root directory inaccessible - format needed");
                } else {
                    // Check for any expected directories
                    File logDir = SD.open("/logs");
                    File dataDir = SD.open("/data");
                    if (!logDir && !dataDir) {
                        needsFormat = true;
                        Serial.println("SDCardModule: No system directories found - format needed");
                    }
                    if (logDir) logDir.close();
                    if (dataDir) dataDir.close();
                    root.close();
                }
            }

            if (needsFormat) {
                Serial.println("SDCardModule: Auto-formatting SD card...");
                if (autoFormat()) {
                    Serial.println("SDCardModule: Auto-format successful");
                } else {
                    Serial.println("SDCardModule: Auto-format failed - continuing with read-only access");
                    hasWriteAccess = false; // Fall back to read-only mode
                }
            }
        } else {
            // Read-only card - just check if directories exist
            Serial.println("SDCardModule: Read-only SD card detected - skipping format marker check");
        }

        // Ensure required directories exist (handles cases where marker exists but dirs removed)
        // Only try to create directories if we have write access
        if (hasWriteAccess) {
            ensureDirectory("/logs");
            ensureDirectory("/data");
            ensureDirectory("/config");

            // Test write capability
            File testFile = SD.open("/test.txt", "w");
            if (testFile) {
                testFile.println("SD card test");
                testFile.close();
                SD.remove("/test.txt");
                Serial.println("SDCardModule: Write test successful");
            } else {
                Serial.println("SDCardModule: WARNING - Write test failed, card may be read-only");
                hasWriteAccess = false;
            }
        } else {
            // Read-only mode - just verify directories are accessible
            Serial.println("SDCardModule: Operating in read-only mode");
        }
    } else {
        Serial.println("SDCardModule: SD card not found or mount failed");
        Serial.println("SDCardModule: Check SD card insertion and power");
    }

    return mounted;
}

uint64_t SDCardModule::totalBytes() const {
    if (!mounted) return 0;
#ifdef ESP32
    return SD.totalBytes();
#else
    return 0;
#endif
}

uint64_t SDCardModule::usedBytes() const {
    if (!mounted) return 0;
#ifdef ESP32
    return SD.usedBytes();
#else
    return 0;
#endif
}

bool SDCardModule::exists(const char* path) const {
    if (!mounted) return false;
    return SD.exists(path);
}

bool SDCardModule::writeText(const char* path, const char* text, bool append) {
    if (!mounted || !text) return false;
    const char* mode = append ? "a" : "w";
    File file = SD.open(path, mode);
    if (!file) return false;
    size_t written = file.print(text);
    file.close();
    return written == strlen(text);
}

bool SDCardModule::readText(const char* path, char* buffer, size_t bufferSize, size_t maxBytes) const {
    if (!mounted || !buffer || bufferSize == 0) return false;

    File file = SD.open(path, "r");
    if (!file) return false;

    size_t bytesRead = 0;
    size_t maxRead = min(maxBytes, bufferSize - 1);

    while (file.available() && bytesRead < maxRead) {
        buffer[bytesRead++] = (char)file.read();
    }

    buffer[bytesRead] = '\0';
    file.close();
    return true;
}

bool SDCardModule::testWrite() const {
    if (!mounted) return false;

    const char* testPath = "/sd_test.tmp";
    const char* testData = "SD card write test";

    // Test write
    File file = SD.open(testPath, "w");
    if (!file) return false;

    size_t written = file.print(testData);
    file.close();

    if (written != strlen(testData)) {
        SD.remove(testPath);
        return false;
    }

    // Test read
    file = SD.open(testPath, "r");
    if (!file) {
        SD.remove(testPath);
        return false;
    }

    char buffer[32];
    size_t read = file.readBytes(buffer, sizeof(buffer) - 1);
    file.close();
    SD.remove(testPath);

    return (read == strlen(testData) && strncmp(buffer, testData, strlen(testData)) == 0);
}

void SDCardModule::printStatus() const {
    if (!mounted) {
        Serial.println("SD Card: Not mounted");
        return;
    }

    Serial.printf("SD Card: Mounted (Type: %d)\n", SD.cardType());
    Serial.printf("SD Card: Total: %llu bytes, Used: %llu bytes\n", totalBytes(), usedBytes());
    Serial.printf("SD Card: Write test: %s\n", testWrite() ? "PASS" : "FAIL");
}

bool SDCardModule::autoFormat() {
    if (!mounted) return false;
    
    Serial.println("SDCardModule: Starting auto-format...");
    
    // Create standard directories
    if (!SD.mkdir("/logs")) {
        Serial.println("SDCardModule: Warning - failed to create /logs directory");
    }
    if (!SD.mkdir("/data")) {
        Serial.println("SDCardModule: Warning - failed to create /data directory");
    }
    if (!SD.mkdir("/config")) {
        Serial.println("SDCardModule: Warning - failed to create /config directory");
    }
    
    // Create format marker
    File marker = SD.open("/format_marker.txt", "w");
    if (marker) {
        marker.println("SD card auto-formatted on first boot");
        marker.printf("Timestamp: %lu ms\n", millis());
        marker.printf("Method: Auto-format on initialization\n");
        marker.close();
        Serial.println("SDCardModule: Format marker created");
        return true;
    }
    
    Serial.println("SDCardModule: Failed to create format marker");
    return false;
}


