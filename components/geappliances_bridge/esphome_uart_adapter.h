/*!
 * @file
 * @brief Polling-based UART adapter implementing i_tiny_uart_t using
 *        ESPHome's uart::UARTComponent.
 *
 * Replaces the Arduino Stream-based tiny_uart_adapter so the component
 * works with both Arduino and ESP-IDF frameworks in ESPHome.
 *
 * Ported from joshualongenecker/home-assistant-bridge-esphome
 */

#pragma once

#include "esphome/components/uart/uart.h"

extern "C" {
#include "hal/i_tiny_uart.h"
#include "tiny_event.h"
#include "tiny_timer.h"
}

typedef struct {
  i_tiny_uart_t interface;
  tiny_timer_group_t *timer_group;
  esphome::uart::UARTComponent *uart;
  tiny_event_t send_complete_event;
  tiny_event_t receive_event;
  tiny_timer_t timer;
  bool sent;
} esphome_uart_adapter_t;

#ifdef __cplusplus
extern "C" {
#endif

void esphome_uart_adapter_init(
  esphome_uart_adapter_t *self,
  tiny_timer_group_t *timer_group,
  esphome::uart::UARTComponent *uart);

#ifdef __cplusplus
}
#endif
