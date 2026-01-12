/*
 * Storage Utilities Header
 * SD card formatting and directory management
 */

#ifndef STORAGE_UTILS_H
#define STORAGE_UTILS_H

// SD card formatting
void formatSDCard();

// Directory management
bool deleteDirectory(const char* path, int depth = 0);

#endif // STORAGE_UTILS_H

