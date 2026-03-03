/*!
 * @file
 * @brief ESPHome MQTT adapter — implementation.
 *
 * Implements i_mqtt_client_t using ESPHome's mqtt::global_mqtt_client.
 * The topic format, hex encoding/decoding logic, and write-result strings
 * are taken directly from mqtt_client_adapter.cpp in the reference repo.
 *
 * Key differences from the reference mqtt_client_adapter.cpp:
 *  - subscribe() / publish() call ESPHome's global_mqtt_client instead of
 *    PubSubClient methods.
 *  - There is no global "terrible hack" callback pointer; ESPHome's subscribe
 *    accepts a std::function so the adapter pointer is captured by value.
 *  - esphome_mqtt_adapter_poll() detects disconnects by comparing the current
 *    is_connected() state to the previous call; the reference detected this
 *    via notifyMqttDisconnected() called from main.cpp.
 */

#include "esphome_mqtt_adapter.h"
#include "esphome/components/mqtt/mqtt_client.h"

#include <string>
#include <cstdio>

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

static uint8_t ascii_hex_to_nybble(char c)
{
  if('A' <= c && c <= 'F') return static_cast<uint8_t>(c - 'A' + 10);
  if('a' <= c && c <= 'f') return static_cast<uint8_t>(c - 'a' + 10);
  return static_cast<uint8_t>(c - '0');
}

// ---------------------------------------------------------------------------
// i_mqtt_client_api_t vtable implementations
// ---------------------------------------------------------------------------

static void register_erd(i_mqtt_client_t* _self, tiny_erd_t erd)
{
  auto* self = reinterpret_cast<esphome_mqtt_adapter_t*>(_self);

  char erd_hex[5];
  snprintf(erd_hex, sizeof(erd_hex), "%04x", erd);

  std::string topic =
    std::string("geappliances/") + self->device_id + "/erd/0x" + erd_hex + "/write";

  // Capture self by pointer — safe because the adapter outlives all callbacks.
  esphome::mqtt::global_mqtt_client->subscribe(
    topic,
    [self](const std::string& t, const std::string& payload) {
      // Parse ERD from topic: geappliances/<id>/erd/0x<XXXX>/write
      auto erd_pos = t.find("/erd/0x");
      if(erd_pos == std::string::npos) return;

      std::string erd_str = t.substr(erd_pos + 7, 4);
      if(erd_str.size() != 4) return;
      tiny_erd_t erd = 0;
      for(char c : erd_str) {
        erd = static_cast<tiny_erd_t>((erd << 4) | ascii_hex_to_nybble(c));
      }

      // Decode hex-encoded payload into binary (max 255 bytes).
      if(payload.size() % 2 != 0) return;
      size_t bin_len = payload.size() / 2;
      if(bin_len > 255) return;

      uint8_t bin_data[255];
      for(size_t i = 0; i < bin_len; i++) {
        bin_data[i] = static_cast<uint8_t>(
          (ascii_hex_to_nybble(payload[2 * i]) << 4) |
          ascii_hex_to_nybble(payload[2 * i + 1]));
      }

      // tiny_event_publish is synchronous — subscribers consume bin_data
      // before this lambda returns, so the stack buffer is safe.
      mqtt_client_on_write_request_args_t args = {
        .erd = erd,
        .size = static_cast<uint8_t>(bin_len),
        .value = bin_data,
      };
      tiny_event_publish(&self->write_request, &args);
    },
    0 /* QoS 0 */);
}

static void update_erd(
  i_mqtt_client_t* _self,
  tiny_erd_t erd,
  const void* _value,
  uint8_t size)
{
  auto* self = reinterpret_cast<esphome_mqtt_adapter_t*>(_self);

  const auto* bytes = reinterpret_cast<const uint8_t*>(_value);
  std::string payload;
  payload.reserve(size * 2u);
  for(unsigned i = 0; i < size; i++) {
    char hex[3];
    snprintf(hex, sizeof(hex), "%02x", bytes[i]);
    payload += hex;
  }

  char erd_hex[5];
  snprintf(erd_hex, sizeof(erd_hex), "%04x", erd);

  std::string topic =
    std::string("geappliances/") + self->device_id + "/erd/0x" + erd_hex + "/value";

  esphome::mqtt::global_mqtt_client->publish(topic, payload, 0 /* QoS */, true /* retain */);
}

static void update_erd_write_result(
  i_mqtt_client_t* _self,
  tiny_erd_t erd,
  bool success,
  tiny_gea3_erd_client_write_failure_reason_t failure_reason)
{
  auto* self = reinterpret_cast<esphome_mqtt_adapter_t*>(_self);

  char erd_hex[5];
  snprintf(erd_hex, sizeof(erd_hex), "%04x", erd);

  std::string topic =
    std::string("geappliances/") + self->device_id + "/erd/0x" + erd_hex + "/write_result";

  const char* result;
  if(success) {
    result = "success";
  }
  else {
    switch(failure_reason) {
      case tiny_gea3_erd_client_write_failure_reason_retries_exhausted:
        result = "retries exhausted";
        break;
      case tiny_gea3_erd_client_write_failure_reason_not_supported:
        result = "not supported";
        break;
      case tiny_gea3_erd_client_write_failure_reason_incorrect_size:
        result = "incorrect size";
        break;
      default:
        result = "unknown error";
        break;
    }
  }

  esphome::mqtt::global_mqtt_client->publish(topic, result, 0 /* QoS */, true /* retain */);
}

static void publish_sub_topic(
  i_mqtt_client_t* _self,
  const char* sub_topic,
  const char* payload)
{
  auto* self = reinterpret_cast<esphome_mqtt_adapter_t*>(_self);

  std::string topic =
    std::string("geappliances/") + self->device_id + "/" + sub_topic;

  esphome::mqtt::global_mqtt_client->publish(topic, payload, 0 /* QoS */, false /* retain */);
}

static i_tiny_event_t* on_write_request(i_mqtt_client_t* _self)
{
  auto* self = reinterpret_cast<esphome_mqtt_adapter_t*>(_self);
  return &self->write_request.interface;
}

static i_tiny_event_t* on_mqtt_disconnect(i_mqtt_client_t* _self)
{
  auto* self = reinterpret_cast<esphome_mqtt_adapter_t*>(_self);
  return &self->mqtt_disconnect.interface;
}

static const i_mqtt_client_api_t kMqttApi = {
  register_erd,
  update_erd,
  update_erd_write_result,
  publish_sub_topic,
  on_write_request,
  on_mqtt_disconnect,
};

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void esphome_mqtt_adapter_init(esphome_mqtt_adapter_t* self, const char* device_id)
{
  self->interface.api = &kMqttApi;
  self->device_id = device_id;
  self->was_connected = false;
  tiny_event_init(&self->write_request);
  tiny_event_init(&self->mqtt_disconnect);
}

void esphome_mqtt_adapter_poll(esphome_mqtt_adapter_t* self)
{
  bool is_connected = esphome::mqtt::global_mqtt_client->is_connected();
  if(self->was_connected && !is_connected) {
    tiny_event_publish(&self->mqtt_disconnect, nullptr);
  }
  self->was_connected = is_connected;
}
