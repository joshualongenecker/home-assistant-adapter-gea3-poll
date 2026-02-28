/*!
 * @file
 * @brief ESPHome component for GE Appliances GEA2 polling bridge.
 */

#include "geappliances_bridge.h"

#include "esphome/components/mqtt/mqtt_client.h"
#include "esphome/core/log.h"

extern "C" {
#include "tiny_time_source.h"
}

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

  uart_stream_.set_device(this);

  ESP_LOGI(TAG, "Timer group startup");
  tiny_timer_group_init(&timer_group_, tiny_time_source_init());

  ESP_LOGI(TAG, "MQTT client adapter init");
  esphome_mqtt_client_adapter_init(&mqtt_client_adapter_, device_id_);

  // Register MQTT disconnect callback to notify the bridge
  mqtt::global_mqtt_client->set_on_disconnect(
    [this](mqtt::MQTTClientDisconnectReason) {
      esphome_mqtt_client_adapter_notify_disconnected(&this->mqtt_client_adapter_);
    });

  ESP_LOGI(TAG, "Fake msec interrupt init");
  tiny_event_init(&fake_msec_interrupt_);
  tiny_timer_start_periodic(
    &timer_group_, &fake_msec_timer_, 1, &fake_msec_interrupt_, +[](void *context) {
      tiny_event_publish(reinterpret_cast<tiny_event_t *>(context), nullptr);
    });

  ESP_LOGI(TAG, "UART adapter startup");
  tiny_uart_adapter_init(&uart_adapter_, &timer_group_, uart_stream_);

  ESP_LOGI(TAG, "GEA2 interface startup");
  tiny_gea2_interface_init(
    &gea2_interface_,
    &uart_adapter_.interface,
    tiny_time_source_init(),
    &fake_msec_interrupt_.interface,
    0xE4,
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
  tiny_timer_group_run(&timer_group_);
  tiny_gea2_interface_run(&gea2_interface_);
}

}  // namespace geappliances_bridge
}  // namespace esphome
