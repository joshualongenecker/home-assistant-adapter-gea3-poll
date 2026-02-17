/*!
 * @file
 * @brief Zigbee Client - provides Client interface for Zigbee communication
 */

#ifndef ZigbeeMqttBridge_h
#define ZigbeeMqttBridge_h

#include <Arduino.h>
#include <Client.h>
#include "esp_zigbee_core.h"

// Maximum message size for Zigbee transmission
#define ZIGBEE_MAX_PAYLOAD_SIZE 82

/**
 * @brief ZigbeeClient implements the Arduino Client interface for Zigbee communication
 * This allows PubSubClient to work over Zigbee instead of WiFi
 */
class ZigbeeClient : public Client {
public:
    ZigbeeClient();
    ~ZigbeeClient();
    
    // Client interface implementation
    int connect(IPAddress ip, uint16_t port) override;
    int connect(const char *host, uint16_t port) override;
    size_t write(uint8_t) override;
    size_t write(const uint8_t *buf, size_t size) override;
    int available() override;
    int read() override;
    int read(uint8_t *buf, size_t size) override;
    int peek() override;
    void flush() override;
    void stop() override;
    uint8_t connected() override;
    operator bool() override;
    
    // Zigbee-specific methods
    void setZigbeeConnected(bool connected);
    bool sendZigbeeMessage(const uint8_t* data, size_t length);
    
private:
    bool isConnected;
    uint8_t readBuffer[ZIGBEE_MAX_PAYLOAD_SIZE];
    size_t readBufferPos;
    size_t readBufferLen;
    
    esp_zb_zcl_custom_cluster_cmd_req_t cmdReq;
};

#endif
