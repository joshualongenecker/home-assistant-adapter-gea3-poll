/*!
 * @file
 * @brief ESPHome component for the GEA2 MQTT bridge — implementation.
 *
 * Mirrors HomeAssistantGea2Bridge.cpp from the reference repository with
 * the following minimal changes (see PORTING.md for full details):
 *
 *  1. setup() / loop() replace Arduino setup() / loop().
 *  2. Serial1 is configured with pins from YAML instead of hard-coded D10/D9.
 *  3. esphome_mqtt_adapter replaces mqtt_client_adapter (PubSubClient).
 *  4. ESP_LOGI replaces Serial.println for component-level log messages.
 *  5. pubSubClient->loop() is removed; ESPHome drives MQTT internally.
 *
 * Everything related to the GEA2 interface initialisation and the 1 ms
 * "fake msec interrupt" timer is kept byte-for-byte from the reference.
 */

#include "gea2_bridge.h"
#include "esphome/core/log.h"

#include <Arduino.h>

extern "C" {
#include "tiny_time_source.h"
}

static const char* TAG = "gea2_bridge";

// ERD client configuration — unchanged from reference.
static const tiny_gea2_erd_client_configuration_t kClientConfiguration = {
  .request_timeout = 250,
  .request_retries = 10,
};

namespace esphome {
namespace gea2_bridge {

void Gea2BridgeComponent::setup()
{
  ESP_LOGI(TAG, "GEA2 bridge startup");

  ESP_LOGI(TAG, "Timer group startup");
  tiny_timer_group_init(&timer_group_, tiny_time_source_init());

  // Open Serial1 on the configured pins.  The reference uses D10 (RX) and
  // D9 (TX) on the Seeed XIAO ESP32-C3; those map to GPIO 10 and GPIO 9.
  ESP_LOGI(TAG, "Serial1 startup (baud=%lu, rx=%d, tx=%d)", kBaud, rx_pin_, tx_pin_);
  Serial1.begin(static_cast<unsigned long>(kBaud), SERIAL_8N1, rx_pin_, tx_pin_);

  ESP_LOGI(TAG, "UART adapter startup");
  tiny_uart_adapter_init(&uart_adapter_, &timer_group_, Serial1);

  ESP_LOGI(TAG, "MQTT adapter init");
  esphome_mqtt_adapter_init(&mqtt_adapter_, device_id_);

  // Synthesise a 1 ms periodic interrupt event required by tiny_gea2_interface.
  // This is critical for GEA2 timing and must not be changed.
  ESP_LOGI(TAG, "Fake msec interrupt init");
  tiny_event_init(&msec_interrupt_);
  tiny_timer_start_periodic(
    &timer_group_, &msec_timer_, 1, &msec_interrupt_,
    +[](void* context) {
      auto* evt = reinterpret_cast<tiny_event_t*>(context);
      tiny_event_publish(evt, nullptr);
    });

  // GEA2 interface — parameters unchanged from reference.
  ESP_LOGI(TAG, "GEA2 interface startup");
  tiny_gea2_interface_init(
    &gea2_interface_,
    &uart_adapter_.interface,
    tiny_time_source_init(),
    &msec_interrupt_.interface,
    client_address_,
    send_queue_buffer_,
    sizeof(send_queue_buffer_),
    receive_buffer_,
    sizeof(receive_buffer_),
    false, /* ignore_destination_address */
    1 /* retries */);

  ESP_LOGI(TAG, "GEA2 ERD client startup");
  tiny_gea2_erd_client_init(
    &erd_client_,
    &timer_group_,
    &gea2_interface_.interface,
    client_queue_buffer_,
    sizeof(client_queue_buffer_),
    &kClientConfiguration);

  ESP_LOGI(TAG, "GEA2 MQTT bridge init");
  gea2_mqtt_bridge_init(
    &gea2_mqtt_bridge_,
    &timer_group_,
    &erd_client_.interface,
    &mqtt_adapter_.interface);

  ESP_LOGI(TAG, "GEA2 bridge started");
}

void Gea2BridgeComponent::loop()
{
  // Detect MQTT disconnect and fire the bridge event.
  // (In the reference this was triggered by notifyMqttDisconnected() from main.cpp.)
  esphome_mqtt_adapter_poll(&mqtt_adapter_);

  // Drive the timer system and the GEA2 protocol state machine.
  tiny_timer_group_run(&timer_group_);
  tiny_gea2_interface_run(&gea2_interface_);
}

}  // namespace gea2_bridge
}  // namespace esphome
