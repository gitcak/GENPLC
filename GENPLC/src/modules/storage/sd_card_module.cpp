#include "sd_card_module.h"
#include <FS.h>
#include <SD.h>

SDCardModule::SDCardModule()
    : mounted(false) {}

bool SDCardModule::begin() {
    // Check if SD already mounted by M5StamPLC or other system
    if (SD.cardType() != CARD_NONE) {
        mounted = true;
        Serial.println("SDCardModule: SD already mounted by system");
        return true;
    }
    
    // Suppress noisy SD card error logs during mount attempt
    esp_log_level_set("sd_diskio", ESP_LOG_ERROR); // Only show errors, not warnings
    esp_log_level_set("vfs", ESP_LOG_ERROR); // Suppress VFS errors too
    
    // Attempt to mount with proper error handling
    mounted = SD.begin();
    
    if (!mounted) {
        // Try alternative mount with explicit parameters for StampPLC
        // StampPLC SD typically uses different pins than standard M5Stack
        mounted = SD.begin(SS, SPI, 4000000); // Use default SS pin, standard SPI, 4MHz
        
        if (!mounted) {
            // Final attempt: try opening root directory as presence check
            File f = SD.open("/");
            mounted = (bool)f;
            if (f) f.close();
        }
    }
    
    // Restore normal logging level
    esp_log_level_set("sd_diskio", ESP_LOG_INFO);
    esp_log_level_set("vfs", ESP_LOG_INFO);
    
    if (mounted) {
        Serial.println("SDCardModule: SD card mounted successfully");
    } else {
        Serial.println("SDCardModule: SD card not found or mount failed");
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

bool SDCardModule::writeText(const char* path, const String& text, bool append) {
    if (!mounted) return false;
    File file = SD.open(path, append ? FILE_APPEND : FILE_WRITE, true);
    if (!file) return false;
    size_t written = file.print(text);
    file.close();
    return written == text.length();
}

String SDCardModule::readText(const char* path, size_t maxBytes) const {
    if (!mounted) return String();
    File file = SD.open(path, FILE_READ);
    if (!file) return String();
    String out;
    out.reserve(min<size_t>(maxBytes, 4096));
    while (file.available() && out.length() < maxBytes) {
        out += (char)file.read();
    }
    file.close();
    return out;
}


