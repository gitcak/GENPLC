/*
 * Advanced Error Handling System
 * Centralized error management with recovery strategies
 */

#ifndef ERROR_HANDLER_H
#define ERROR_HANDLER_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "config/system_config.h"

// ============================================================================
// ERROR SEVERITY LEVELS
// ============================================================================
enum class ErrorSeverity {
    INFO = 0,
    WARNING = 1,
    ERROR = 2,
    CRITICAL = 3,
    FATAL = 4
};

// ============================================================================
// ERROR CATEGORIES
// ============================================================================
enum class ErrorCategory {
    SYSTEM = 0,
    HARDWARE = 1,
    COMMUNICATION = 2,
    MEMORY = 3,
    NETWORK = 4,
    GNSS = 5,
    CELLULAR = 6,
    STORAGE = 8,
    POWER = 9
};

// ============================================================================
// ERROR RECOVERY ACTIONS
// ============================================================================
#include "recovery_actions.h"

// ============================================================================
// ERROR EVENT STRUCTURE
// ============================================================================
struct ErrorEvent {
    uint32_t timestamp;
    ErrorCode code;
    ErrorSeverity severity;
    ErrorCategory category;
    TaskHandle_t taskHandle;
    char description[128];
    char context[256];
    uint32_t count;
    uint32_t firstOccurrence;
    RecoveryAction recommendedAction;
    bool recoveryAttempted;
    bool recoverySuccessful;
};

// ============================================================================
// ERROR HANDLER CLASS
// ============================================================================
class ErrorHandler {
private:
    static ErrorHandler* instance;
    
    // Error storage
    ErrorEvent errorLog[50]; // Store last 50 error events
    uint8_t errorCount;
    uint8_t currentIndex;
    
    // Statistics
    uint32_t totalErrors;
    uint32_t criticalErrors;
    uint32_t recoveryAttempts;
    uint32_t successfulRecoveries;
    
    // Mutex for thread safety
    SemaphoreHandle_t errorMutex;
    
    // Error recovery functions
    bool recoverFromHardwareError(const ErrorEvent& error);
    bool recoverFromCommunicationError(const ErrorEvent& error);
    bool recoverFromMemoryError(const ErrorEvent& error);
    bool recoverFromNetworkError(const ErrorEvent& error);
    bool recoverFromSystemError(const ErrorEvent& error);
    
    // Internal helper methods
    const char* severityToString(ErrorSeverity severity) const;
    const char* categoryToString(ErrorCategory category) const;
    const char* recoveryActionToString(RecoveryAction action) const;
    void logError(const ErrorEvent& error);
    void updateStatistics(const ErrorEvent& error);
    bool canAttemptRecovery(const ErrorEvent& error) const;
    
    ErrorHandler();
    
public:
    // Singleton access
    static ErrorHandler* getInstance();
    
    // Error reporting
    void reportError(ErrorCode code, ErrorSeverity severity, ErrorCategory category,
                    const char* description, const char* context = nullptr);
    void reportErrorWithTask(ErrorCode code, ErrorSeverity severity, ErrorCategory category,
                           const char* description, TaskHandle_t task, const char* context = nullptr);
    
    // Error recovery
    bool attemptRecovery(ErrorEvent& error);
    bool attemptAutomaticRecovery(ErrorCode code, ErrorCategory category);
    
    // Error querying
    ErrorEvent* getLastError();
    ErrorEvent* getErrorByCode(ErrorCode code);
    uint8_t getErrorCount() const { return errorCount; }
    uint32_t getTotalErrors() const { return totalErrors; }
    uint32_t getCriticalErrors() const { return criticalErrors; }
    uint32_t getRecoverySuccessRate() const;
    
    // Error management
    void clearErrors();
    void clearErrorsByCategory(ErrorCategory category);
    void clearOldErrors(uint32_t olderThanMs);
    
    // Diagnostics
    void printErrorLog() const;
    void printStatistics() const;
    void dumpErrorToFile(const char* filename) const;
    
    // System health
    bool isSystemHealthy() const;
    ErrorSeverity getHighestSeverity() const;
    ErrorCategory getMostProblematicCategory() const;
    
    // Destructor
    ~ErrorHandler();
};

// ============================================================================
// ERROR REPORTING MACROS
// ============================================================================
#define REPORT_ERROR(code, severity, category, desc) \
    ErrorHandler::getInstance()->reportError(code, severity, category, desc, __FUNCTION__)

#define REPORT_ERROR_CONTEXT(code, severity, category, desc, context) \
    ErrorHandler::getInstance()->reportError(code, severity, category, desc, context)

#define REPORT_WARNING(category, desc) \
    ErrorHandler::getInstance()->reportError(ERROR_NONE, ErrorSeverity::WARNING, category, desc, __FUNCTION__)

#define REPORT_CRITICAL(category, desc) \
    ErrorHandler::getInstance()->reportError(ERROR_NONE, ErrorSeverity::CRITICAL, category, desc, __FUNCTION__)

#define REPORT_FATAL(category, desc) \
    ErrorHandler::getInstance()->reportError(ERROR_NONE, ErrorSeverity::FATAL, category, desc, __FUNCTION__)

#define ATTEMPT_RECOVERY(code, category) \
    ErrorHandler::getInstance()->attemptAutomaticRecovery(code, category)

// ============================================================================
// ERROR GUARD CLASS (RAII for error reporting)
// ============================================================================
class ErrorGuard {
private:
    ErrorCode expectedError;
    ErrorCategory category;
    bool errorOccurred;
    
public:
    ErrorGuard(ErrorCode err, ErrorCategory cat) : expectedError(err), category(cat), errorOccurred(false) {}
    
    ~ErrorGuard() {
        if (!errorOccurred) {
            ErrorHandler::getInstance()->reportError(
                expectedError, ErrorSeverity::WARNING, category,
                "Expected error did not occur within scope", __FUNCTION__
            );
        }
    }
    
    void markError(const char* description) {
        errorOccurred = true;
        ErrorHandler::getInstance()->reportError(
            expectedError, ErrorSeverity::ERROR, category, description, __FUNCTION__
        );
    }
    
    bool hasError() const { return errorOccurred; }
};

// ============================================================================
// ERROR RECOVERY STRATEGIES
// ============================================================================
namespace RecoveryStrategies {
    // Hardware recovery
    bool resetUART(int uartNum);
    bool resetI2C();
    bool reinitializeSensor(const char* sensorName);
    
    // Communication recovery
    bool reconnectCellular();
    bool resetNetworkStack();
    
    // Memory recovery
    bool defragmentMemory();
    bool restartTask(TaskHandle_t task);
    bool clearAllocations();
    
    // System recovery
    bool enterSafeMode();
    bool performSoftReset();
    bool scheduleReboot(uint32_t delayMs);
}

#endif // ERROR_HANDLER_H
