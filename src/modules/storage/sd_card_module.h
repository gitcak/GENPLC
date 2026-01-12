#ifndef SD_CARD_MODULE_H
#define SD_CARD_MODULE_H

#include <Arduino.h>

class SDCardModule {
private:
    bool mounted;
    bool autoFormat();

public:
    SDCardModule();

    // Detect availability; M5StamPLC mounts SD when enabled in config
    bool begin();
    bool isMounted() const { return mounted; }

    // Filesystem info (bytes); return 0 if unavailable
    uint64_t totalBytes() const;
    uint64_t usedBytes() const;

    // File helpers
    bool exists(const char* path) const;
    bool writeText(const char* path, const char* text, bool append = true);
    bool readText(const char* path, char* buffer, size_t bufferSize, size_t maxBytes = 4096) const;
    
    // Diagnostics
    bool testWrite() const;
    void printStatus() const;
};

#endif // SD_CARD_MODULE_H


