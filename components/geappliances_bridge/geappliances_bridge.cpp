/*!
 * @file
 * @brief ESPHome component for GE Appliances GEA2 polling bridge.
 */

#include "geappliances_bridge.h"

#include "esphome/components/mqtt/mqtt_client.h"
#include "esphome/core/log.h"

static const char *const TAG = "geappliances_bridge";

static const tiny_gea2_erd_client_configuration_t client_configuration = {
  .request_timeout = 250,
  .request_retries = 10
};

namespace esphome {
namespace geappliances_bridge {

void GEAppliancesBridgeComponent::setup()
{
  ESP_LOGI(TAG, "GEA2 bridge startup");

  ESP_LOGI(TAG, "Timer group startup");
  tiny_timer_group_init(&timer_group_, esphome_time_source_init());

  ESP_LOGI(TAG, "UART adapter startup");
  esphome_uart_adapter_init(&uart_adapter_, &timer_group_, this->parent_);

  ESP_LOGI(TAG, "MQTT client adapter init");
  esphome_mqtt_client_adapter_init(&mqtt_client_adapter_, device_id_);

  // MQTT disconnect/reconnect handling is done in loop() by tracking
  // connection state, rather than using set_on_disconnect() which replaces
  // ESPHome's internal backend callback and breaks MQTT state tracking
  // and automatic reconnection.

  // The GEA2 interface needs a periodic 1 ms interrupt to drive its internal
  // timers (used for inter-byte gap detection, collision avoidance, etc.).
  ESP_LOGI(TAG, "Msec interrupt init");
  tiny_event_init(&msec_interrupt_);
  tiny_timer_start_periodic(
    &timer_group_, &msec_timer_, 1, &msec_interrupt_, +[](void *context) {
      tiny_event_publish(reinterpret_cast<tiny_event_t *>(context), nullptr);
    });

  ESP_LOGI(TAG, "GEA2 interface startup");
  // GEA2 client address: 0xE4 is the conventional address for a Home Assistant
  // bridge / adapter device on the GEA2 bus (used by PaulGoodJohn's reference
  // adapter and the GE FirstBuild community adapter hardware).
  static constexpr uint8_t kClientAddress = 0xE4;

  tiny_gea2_interface_init(
    &gea2_interface_,
    &uart_adapter_.interface,
    esphome_time_source_init(),
    &msec_interrupt_.interface,
    kClientAddress,
    send_queue_buffer_,
    sizeof(send_queue_buffer_),
    receive_buffer_,
    sizeof(receive_buffer_),
    false,
    1);

  ESP_LOGI(TAG, "GEA2 ERD client startup");
  tiny_gea2_erd_client_init(
    &erd_client_,
    &timer_group_,
    &gea2_interface_.interface,
    client_queue_buffer_,
    sizeof(client_queue_buffer_),
    &client_configuration);

  ESP_LOGI(TAG, "GEA2 MQTT bridge init");
  gea2_mqtt_bridge_init(
    &gea2_mqtt_bridge_,
    &timer_group_,
    &erd_client_.interface,
    &mqtt_client_adapter_.interface);

  ESP_LOGI(TAG, "GEA2 bridge started");
}

void GEAppliancesBridgeComponent::loop()
{
  // Track MQTT connection state transitions. When MQTT reconnects after a
  // disconnect, notify the bridge so it clears its ERD registry and
  // resubscribes. This matches the reference (connectToMqtt calls
  // notifyMqttDisconnected after reconnection). Using set_on_disconnect()
  // would replace ESPHome's internal backend callback, breaking MQTT state
  // tracking and reconnection.
  bool mqtt_connected = mqtt::global_mqtt_client->is_connected();
  if(mqtt_was_connected_ && !mqtt_connected) {
    ESP_LOGW(TAG, "MQTT disconnected");
    esphome_mqtt_client_adapter_notify_disconnected(&mqtt_client_adapter_);
  }
  mqtt_was_connected_ = mqtt_connected;

  tiny_timer_group_run(&timer_group_);
  tiny_gea2_interface_run(&gea2_interface_);
}

}  // namespace geappliances_bridge
}  // namespace esphome
