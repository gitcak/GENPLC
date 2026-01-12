#include <Arduino.h>

// The source for this test lives in `recovery_cellular_test/recovery_cellular_test.ino`.
// PlatformIO does not auto-generate Arduino function prototypes for included `.ino` files,
// so we provide forward declarations here.
bool testModuleCommunication();
void runComprehensiveTest();
void testBasicInfo();
void testSIMStatus();
void testNetworkRegistration();
void testSignalQuality();
void testAPNConfiguration();
void testPDPActivation();
void testPDPActivationSingleAPN();
void testConnectivity();
void checkConnectionStatus();
bool sendATCommand(const char* command, const char* description);
void printHelp();

#include "../recovery_cellular_test/recovery_cellular_test.ino"

