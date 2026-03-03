/*!
 * @file
 * @brief ESPHome MQTT adapter implementing i_mqtt_client_t.
 *
 * Replaces mqtt_client_adapter.hpp / mqtt_client_adapter.cpp from the
 * reference repository (paulgoodjohn/home-assistant-adapter).
 *
 * Instead of wrapping PubSubClient this adapter calls ESPHome's native
 * mqtt::global_mqtt_client, so no separate MQTT connection is needed.
 *
 * Topic format is identical to the reference:
 *   publish  : geappliances/<device_id>/erd/0x<XXXX>/value      (retained)
 *   subscribe: geappliances/<device_id>/erd/0x<XXXX>/write
 *   result   : geappliances/<device_id>/erd/0x<XXXX>/write_result (retained)
 *   sub-topic: geappliances/<device_id>/<sub_topic>
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "i_mqtt_client.h"
#include "tiny_event.h"

typedef struct {
  i_mqtt_client_t interface;
  const char* device_id;
  tiny_event_t write_request;
  tiny_event_t mqtt_disconnect;
  bool was_connected;
} esphome_mqtt_adapter_t;

/*!
 * Initialize the ESPHome MQTT adapter.
 * Must be called after the ESPHome MQTT component has been instantiated
 * (i.e. inside Component::setup(), not in the constructor).
 */
void esphome_mqtt_adapter_init(esphome_mqtt_adapter_t* self, const char* device_id);

/*!
 * Poll the adapter for MQTT connection-state changes.
 * Call once per Component::loop() iteration.  When a disconnect is detected
 * the mqtt_disconnect event is published, which triggers the bridge HSM to
 * reload any cached poll list or restart appliance discovery.
 */
void esphome_mqtt_adapter_poll(esphome_mqtt_adapter_t* self);

#ifdef __cplusplus
}
#endif
