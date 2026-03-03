/*!
 * @file
 * @brief ESPHome component for GE Appliances GEA2 polling bridge.
 */

#include "geappliances_bridge.h"

#include "esphome/components/mqtt/mqtt_client.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

static const char *const TAG = "geappliances_bridge";

static const tiny_gea2_erd_client_configuration_t client_configuration = {
  .request_timeout = 250,
  .request_retries = 10
};

// Tick-counter time source for the GEA2 interface's internal timer group.
//
// Problem: tiny_gea2_interface.c's msec_interrupt_callback() calls
// tiny_timer_group_run(&self->timer_group) using the wall-clock time source
// (esphome::millis()).  After the ~50 ms ESPHome framework gap between our
// loop() calls, the first msec_interrupt_callback in the new loop sees
// accumulated delta = 50 ms.  If the GEA2 FSM is in state_receive (partially
// received response frame), the 6 ms interbyte timeout fires immediately with
// that 50 ms delta, transitioning the FSM out of state_receive and silently
// discarding the partial frame before poll() can read the remaining bytes.
//
// Fix: give the GEA2 interface a tick-counter time source whose value only
// increments by 1 each time msec_timer_ fires (once per 1 ms of wall-clock
// time within the tight loop).  tiny_timer_group_run(&self->timer_group) then
// always sees delta ≤ 1 regardless of wall-clock gaps, so GEA2 internal timers
// advance by at most 1 ms per msec event — matching the reference Arduino
// implementation's behavior.
static tiny_time_source_ticks_t g_gea2_tick_count = 0;

static tiny_time_source_ticks_t gea2_tick_ticks(i_tiny_time_source_t *)
{
  return g_gea2_tick_count;
}

static const i_tiny_time_source_api_t kGea2TickApi = {gea2_tick_ticks};
static i_tiny_time_source_t g_gea2_tick_source = {&kGea2TickApi};

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
      // Increment the GEA2 tick counter BEFORE publishing so that when
      // msec_interrupt_callback calls tiny_timer_group_run(&self->timer_group)
      // it sees delta = 1 (not the wall-clock gap accumulated since setup or
      // the previous ESPHome loop() call).
      g_gea2_tick_count++;
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
    &g_gea2_tick_source,
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

  // Run the GEA2 protocol stack for a fixed wall-clock window per ESPHome
  // loop() call. The reference Arduino implementation (paulgoodjohn/
  // home-assistant-adapter) calls tiny_timer_group_run + tiny_gea2_interface_run
  // once per Arduino loop() which runs at >10 kHz — effectively continuous.
  //
  // The complete GEA2 request/response cycle requires:
  //   TX request frame : ~5 ms  (10 bytes at 19200 baud, 0.52 ms/byte)
  //   Appliance processing + response delay : 5–30 ms
  //   RX response frame : ~6 ms  (12 bytes at 0.52 ms/byte)
  //   Total : up to ~41 ms
  //
  // Running for kLoopDurationMs (35 ms) keeps the entire cycle within a single
  // ESPHome loop() call in the common case, matching the reference behavior.
  //
  // The GEA2 interface's internal timer group uses a tick-counter time source
  // (g_gea2_tick_source) instead of wall-clock millis().  This ensures that
  // msec_interrupt_callback always advances the GEA2 internal timers by exactly
  // 1 ms per event, even after the ~50 ms ESPHome framework gap between our
  // loop() calls.  Without this, the first msec_interrupt_callback after the
  // gap would call tiny_timer_group_run(&self->timer_group) with a 50 ms
  // accumulated delta, instantly firing the 6 ms interbyte timeout and
  // discarding any partially-received response frame sitting in the UART FIFO.
  static constexpr uint32_t kLoopDurationMs = 35;
  uint32_t loop_start_ms = esphome::millis();
  while (esphome::millis() - loop_start_ms < kLoopDurationMs) {
    tiny_timer_group_run(&timer_group_);
    tiny_gea2_interface_run(&gea2_interface_);
  }
}

}  // namespace geappliances_bridge
}  // namespace esphome
