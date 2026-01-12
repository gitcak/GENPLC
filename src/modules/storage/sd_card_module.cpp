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
        
        // Check if SD card needs initialization (first boot)
        bool needsFormat = false;
        File formatMarker = SD.open("/format_marker.txt", FILE_READ);
        
        if (!formatMarker) {
            // No marker file - check if card is empty or corrupted
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
        } else {
            Serial.println("SDCardModule: Format marker found - SD card already initialized");
            formatMarker.close();
        }
        
        if (needsFormat) {
            Serial.println("SDCardModule: Auto-formatting SD card...");
            if (autoFormat()) {
                Serial.println("SDCardModule: Auto-format successful");
            } else {
                Serial.println("SDCardModule: Auto-format failed");
                return false;
            }
        }
        
        // Test write capability
        File testFile = SD.open("/test.txt", FILE_WRITE);
        if (testFile) {
            testFile.println("SD card test");
            testFile.close();
            SD.remove("/test.txt");
            Serial.println("SDCardModule: Write test successful");
        } else {
            Serial.println("SDCardModule: WARNING - Mounted but write test failed");
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
    File file = SD.open(path, append ? FILE_APPEND : FILE_WRITE);
    if (!file) return false;
    size_t written = file.print(text);
    file.close();
    return written == strlen(text);
}

bool SDCardModule::readText(const char* path, char* buffer, size_t bufferSize, size_t maxBytes) const {
    if (!mounted || !buffer || bufferSize == 0) return false;

    File file = SD.open(path, FILE_READ);
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
    File file = SD.open(testPath, FILE_WRITE);
    if (!file) return false;

    size_t written = file.print(testData);
    file.close();

    if (written != strlen(testData)) {
        SD.remove(testPath);
        return false;
    }

    // Test read
    file = SD.open(testPath, FILE_READ);
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
    File marker = SD.open("/format_marker.txt", FILE_WRITE);
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


