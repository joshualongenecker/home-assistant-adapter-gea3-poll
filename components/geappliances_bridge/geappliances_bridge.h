/*!
 * @file
 * @brief ESPHome component for GE Appliances GEA2 polling bridge.
 *
 * Connects to a GE Appliances device via the GEA2 protocol, polls ERDs,
 * and publishes data via MQTT. Based on PaulGoodJohn's GEA2 Polling Adapter:
 * https://github.com/paulgoodjohn/home-assistant-adapter
 */

#pragma once

#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "esphome/components/uart/uart.h"

#include "esphome_uart_adapter.h"
#include "esphome_time_source.h"
#include "esphome_mqtt_client_adapter.h"

extern "C" {
#include "tiny_gea2_erd_client.h"
#include "tiny_gea2_interface.h"
#include "tiny_timer.h"
#include "tiny_event.h"
#include "Gea2MqttBridge.h"
}

namespace esphome {
namespace geappliances_bridge {

/*!
 * @brief ESPHome component that bridges GEA2 appliances to MQTT.
 *
 * This component:
 * - Uses the GEA2 protocol to communicate with GE Appliances
 * - Discovers supported ERDs by polling the appliance
 * - Publishes ERD values to MQTT under geappliances/{device_id}/erd/{erd_id}/value
 * - Accepts ERD write commands from MQTT
 *
 * The component extends uart::UARTDevice so ESPHome wires the configured UART
 * automatically via set_uart_parent() from the generated main.cpp.
 */
class GEAppliancesBridgeComponent : public Component, public uart::UARTDevice {
 public:
  void set_device_id(const char *device_id) { device_id_ = device_id; }

  void setup() override;
  void loop() override;

  float get_setup_priority() const override { return setup_priority::AFTER_WIFI; }

 private:
  const char *device_id_{"ge_appliance"};

  tiny_timer_group_t timer_group_;

  tiny_event_t msec_interrupt_;
  tiny_timer_t msec_timer_;

  esphome_uart_adapter_t uart_adapter_;
  EspHomeMqttClientAdapter mqtt_client_adapter_;

  // Buffer sizes match the reference implementation (geappliances/home-assistant-bridge).
  // send_queue_buffer is large to accommodate burst GEA2 write traffic.
  // client_queue_buffer intentionally uses 8096 (not 8192) matching the reference.
  static constexpr size_t kSendQueueBufferSize = 10000;
  static constexpr size_t kClientQueueBufferSize = 8096;

  tiny_gea2_interface_t gea2_interface_;
  uint8_t receive_buffer_[255];
  uint8_t send_queue_buffer_[kSendQueueBufferSize];

  tiny_gea2_erd_client_t erd_client_;
  uint8_t client_queue_buffer_[kClientQueueBufferSize];

  Gea2MqttBridge_t gea2_mqtt_bridge_;
};

}  // namespace geappliances_bridge
}  // namespace esphome
