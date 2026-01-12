/*
 * Error Handler Implementation
 * Minimal stub implementation for crash recovery system
 */

#include "../include/error_handler.h"
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// ============================================================================
// GLOBAL INSTANCE
// ============================================================================
ErrorHandler* ErrorHandler::instance = nullptr;

// ============================================================================
// CONSTRUCTOR/DESTRUCTOR
// ============================================================================
ErrorHandler::ErrorHandler() {
    errorCount = 0;
    currentIndex = 0;
    totalErrors = 0;
    criticalErrors = 0;
    recoveryAttempts = 0;
    successfulRecoveries = 0;
    errorMutex = xSemaphoreCreateMutex();
    memset(errorLog, 0, sizeof(errorLog));
}

ErrorHandler::~ErrorHandler() {
    if (errorMutex) {
        vSemaphoreDelete(errorMutex);
        errorMutex = nullptr;
    }
}

// ============================================================================
// SINGLETON ACCESS
// ============================================================================
ErrorHandler* ErrorHandler::getInstance() {
    if (!instance) {
        instance = new ErrorHandler();
    }
    return instance;
}

// ============================================================================
// ERROR REPORTING
// ============================================================================
void ErrorHandler::reportError(ErrorCode code, ErrorSeverity severity, ErrorCategory category,
                              const char* description, const char* context) {
    // Simple logging implementation - just log to Serial
    const char* severityStr = severityToString(severity);
    const char* categoryStr = categoryToString(category);
    
    Serial.printf("[ERROR] %s/%s: %s", severityStr, categoryStr, description);
    if (context) {
        Serial.printf(" (Context: %s)", context);
    }
    Serial.println();
    
    // Update statistics
    totalErrors++;
    if (severity == ErrorSeverity::CRITICAL || severity == ErrorSeverity::FATAL) {
        criticalErrors++;
    }
    
    // Store in log (simple circular buffer)
    if (errorMutex && xSemaphoreTake(errorMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        ErrorEvent& event = errorLog[currentIndex];
        event.timestamp = millis();
        event.code = code;
        event.severity = severity;
        event.category = category;
        event.taskHandle = xTaskGetCurrentTaskHandle();
        strncpy(event.description, description, sizeof(event.description) - 1);
        event.description[sizeof(event.description) - 1] = '\0';
        if (context) {
            strncpy(event.context, context, sizeof(event.context) - 1);
            event.context[sizeof(event.context) - 1] = '\0';
        } else {
            event.context[0] = '\0';
        }
        event.count = 1;
        event.firstOccurrence = event.timestamp;
        event.recoveryAttempted = false;
        event.recoverySuccessful = false;
        
        currentIndex = (currentIndex + 1) % 50;
        if (errorCount < 50) {
            errorCount++;
        }
        
        xSemaphoreGive(errorMutex);
    }
}

void ErrorHandler::reportErrorWithTask(ErrorCode code, ErrorSeverity severity, ErrorCategory category,
                                     const char* description, TaskHandle_t task, const char* context) {
    reportError(code, severity, category, description, context);
}

// ============================================================================
// ERROR RECOVERY
// ============================================================================
bool ErrorHandler::attemptRecovery(ErrorEvent& error) {
    recoveryAttempts++;
    // Stub implementation - just return false for now
    return false;
}

bool ErrorHandler::attemptAutomaticRecovery(ErrorCode code, ErrorCategory category) {
    recoveryAttempts++;
    // Stub implementation - just return false for now
    return false;
}

// ============================================================================
// ERROR QUERYING
// ============================================================================
ErrorEvent* ErrorHandler::getLastError() {
    if (errorCount == 0) return nullptr;
    uint8_t lastIndex = (currentIndex == 0) ? 49 : (currentIndex - 1);
    return &errorLog[lastIndex];
}

ErrorEvent* ErrorHandler::getErrorByCode(ErrorCode code) {
    for (uint8_t i = 0; i < errorCount; i++) {
        if (errorLog[i].code == code) {
            return &errorLog[i];
        }
    }
    return nullptr;
}

uint32_t ErrorHandler::getRecoverySuccessRate() const {
    if (recoveryAttempts == 0) return 0;
    return (successfulRecoveries * 100) / recoveryAttempts;
}

// ============================================================================
// ERROR MANAGEMENT
// ============================================================================
void ErrorHandler::clearErrors() {
    if (errorMutex && xSemaphoreTake(errorMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        errorCount = 0;
        currentIndex = 0;
        memset(errorLog, 0, sizeof(errorLog));
        xSemaphoreGive(errorMutex);
    }
}

void ErrorHandler::clearErrorsByCategory(ErrorCategory category) {
    // Stub implementation
}

void ErrorHandler::clearOldErrors(uint32_t olderThanMs) {
    // Stub implementation
}

// ============================================================================
// DIAGNOSTICS
// ============================================================================
void ErrorHandler::printErrorLog() const {
    Serial.println("\n=== Error Log ===");
    for (uint8_t i = 0; i < errorCount; i++) {
        const ErrorEvent& event = errorLog[i];
        Serial.printf("[%lu] %s/%s: %s\n", 
                     event.timestamp, 
                     severityToString(event.severity),
                     categoryToString(event.category),
                     event.description);
    }
    Serial.println("================\n");
}

void ErrorHandler::printStatistics() const {
    Serial.println("\n=== Error Statistics ===");
    Serial.printf("Total Errors: %lu\n", totalErrors);
    Serial.printf("Critical Errors: %lu\n", criticalErrors);
    Serial.printf("Recovery Attempts: %lu\n", recoveryAttempts);
    Serial.printf("Successful Recoveries: %lu\n", successfulRecoveries);
    Serial.println("========================\n");
}

void ErrorHandler::dumpErrorToFile(const char* filename) const {
    // Stub implementation
    Serial.printf("ErrorHandler: Dump to file not implemented: %s\n", filename);
}

// ============================================================================
// SYSTEM HEALTH
// ============================================================================
bool ErrorHandler::isSystemHealthy() const {
    return (criticalErrors == 0);
}

ErrorSeverity ErrorHandler::getHighestSeverity() const {
    ErrorSeverity highest = ErrorSeverity::INFO;
    for (uint8_t i = 0; i < errorCount; i++) {
        if (errorLog[i].severity > highest) {
            highest = errorLog[i].severity;
        }
    }
    return highest;
}

ErrorCategory ErrorHandler::getMostProblematicCategory() const {
    // Stub implementation
    return ErrorCategory::SYSTEM;
}

// ============================================================================
// PRIVATE HELPER METHODS
// ============================================================================
const char* ErrorHandler::severityToString(ErrorSeverity severity) const {
    switch (severity) {
        case ErrorSeverity::INFO: return "INFO";
        case ErrorSeverity::WARNING: return "WARNING";
        case ErrorSeverity::ERROR: return "ERROR";
        case ErrorSeverity::CRITICAL: return "CRITICAL";
        case ErrorSeverity::FATAL: return "FATAL";
        default: return "UNKNOWN";
    }
}

const char* ErrorHandler::categoryToString(ErrorCategory category) const {
    switch (category) {
        case ErrorCategory::SYSTEM: return "SYSTEM";
        case ErrorCategory::HARDWARE: return "HARDWARE";
        case ErrorCategory::COMMUNICATION: return "COMMUNICATION";
        case ErrorCategory::MEMORY: return "MEMORY";
        case ErrorCategory::NETWORK: return "NETWORK";
        case ErrorCategory::GNSS: return "GNSS";
        case ErrorCategory::CELLULAR: return "CELLULAR";
        case ErrorCategory::MQTT: return "MQTT";
        case ErrorCategory::STORAGE: return "STORAGE";
        case ErrorCategory::POWER: return "POWER";
        default: return "UNKNOWN";
    }
}

const char* ErrorHandler::recoveryActionToString(RecoveryAction action) const {
    // Stub implementation
    return "NONE";
}

void ErrorHandler::logError(const ErrorEvent& error) {
    // Already handled in reportError
}

void ErrorHandler::updateStatistics(const ErrorEvent& error) {
    // Already handled in reportError
}

bool ErrorHandler::canAttemptRecovery(const ErrorEvent& error) const {
    // Stub implementation
    return false;
}

bool ErrorHandler::recoverFromHardwareError(const ErrorEvent& error) {
    return false;
}

bool ErrorHandler::recoverFromCommunicationError(const ErrorEvent& error) {
    return false;
}

bool ErrorHandler::recoverFromMemoryError(const ErrorEvent& error) {
    return false;
}

bool ErrorHandler::recoverFromNetworkError(const ErrorEvent& error) {
    return false;
}

bool ErrorHandler::recoverFromSystemError(const ErrorEvent& error) {
    return false;
}

