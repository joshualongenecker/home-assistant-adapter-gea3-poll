/*!
 * @file
 * @brief ESPHome MQTT client adapter implementation.
 *
 * Implements the i_mqtt_client_t vtable using ESPHome's mqtt::global_mqtt_client.
 */

#include "esphome_mqtt_client_adapter.h"

#include "esphome/components/mqtt/mqtt_client.h"
#include "esphome/core/log.h"

#include <cstdio>
#include <string>
#include <vector>

static const char *const TAG = "gea2_mqtt_adapter";

// Forward declarations for vtable functions
static void _publish_sub_topic(i_mqtt_client_t *self, const char *sub_topic, const char *payload);
static void _register_erd(i_mqtt_client_t *self, tiny_erd_t erd);
static void _update_erd(i_mqtt_client_t *self, tiny_erd_t erd, const void *data, uint8_t size);
static void _update_erd_write_result(i_mqtt_client_t *self, tiny_erd_t erd, bool success, uint8_t reason);
static i_tiny_event_t *_on_write_request(i_mqtt_client_t *self);
static i_tiny_event_t *_on_mqtt_disconnect(i_mqtt_client_t *self);

static const i_mqtt_client_api_t mqtt_client_api = {
  .publish_sub_topic = _publish_sub_topic,
  .register_erd = _register_erd,
  .update_erd = _update_erd,
  .update_erd_write_result = _update_erd_write_result,
  .on_write_request = _on_write_request,
  .on_mqtt_disconnect = _on_mqtt_disconnect,
};

static EspHomeMqttClientAdapter *adapter_from(i_mqtt_client_t *self)
{
  return reinterpret_cast<EspHomeMqttClientAdapter *>(self);
}

static std::string build_topic(const std::string &device_id, const char *suffix)
{
  return std::string("geappliances/") + device_id + "/" + suffix;
}

static std::string erd_topic(const std::string &device_id, tiny_erd_t erd, const char *suffix)
{
  char erd_str[16];
  snprintf(erd_str, sizeof(erd_str), "erd/0x%04x/%s", static_cast<unsigned>(erd), suffix);
  return build_topic(device_id, erd_str);
}

static void _publish_sub_topic(i_mqtt_client_t *self, const char *sub_topic, const char *payload)
{
  auto adapter = adapter_from(self);
  std::string topic = build_topic(adapter->device_id, sub_topic);
  esphome::mqtt::global_mqtt_client->publish(topic, payload, 0, false);
}

static void _register_erd(i_mqtt_client_t *self, tiny_erd_t erd)
{
  auto adapter = adapter_from(self);
  std::string topic = erd_topic(adapter->device_id, erd, "write");

  // Capture adapter and erd by value for the lambda
  EspHomeMqttClientAdapter *a = adapter;
  tiny_erd_t captured_erd = erd;

  esphome::mqtt::global_mqtt_client->subscribe(
    topic,
    [a, captured_erd](const std::string & /*topic*/, const std::string &payload) {
      // Validate that the payload is a well-formed hex string (even length, hex chars only)
      if(payload.size() % 2 != 0) {
        return;
      }
      for(char c : payload) {
        if(!isxdigit(static_cast<unsigned char>(c))) {
          return;
        }
      }

      // Convert hex string payload to bytes
      std::vector<uint8_t> bytes;
      bytes.reserve(payload.size() / 2);
      for(size_t i = 0; i + 1 < payload.size(); i += 2) {
        char hex_byte[3] = {payload[i], payload[i + 1], '\0'};
        bytes.push_back(static_cast<uint8_t>(strtoul(hex_byte, nullptr, 16)));
      }

      mqtt_client_on_write_request_args_t args = {
        .erd = captured_erd,
        .value = bytes.data(),
        .size = static_cast<uint8_t>(bytes.size()),
      };

      tiny_event_publish(&a->on_write_request_event, &args);
    },
    0);
}

static void _update_erd(i_mqtt_client_t *self, tiny_erd_t erd, const void *data, uint8_t size)
{
  auto adapter = adapter_from(self);
  std::string topic = erd_topic(adapter->device_id, erd, "value");

  // Encode binary data as lowercase hex string
  std::string payload;
  payload.reserve(size * 2);
  const uint8_t *bytes = reinterpret_cast<const uint8_t *>(data);
  for(uint8_t i = 0; i < size; i++) {
    char hex_byte[3];
    snprintf(hex_byte, sizeof(hex_byte), "%02x", bytes[i]);
    payload += hex_byte;
  }

  esphome::mqtt::global_mqtt_client->publish(topic, payload, 0, false);
}

static void _update_erd_write_result(i_mqtt_client_t *self, tiny_erd_t erd, bool success, uint8_t reason)
{
  auto adapter = adapter_from(self);
  std::string topic = erd_topic(adapter->device_id, erd, "writeResult");

  std::string payload;
  if(success) {
    payload = "OK";
  }
  else {
    char buf[32];
    snprintf(buf, sizeof(buf), "FAILED:%u", static_cast<unsigned>(reason));
    payload = buf;
  }

  esphome::mqtt::global_mqtt_client->publish(topic, payload, 0, false);
}

static i_tiny_event_t *_on_write_request(i_mqtt_client_t *self)
{
  auto adapter = adapter_from(self);
  return &adapter->on_write_request_event.interface;
}

static i_tiny_event_t *_on_mqtt_disconnect(i_mqtt_client_t *self)
{
  auto adapter = adapter_from(self);
  return &adapter->on_mqtt_disconnect_event.interface;
}

void esphome_mqtt_client_adapter_init(EspHomeMqttClientAdapter *self, const char *device_id)
{
  self->interface.api = &mqtt_client_api;
  self->device_id = device_id;
  tiny_event_init(&self->on_write_request_event);
  tiny_event_init(&self->on_mqtt_disconnect_event);
}

void esphome_mqtt_client_adapter_notify_disconnected(EspHomeMqttClientAdapter *self)
{
  tiny_event_publish(&self->on_mqtt_disconnect_event, nullptr);
}
