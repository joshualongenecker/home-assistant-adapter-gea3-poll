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

  // Capture the available count once, matching the reference implementation
  // (geappliances/home-assistant-bridge tiny_uart_adapter.cpp). This ensures
  // we only process the bytes that were available when the poll started and
  // avoids reading any bytes that arrive during event processing (e.g.,
  // reflected TX bytes from the GEA2 bus during send_next_byte callbacks).
  int rx_bytes = self->uart->available();

  while(rx_bytes--) {
    uint8_t byte;
    if(!self->uart->read_byte(&byte)) {
      break;
    }
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

void esphome_uart_adapter_init(
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

  // Period 0: poll the UART on every call to tiny_timer_group_run() (every
  // loop() iteration). At 19200 baud a byte arrives every ~0.52 ms; with a
  // 1 ms period the GEA2 interface's inter-byte gap timer misfires and
  // discards valid responses before they are fully received. Matching the
  // reference implementation (geappliances/home-assistant-bridge
  // tiny_uart_adapter.cpp, period = 0).
  tiny_timer_start_periodic(timer_group, &self->timer, 0, self, poll);
}
