/*!
 * @file
 * @brief ESPHome MQTT client adapter implementing i_mqtt_client_t.
 *
 * Bridges the GEA2 bridge's i_mqtt_client_t interface to ESPHome's
 * built-in MQTT client (mqtt::global_mqtt_client).
 *
 * Topics used (matching the home-assistant-bridge library conventions):
 *   ERD value publish:  geappliances/{device_id}/erd/0x{erd:04x}/value
 *   ERD write subscribe: geappliances/{device_id}/erd/0x{erd:04x}/write
 *   Sub-topic publish:  geappliances/{device_id}/{subtopic}
 */

#pragma once

#include <string>

extern "C" {
#include "i_mqtt_client.h"
#include "tiny_event.h"
}

/*!
 * @brief ESPHome MQTT adapter state.
 *
 * Implements i_mqtt_client_t using ESPHome's global_mqtt_client.
 * The interface field MUST be first for C-style upcasting to work.
 */
struct EspHomeMqttClientAdapter {
  i_mqtt_client_t interface;
  std::string device_id;
  tiny_event_t on_write_request_event;
  tiny_event_t on_mqtt_disconnect_event;
};

/*!
 * @brief Initialize the ESPHome MQTT client adapter.
 *
 * @param self The adapter instance to initialize.
 * @param device_id The device identifier used to construct MQTT topics.
 */
void esphome_mqtt_client_adapter_init(EspHomeMqttClientAdapter *self, const char *device_id);

/*!
 * @brief Notify the adapter that MQTT has disconnected.
 *
 * Fires the on_mqtt_disconnect event so the GEA2 bridge can react.
 */
void esphome_mqtt_client_adapter_notify_disconnected(EspHomeMqttClientAdapter *self);
