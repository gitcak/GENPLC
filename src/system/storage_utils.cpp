/*
 * Storage Utilities Implementation
 * SD card formatting and directory management
 */

#include "storage_utils.h"
#include "../modules/storage/sd_card_module.h"
#include "../modules/logging/log_buffer.h"
#include <SD.h>
#include <FS.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// External globals
extern SDCardModule* sdModule;

void formatSDCard() {
    if (!sdModule || !sdModule->isMounted()) {
        Serial.println("SD card not mounted, cannot format");
        return;
    }
    
    Serial.println("Starting SD card format...");
    
    // Unmount SD card first
    SD.end();
    
    // Wait for unmount to complete
    vTaskDelay(pdMS_TO_TICKS(500));
    
    bool formatSuccess = false;
    
    // Use file deletion approach for formatting
    Serial.println("Using file deletion approach for formatting...");
    
    // Try to remount and delete all files (basic format)
    if (SD.begin()) {
        Serial.println("Erasing all files...");
        
        // Delete all files and directories
        bool allDeleted = true;
        File root = SD.open("/");
        if (root) {
            File file = root.openNextFile();
            while (file) {
                String fileName = file.name();
                bool isDirectory = file.isDirectory();
                file.close();
                
                if (isDirectory) {
                    // Recursively delete directory contents
                    String fullPath = "/" + fileName;
                    if (!deleteDirectory(fullPath.c_str(), 0)) {
                        allDeleted = false;
                        Serial.printf("Failed to delete directory: %s\n", fileName.c_str());
                    }
                } else {
                    // Delete file
                    if (!SD.remove("/" + fileName)) {
                        allDeleted = false;
                        Serial.printf("Failed to delete file: %s\n", fileName.c_str());
                    }
                }
                file = root.openNextFile();
            }
            root.close();
        }
        
        if (allDeleted) {
            formatSuccess = true;
            Serial.println("File deletion completed");
        }
    }
    
    // Create basic directory structure and format marker
    if (formatSuccess && SD.begin()) {
        SD.mkdir("/data");
        SD.mkdir("/logs");
        SD.mkdir("/config");
        
        // Create a format marker file
        File formatFile = SD.open("/format_marker.txt", FILE_WRITE);
        if (formatFile) {
            formatFile.println("SD card formatted on: ");
            formatFile.printf("Timestamp: %lu ms\n", millis());
            formatFile.printf("Method: File deletion\n");
            formatFile.close();
            Serial.println("Format marker created");
        }
        SD.end();
    }
    
    // Reinitialize SD module
    if (sdModule) {
        delete sdModule;
        sdModule = nullptr;
    }
    
#if ENABLE_SD
    sdModule = new SDCardModule();
    if (sdModule->begin()) {
        Serial.println("SD card remounted after format");
        if (formatSuccess) {
            log_add("SD card formatted successfully");
        } else {
            log_add("SD card format completed with warnings");
        }
    } else {
        Serial.println("Failed to remount SD card after format");
        delete sdModule;
        sdModule = nullptr;
        log_add("SD card format failed - remount failed");
    }
#endif
}

bool deleteDirectory(const char* path, int depth) {
    // Safety: prevent infinite recursion
    if (depth > 10) {
        Serial.printf("deleteDirectory: Maximum depth exceeded for %s\n", path);
        return false;
    }
    
    File dir = SD.open(path);
    if (!dir) return false;
    
    bool success = true;
    File file = dir.openNextFile();
    while (file && success) {
        String fileName = file.name();
        bool isDir = file.isDirectory();
        file.close();
        
        String fullPath = String(path) + "/" + fileName;
        
        if (isDir) {
            success = deleteDirectory(fullPath.c_str(), depth + 1);
        } else {
            success = SD.remove(fullPath.c_str());
        }
        
        file = dir.openNextFile();
    }
    dir.close();
    
    // Remove the directory itself if it's empty
    if (success) {
        success = SD.rmdir(path);
    }
    
    return success;
}

