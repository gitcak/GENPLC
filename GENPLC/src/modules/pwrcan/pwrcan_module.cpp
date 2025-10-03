#include "pwrcan_module.h"

PWRCANModule::PWRCANModule()
    : isInitializedFlag(false), isStartedFlag(false), errorCount(0),
      framesReceived(0), framesTransmitted(0), busLoadPercent(0),
      lastErrorCode(0), lastBusActivityTime(0) {}

PWRCANModule::~PWRCANModule() {
    end();
}

bool PWRCANModule::begin(int txPin, int rxPin, int bitrateKbps) {
#if defined(ARDUINO_ARCH_ESP32)
    if (isInitializedFlag) {
        return true;
    }

    // General config
    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT((gpio_num_t)txPin, (gpio_num_t)rxPin, TWAI_MODE_NORMAL);
    g_config.tx_queue_len = 10;
    g_config.rx_queue_len = 10;

    // Timing config
    twai_timing_config_t t_config = selectTimingConfig(bitrateKbps);

    // Filter: accept all
    twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    if (twai_driver_install(&g_config, &t_config, &f_config) != ESP_OK) {
        errorCount++;
        return false;
    }
    if (twai_start() != ESP_OK) {
        errorCount++;
        twai_driver_uninstall();
        return false;
    }

    isInitializedFlag = true;
    isStartedFlag = true;
    return true;
#else
    (void)txPin; (void)rxPin; (void)bitrateKbps;
    return false;
#endif
}

void PWRCANModule::end() {
#if defined(ARDUINO_ARCH_ESP32)
    if (isStartedFlag) {
        twai_stop();
        isStartedFlag = false;
    }
    if (isInitializedFlag) {
        twai_driver_uninstall();
        isInitializedFlag = false;
    }
#endif
}

bool PWRCANModule::sendFrame(uint32_t identifier,
                             const uint8_t *payload,
                             uint8_t length,
                             bool isExtendedId,
                             bool isRemoteRequest,
                             uint32_t timeoutMs) {
#if defined(ARDUINO_ARCH_ESP32)
    if (!isStartedFlag || payload == nullptr || length > 8) {
        errorCount++;
        lastErrorCode = 3; // Invalid parameters
        return false;
    }
    twai_message_t msg = {};
    msg.identifier = identifier;
    msg.data_length_code = length;
    msg.flags = 0;
    if (isExtendedId) msg.extd = 1;
    if (isRemoteRequest) msg.rtr = 1;
    for (uint8_t i = 0; i < length; ++i) msg.data[i] = payload[i];
    esp_err_t res = twai_transmit(&msg, pdMS_TO_TICKS(timeoutMs));
    if (res != ESP_OK) {
        errorCount++;
        lastErrorCode = 3; // Send timeout/failed
        return false;
    }

    // Success - update statistics
    framesTransmitted++;
    lastBusActivityTime = millis();

    return true;
#else
    (void)identifier; (void)payload; (void)length; (void)isExtendedId; (void)isRemoteRequest; (void)timeoutMs;
    return false;
#endif
}

bool PWRCANModule::receiveFrame(uint32_t &identifier,
                                uint8_t *payload,
                                uint8_t &length,
                                bool &isExtendedId,
                                bool &isRemoteRequest,
                                uint32_t timeoutMs) {
#if defined(ARDUINO_ARCH_ESP32)
    if (!isStartedFlag || payload == nullptr) {
        return false;
    }
    twai_message_t msg = {};
    esp_err_t res = twai_receive(&msg, timeoutMs == 0 ? 0 : pdMS_TO_TICKS(timeoutMs));
    if (res != ESP_OK) {
        return false;
    }
    identifier = msg.identifier;
    length = msg.data_length_code;
    isExtendedId = msg.extd;
    isRemoteRequest = msg.rtr;
    for (uint8_t i = 0; i < length && i < 8; ++i) payload[i] = msg.data[i];

    // Process received frame for generator protocol
    processReceivedFrame(identifier, payload, length);

    return true;
#else
    (void)identifier; (void)payload; (void)length; (void)isExtendedId; (void)isRemoteRequest; (void)timeoutMs;
    return false;
#endif
}

bool PWRCANModule::processReceivedFrame(uint32_t identifier, const uint8_t* payload, uint8_t length) {
    // Update statistics
    framesReceived++;
    lastBusActivityTime = millis();

    // Pass to generator protocol handler
    return generatorProtocol.processMessage(identifier, payload, length);
}

// Enhanced status methods
String PWRCANModule::getLastErrorDescription() const {
    switch (lastErrorCode) {
        case 0: return "";
        case 1: return "Driver install failed";
        case 2: return "Start failed";
        case 3: return "Send timeout";
        case 4: return "Receive error";
        default: return "Unknown error " + String(lastErrorCode);
    }
}

// Update the getCanStatus method to use the new create signature
CanStatus PWRCANModule::getCanStatus() const {
    return CanStatus::create(*this);
}

#if defined(ARDUINO_ARCH_ESP32)
twai_timing_config_t PWRCANModule::selectTimingConfig(int bitrateKbps) {
    switch (bitrateKbps) {
        case 1000: return TWAI_TIMING_CONFIG_1MBITS();
        case 800:  return TWAI_TIMING_CONFIG_800KBITS();
        case 500:  return TWAI_TIMING_CONFIG_500KBITS();
        case 250:  return TWAI_TIMING_CONFIG_250KBITS();
        case 125:  return TWAI_TIMING_CONFIG_125KBITS();
        case 100:  return TWAI_TIMING_CONFIG_100KBITS();
        case 50:   return TWAI_TIMING_CONFIG_50KBITS();
        case 25:   return TWAI_TIMING_CONFIG_25KBITS();
        default:   return TWAI_TIMING_CONFIG_250KBITS();
    }
}
#endif


