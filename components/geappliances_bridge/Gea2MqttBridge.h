/*!
 * @file
 * @brief GEA2 MQTT polling bridge.
 *
 * Discovers ERDs on a GE Appliances device via the GEA2 protocol,
 * builds a polling list, and continuously polls all discovered ERDs,
 * publishing their values to MQTT.
 *
 * Ported from PaulGoodJohn's GEA2 Polling Adapter:
 * https://github.com/paulgoodjohn/home-assistant-adapter
 */

#ifndef Gea2MqttBridge_h
#define Gea2MqttBridge_h

#include "i_mqtt_client.h"
#include "i_tiny_gea2_erd_client.h"
#include "tiny_hsm.h"
#include "tiny_timer.h"

#define POLLING_LIST_MAX_SIZE 256

typedef struct {
  uint32_t uptime;
  tiny_erd_t lastErdPolledSuccessfully;
  tiny_erd_t erd_polling_list[POLLING_LIST_MAX_SIZE];
  uint16_t pollingListCount;
  tiny_timer_group_t *timer_group;
  i_tiny_gea2_erd_client_t *erd_client;
  i_mqtt_client_t *mqtt_client;
  tiny_timer_t timer;
  tiny_timer_t applianceLostTimer;
  tiny_timer_t mqttInformationTimer;
  tiny_event_subscription_t mqtt_write_request_subscription;
  tiny_event_subscription_t mqtt_disconnect_subscription;
  tiny_event_subscription_t erd_client_activity_subscription;
  tiny_hsm_t hsm;
  void *erd_set;
  tiny_gea2_erd_client_request_id_t request_id;
  uint8_t erd_host_address;
  uint8_t appliance_type;
  const tiny_erd_t *applianceErdList;
  uint16_t applianceErdListCount;
  uint16_t erd_index;
} Gea2MqttBridge_t;

/*!
 * Initialize the GEA2 MQTT bridge.
 */
void gea2_mqtt_bridge_init(
  Gea2MqttBridge_t *self,
  tiny_timer_group_t *timer_group,
  i_tiny_gea2_erd_client_t *erd_client,
  i_mqtt_client_t *mqtt_client);

/*!
 * Destroy the GEA2 MQTT bridge and free resources.
 */
void gea2_mqtt_bridge_destroy(Gea2MqttBridge_t *self);

#endif
