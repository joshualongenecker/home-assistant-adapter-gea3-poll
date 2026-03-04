/*!
 * @file
 * @brief ESPHome component that wraps the GEA2 MQTT bridge.
 *
 * Replaces HomeAssistantGea2Bridge.h from the reference repository.
 * WiFi and MQTT lifecycle are handled by ESPHome; this component owns only
 * the GEA2 protocol stack and the bridge state-machine.
 *
 * Pin numbers (tx_pin / rx_pin) and device_id are set from the ESPHome YAML
 * instead of being hard-coded as in the reference.
 */

#pragma once

#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome_mqtt_adapter.h"

extern "C" {
#include "Gea2MqttBridge.h"
#include "tiny_gea2_erd_client.h"
#include "tiny_gea2_interface.h"
#include "tiny_timer.h"
#include "tiny_event.h"
}

#include "tiny_uart_adapter.hpp"

namespace esphome {
namespace gea2_bridge {

class Gea2BridgeComponent : public Component {
 public:
  void set_device_id(const char* device_id) { device_id_ = device_id; }
  void set_tx_pin(int pin) { tx_pin_ = pin; }
  void set_rx_pin(int pin) { rx_pin_ = pin; }
  void set_client_address(uint8_t addr) { client_address_ = addr; }

  void setup() override;
  void loop() override;

  // Run after WiFi is up so ESPHome's MQTT client is already initialised.
  float get_setup_priority() const override { return setup_priority::AFTER_WIFI; }

 private:
  // GEA2 baud rate — must not be changed; GEA2 is defined at 19 200 baud.
  static constexpr unsigned long kBaud = 19200;

  const char* device_id_ = "";
  uint8_t client_address_ = 0xE4;
  int tx_pin_ = -1;
  int rx_pin_ = -1;

  // Request the ESPHome main loop to run at maximum speed (no 16 ms sleep).
  // The GEA2 interface needs tiny_gea2_interface_run() to be called at
  // baud-rate character frequency (~2 kHz at 19200 baud) to process echoed
  // bytes within the collision-detection window.  Without this, ESPHome's
  // default 60 Hz loop rate causes every TX attempt to time out on the echo
  // check, silencing the TX pin entirely.
  esphome::HighFrequencyLoopRequester high_freq_;

  tiny_timer_group_t timer_group_;

  // The GEA2 interface requires a 1 ms "interrupt" event to drive its
  // internal timing FSM.  We synthesise it with a periodic timer exactly
  // as the reference implementation does.
  tiny_event_t msec_interrupt_;
  tiny_timer_t msec_timer_;

  tiny_uart_adapter_t uart_adapter_;
  esphome_mqtt_adapter_t mqtt_adapter_;

  tiny_gea2_interface_t gea2_interface_;
  uint8_t receive_buffer_[255];
  uint8_t send_queue_buffer_[10000];

  tiny_gea2_erd_client_t erd_client_;
  uint8_t client_queue_buffer_[8096];

  Gea2MqttBridge_t gea2_mqtt_bridge_;
};

}  // namespace gea2_bridge
}  // namespace esphome
