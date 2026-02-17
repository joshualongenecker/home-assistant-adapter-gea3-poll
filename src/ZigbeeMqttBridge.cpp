/*!
 * @file
 * @brief Zigbee Client implementation - provides Client interface for Zigbee
 */

#include "ZigbeeMqttBridge.h"
#include "esp_log.h"
#include <string.h>

static const char* TAG = "ZigbeeClient";

// Custom cluster ID for GEA3 MQTT messages
#define GEA3_MQTT_CLUSTER_ID 0xFC00
#define GEA3_MQTT_CMD_DATA 0x00

ZigbeeClient::ZigbeeClient()
    : isConnected(false), readBufferPos(0), readBufferLen(0)
{
    ESP_LOGI(TAG, "ZigbeeClient created");
    memset(readBuffer, 0, sizeof(readBuffer));
}

ZigbeeClient::~ZigbeeClient()
{
}

int ZigbeeClient::connect(IPAddress ip, uint16_t port)
{
    // Zigbee connection is handled by the stack
    // This just marks the client as ready
    ESP_LOGI(TAG, "ZigbeeClient connect (IP mode)");
    isConnected = true;
    return 1;
}

int ZigbeeClient::connect(const char *host, uint16_t port)
{
    // Zigbee connection is handled by the stack
    // This just marks the client as ready
    ESP_LOGI(TAG, "ZigbeeClient connect to %s:%d", host, port);
    isConnected = true;
    return 1;
}

size_t ZigbeeClient::write(uint8_t val)
{
    return write(&val, 1);
}

size_t ZigbeeClient::write(const uint8_t *buf, size_t size)
{
    if (!isConnected) {
        ESP_LOGE(TAG, "Not connected");
        return 0;
    }
    
    if (size > ZIGBEE_MAX_PAYLOAD_SIZE) {
        ESP_LOGW(TAG, "Message truncated from %zu to %d bytes", size, ZIGBEE_MAX_PAYLOAD_SIZE);
        size = ZIGBEE_MAX_PAYLOAD_SIZE;
    }
    
    if (sendZigbeeMessage(buf, size)) {
        return size;
    }
    
    return 0;
}

int ZigbeeClient::available()
{
    return readBufferLen - readBufferPos;
}

int ZigbeeClient::read()
{
    if (available() <= 0) {
        return -1;
    }
    
    return readBuffer[readBufferPos++];
}

int ZigbeeClient::read(uint8_t *buf, size_t size)
{
    int avail = available();
    if (avail <= 0) {
        return 0;
    }
    
    size_t toRead = min(size, (size_t)avail);
    memcpy(buf, readBuffer + readBufferPos, toRead);
    readBufferPos += toRead;
    
    return toRead;
}

int ZigbeeClient::peek()
{
    if (available() <= 0) {
        return -1;
    }
    
    return readBuffer[readBufferPos];
}

void ZigbeeClient::flush()
{
    // Nothing to flush for Zigbee
}

void ZigbeeClient::stop()
{
    ESP_LOGI(TAG, "ZigbeeClient stopped");
    isConnected = false;
    readBufferPos = 0;
    readBufferLen = 0;
}

uint8_t ZigbeeClient::connected()
{
    return isConnected ? 1 : 0;
}

ZigbeeClient::operator bool()
{
    return isConnected;
}

void ZigbeeClient::setZigbeeConnected(bool connected)
{
    isConnected = connected;
}

bool ZigbeeClient::sendZigbeeMessage(const uint8_t* data, size_t length)
{
    if (!isConnected) {
        ESP_LOGE(TAG, "Cannot send: not connected to Zigbee network");
        return false;
    }
    
    // Send custom cluster command to coordinator
    esp_zb_zcl_custom_cluster_cmd_req_t cmd_req;
    cmd_req.zcl_basic_cmd.dst_addr_u.addr_short = 0x0000; // Coordinator address
    cmd_req.zcl_basic_cmd.dst_endpoint = 1;
    cmd_req.zcl_basic_cmd.src_endpoint = 1;
    cmd_req.address_mode = ESP_ZB_APS_ADDR_MODE_16_ENDP_PRESENT;
    cmd_req.cluster_id = GEA3_MQTT_CLUSTER_ID;
    cmd_req.custom_cmd_id = GEA3_MQTT_CMD_DATA;
    cmd_req.data.type = ESP_ZB_ZCL_ATTR_TYPE_CHAR_STRING;
    cmd_req.data.value = (void*)data;
    cmd_req.data.size = length;
    
    esp_err_t err = esp_zb_zcl_custom_cluster_cmd_req(&cmd_req);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send Zigbee message: %d", err);
        return false;
    }
    
    ESP_LOGI(TAG, "Sent %zu bytes over Zigbee", length);
    return true;
}
