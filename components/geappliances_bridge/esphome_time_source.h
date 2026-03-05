/*!
 * @file
 * @brief ESPHome time source implementing i_tiny_time_source_t using
 *        esphome::millis().
 *
 * Replaces tiny_time_source_init() (Arduino millis()) so the component
 * works with both Arduino and ESP-IDF frameworks in ESPHome.
 *
 * Ported from joshualongenecker/home-assistant-bridge-esphome
 */

#pragma once

extern "C" {
#include "i_tiny_time_source.h"
}

#ifdef __cplusplus
extern "C" {
#endif

/*!
 * @brief Returns the singleton ESPHome time source interface.
 */
i_tiny_time_source_t *esphome_time_source_init(void);

#ifdef __cplusplus
}
#endif
