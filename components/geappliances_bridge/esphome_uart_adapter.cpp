/*!
 * @file
 * @brief Polling-based UART adapter implementation.
 *
 * Polls the ESPHome UART component on every timer tick and fires
 * receive/send-complete events for the tiny GEA2 interface.
 */

#include "esphome_uart_adapter.h"

extern "C" {
#include "tiny_utils.h"
}

static void poll(void *context)
{
  auto self = static_cast<esphome_uart_adapter_t *>(context);

  while(self->uart->available()) {
    uint8_t byte;
    self->uart->read_byte(&byte);
    tiny_uart_on_receive_args_t args = {byte};
    tiny_event_publish(&self->receive_event, &args);
  }

  if(self->sent) {
    self->sent = false;
    tiny_event_publish(&self->send_complete_event, nullptr);
  }
}

static void send(i_tiny_uart_t *_self, uint8_t byte)
{
  auto self = reinterpret_cast<esphome_uart_adapter_t *>(_self);
  self->sent = true;
  self->uart->write_byte(byte);
}

static i_tiny_event_t *on_send_complete(i_tiny_uart_t *_self)
{
  auto self = reinterpret_cast<esphome_uart_adapter_t *>(_self);
  return &self->send_complete_event.interface;
}

static i_tiny_event_t *on_receive(i_tiny_uart_t *_self)
{
  auto self = reinterpret_cast<esphome_uart_adapter_t *>(_self);
  return &self->receive_event.interface;
}

static const i_tiny_uart_api_t api = {send, on_send_complete, on_receive};

extern "C" void esphome_uart_adapter_init(
  esphome_uart_adapter_t *self,
  tiny_timer_group_t *timer_group,
  esphome::uart::UARTComponent *uart)
{
  self->interface.api = &api;
  self->timer_group = timer_group;
  self->uart = uart;
  self->sent = false;

  tiny_event_init(&self->send_complete_event);
  tiny_event_init(&self->receive_event);

  // Poll UART every 1 ms — matches the GEA2 msec interrupt period and avoids
  // consuming excessive CPU on the event loop while still draining bytes fast
  // enough for the 19200 baud GEA2 bus (~1 byte per 0.5 ms).
  tiny_timer_start_periodic(timer_group, &self->timer, 1, self, poll);
}
