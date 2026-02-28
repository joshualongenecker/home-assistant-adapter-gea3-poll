/*!
 * @file
 * @brief GEA2 MQTT polling bridge implementation.
 *
 * Implements an HSM that discovers and continuously polls ERDs on a GE
 * Appliances device via GEA2 protocol, publishing values to MQTT.
 *
 * Ported from PaulGoodJohn's GEA2 Polling Adapter:
 * https://github.com/paulgoodjohn/home-assistant-adapter
 *
 * Arduino-specific dependencies (Arduino.h, String, Serial, Preferences)
 * have been replaced with ESPHome/IDF equivalents so this file compiles
 * under both the Arduino and ESP-IDF frameworks.
 */

#include "esphome/core/log.h"

#if defined(ESP32)
#include "esp_system.h"
#endif

extern "C" {
#include "Gea2MqttBridge.h"
#include "i_tiny_gea2_erd_client.h"
#include "tiny_gea_constants.h"
#include "tiny_utils.h"
}
#include "ApplianceErds.h"

#include <cstdio>
#include <cstring>
#include <set>

using namespace std;
typedef Gea2MqttBridge_t self_t;

static const char *const TAG = "gea2_mqtt_bridge";

enum {
  retry_delay = 3000,
  appliance_lost_timeout = 60000,
  mqtt_info_update_period = 1000,
  ticks_per_second = 1000
};

enum {
  signal_start = tiny_hsm_signal_user_start,
  signal_timer_expired,
  signal_read_failed,
  signal_read_completed,
  signal_mqtt_disconnected,
  signal_appliance_lost,
  signal_write_requested
};

static void publishMqttInfo(void *context)
{
  self_t *self = (self_t *)context;

  self->uptime += (mqtt_info_update_period / ticks_per_second);

  char uptimeBuf[16];
  snprintf(uptimeBuf, sizeof(uptimeBuf), "%u", (unsigned)self->uptime);
  mqtt_client_publish_sub_topic(self->mqtt_client, "uptime", uptimeBuf);

#if defined(ESP32)
  char minHeapBuf[16];
  snprintf(minHeapBuf, sizeof(minHeapBuf), "%u", (unsigned)esp_get_minimum_free_heap_size());
  mqtt_client_publish_sub_topic(self->mqtt_client, "minHeap", minHeapBuf);

  char curHeapBuf[16];
  snprintf(curHeapBuf, sizeof(curHeapBuf), "%u", (unsigned)esp_get_free_heap_size());
  mqtt_client_publish_sub_topic(self->mqtt_client, "currentHeap", curHeapBuf);
#endif

  char lastErdBuf[10];
  snprintf(lastErdBuf, sizeof(lastErdBuf), "0x%04x", (unsigned)self->lastErdPolledSuccessfully);
  mqtt_client_publish_sub_topic(self->mqtt_client, "lastErd", lastErdBuf);
}

static void startMqttInfoTimer(self_t *self)
{
  self->uptime = 0;
  tiny_timer_start_periodic(self->timer_group, &self->mqttInformationTimer, mqtt_info_update_period, self, publishMqttInfo);
}

static void stopMqttInfoTimer(self_t *self)
{
  self->uptime = 0xFFFFFFFF;
  tiny_timer_stop(self->timer_group, &self->mqttInformationTimer);
}

static void arm_timer(self_t *self, tiny_timer_ticks_t ticks)
{
  tiny_timer_start(
    self->timer_group, &self->timer, ticks, self, +[](void *context) {
      tiny_hsm_send_signal(&reinterpret_cast<self_t *>(context)->hsm, signal_timer_expired, nullptr);
    });
}

static void DisarmLostApplianceTimer(self_t *self)
{
  tiny_timer_stop(self->timer_group, &self->applianceLostTimer);
}

static void ResetLostApplianceTimer(self_t *self)
{
  tiny_timer_start(
    self->timer_group, &self->applianceLostTimer, appliance_lost_timeout, self, +[](void *context) {
      tiny_hsm_send_signal(&reinterpret_cast<self_t *>(context)->hsm, signal_appliance_lost, nullptr);
    });
}

static void DisarmRetryTimer(self_t *self)
{
  tiny_timer_stop(self->timer_group, &self->timer);
}

static set<tiny_erd_t> &erd_set(self_t *self)
{
  return *reinterpret_cast<set<tiny_erd_t> *>(self->erd_set);
}

static tiny_hsm_result_t State_Top(tiny_hsm_t *hsm, tiny_hsm_signal_t signal, const void *data);
static tiny_hsm_result_t State_IdentifyAppliance(tiny_hsm_t *hsm, tiny_hsm_signal_t signal, const void *data);
static tiny_hsm_result_t State_AddCommonErds(tiny_hsm_t *hsm, tiny_hsm_signal_t signal, const void *data);
static tiny_hsm_result_t State_AddEnergyErds(tiny_hsm_t *hsm, tiny_hsm_signal_t signal, const void *data);
static tiny_hsm_result_t State_AddApplianceErds(tiny_hsm_t *hsm, tiny_hsm_signal_t signal, const void *data);
static tiny_hsm_result_t State_PollErdsFromList(tiny_hsm_t *hsm, tiny_hsm_signal_t signal, const void *data);

static tiny_hsm_result_t State_Top(tiny_hsm_t *hsm, tiny_hsm_signal_t signal, const void *data)
{
  self_t *self = container_of(self_t, hsm, hsm);
  (void)data;

  switch(signal) {
    case signal_write_requested: {
      auto args = reinterpret_cast<const mqtt_client_on_write_request_args_t *>(data);
      tiny_gea2_erd_client_write(self->erd_client, &self->request_id, self->erd_host_address, args->erd, args->value, args->size);
    } break;

    case signal_appliance_lost: {
      tiny_hsm_transition(hsm, State_IdentifyAppliance);
    } break;

    default:
      return tiny_hsm_result_signal_deferred;
  }

  return tiny_hsm_result_signal_consumed;
}

static tiny_hsm_result_t State_IdentifyAppliance(tiny_hsm_t *hsm, tiny_hsm_signal_t signal, const void *data)
{
  self_t *self = container_of(self_t, hsm, hsm);
  auto args = reinterpret_cast<const tiny_gea2_erd_client_on_activity_args_t *>(data);

  switch(signal) {
    case tiny_hsm_signal_entry: {
      self->erd_host_address = tiny_gea_broadcast_address;
    }
      [[fallthrough]];

    case signal_timer_expired: {
      ESP_LOGD(TAG, "Asking for appliance type ERD 0x0008 from address 0x%02X", self->erd_host_address);
      tiny_gea2_erd_client_read(self->erd_client, &self->request_id, self->erd_host_address, 0x0008);
      arm_timer(self, retry_delay);
      break;
    }

    case signal_read_completed: {
      DisarmRetryTimer(self);
      DisarmLostApplianceTimer(self);
      if(args->read_completed.erd == 0x0008) {
        self->erd_host_address = args->address;
        ESP_LOGI(TAG, "Using GEA address 0x%02X", self->erd_host_address);
      }

      const uint8_t *applianceTypeResponse = (const uint8_t *)args->read_completed.data;
      self->appliance_type = *applianceTypeResponse;
      tiny_hsm_transition(hsm, State_AddCommonErds);
      break;
    }

    case tiny_hsm_signal_exit: {
      DisarmRetryTimer(self);
      break;
    }

    default:
      return tiny_hsm_result_signal_deferred;
  }

  return tiny_hsm_result_signal_consumed;
}

static bool SendNextReadRequest(self_t *self)
{
  self->erd_index++;
  bool more_erds_to_try = (self->erd_index < self->applianceErdListCount);
  if(more_erds_to_try) {
    self->request_id++;
    tiny_gea2_erd_client_read(self->erd_client, &self->request_id, self->erd_host_address, self->applianceErdList[self->erd_index]);
    arm_timer(self, retry_delay);
  }
  return more_erds_to_try;
}

static void AddErdToPollingList(self_t *self, tiny_erd_t erd)
{
  if(erd_set(self).find(erd) == erd_set(self).end()) {
    mqtt_client_register_erd(self->mqtt_client, erd);
    erd_set(self).insert(erd);
  }

  if(self->pollingListCount < POLLING_LIST_MAX_SIZE) {
    self->erd_polling_list[self->pollingListCount] = erd;
    self->pollingListCount++;
    ESP_LOGD(TAG, "#%u Add ERD 0x%04X to polling list", (unsigned)self->pollingListCount, (unsigned)erd);
  }
  else {
    ESP_LOGW(TAG, "Polling list full (%u entries). Cannot add ERD 0x%04X", (unsigned)self->pollingListCount, (unsigned)erd);
  }
}

static tiny_hsm_result_t State_AddCommonErds(tiny_hsm_t *hsm, tiny_hsm_signal_t signal, const void *data)
{
  self_t *self = container_of(self_t, hsm, hsm);
  auto args = reinterpret_cast<const tiny_gea2_erd_client_on_activity_args_t *>(data);

  switch(signal) {
    case tiny_hsm_signal_entry: {
      const tiny_erd_list_t *commonErds = GetCommonErdList();
      self->applianceErdList = commonErds->erdList;
      self->applianceErdListCount = commonErds->erdCount;
      ESP_LOGI(TAG, "Looking for %u common ERDs", (unsigned)self->applianceErdListCount);
      self->erd_index = 0;
      self->pollingListCount = 0;
      tiny_gea2_erd_client_read(self->erd_client, &self->request_id, self->erd_host_address, self->applianceErdList[self->erd_index]);
      arm_timer(self, retry_delay);
    } break;

    case signal_timer_expired:
      if(!SendNextReadRequest(self)) {
        tiny_hsm_transition(hsm, State_AddEnergyErds);
      }
      break;

    case signal_read_completed:
      DisarmRetryTimer(self);
      AddErdToPollingList(self, args->read_completed.erd);
      mqtt_client_update_erd(
        self->mqtt_client,
        args->read_completed.erd,
        args->read_completed.data,
        args->read_completed.data_size);

      if(!SendNextReadRequest(self)) {
        tiny_hsm_transition(hsm, State_AddEnergyErds);
      }
      break;

    default:
      return tiny_hsm_result_signal_deferred;
  }

  return tiny_hsm_result_signal_consumed;
}

static tiny_hsm_result_t State_AddEnergyErds(tiny_hsm_t *hsm, tiny_hsm_signal_t signal, const void *data)
{
  self_t *self = container_of(self_t, hsm, hsm);
  auto args = reinterpret_cast<const tiny_gea2_erd_client_on_activity_args_t *>(data);

  switch(signal) {
    case tiny_hsm_signal_entry: {
      const tiny_erd_list_t *energyErds = GetEnergyErdList();
      self->applianceErdList = energyErds->erdList;
      self->applianceErdListCount = energyErds->erdCount;
      ESP_LOGI(TAG, "Looking for %u energy ERDs", (unsigned)self->applianceErdListCount);
      self->erd_index = 0;
      tiny_gea2_erd_client_read(self->erd_client, &self->request_id, self->erd_host_address, self->applianceErdList[self->erd_index]);
      arm_timer(self, retry_delay);
    } break;

    case signal_timer_expired:
      if(!SendNextReadRequest(self)) {
        tiny_hsm_transition(hsm, State_AddApplianceErds);
      }
      break;

    case signal_read_completed:
      DisarmRetryTimer(self);
      AddErdToPollingList(self, args->read_completed.erd);
      mqtt_client_update_erd(
        self->mqtt_client,
        args->read_completed.erd,
        args->read_completed.data,
        args->read_completed.data_size);

      if(!SendNextReadRequest(self)) {
        tiny_hsm_transition(hsm, State_AddApplianceErds);
      }
      break;

    default:
      return tiny_hsm_result_signal_deferred;
  }

  return tiny_hsm_result_signal_consumed;
}

static tiny_hsm_result_t State_AddApplianceErds(tiny_hsm_t *hsm, tiny_hsm_signal_t signal, const void *data)
{
  self_t *self = container_of(self_t, hsm, hsm);
  auto args = reinterpret_cast<const tiny_gea2_erd_client_on_activity_args_t *>(data);

  switch(signal) {
    case tiny_hsm_signal_entry: {
      const tiny_erd_list_t *applianceErds = GetApplianceErdList(self->appliance_type);
      self->applianceErdList = applianceErds->erdList;
      self->applianceErdListCount = applianceErds->erdCount;
      ESP_LOGI(TAG, "Looking for %u appliance-specific ERDs", (unsigned)self->applianceErdListCount);
      self->erd_index = 0;
      tiny_gea2_erd_client_read(self->erd_client, &self->request_id, self->erd_host_address, self->applianceErdList[self->erd_index]);
      arm_timer(self, retry_delay);
    } break;

    case signal_timer_expired:
      if(!SendNextReadRequest(self)) {
        tiny_hsm_transition(hsm, State_PollErdsFromList);
      }
      break;

    case signal_read_completed:
      DisarmRetryTimer(self);
      AddErdToPollingList(self, args->read_completed.erd);
      mqtt_client_update_erd(
        self->mqtt_client,
        args->read_completed.erd,
        args->read_completed.data,
        args->read_completed.data_size);

      if(!SendNextReadRequest(self)) {
        tiny_hsm_transition(hsm, State_PollErdsFromList);
      }
      break;

    default:
      return tiny_hsm_result_signal_deferred;
  }

  return tiny_hsm_result_signal_consumed;
}

static void SendNextPollReadRequest(self_t *self)
{
  self->erd_index++;
  if(self->erd_index >= self->pollingListCount) {
    self->erd_index = 0;
  }
  self->request_id++;
  tiny_gea2_erd_client_read(self->erd_client, &self->request_id, self->erd_host_address, self->erd_polling_list[self->erd_index]);
  arm_timer(self, retry_delay);
}

static tiny_hsm_result_t State_PollErdsFromList(tiny_hsm_t *hsm, tiny_hsm_signal_t signal, const void *data)
{
  self_t *self = container_of(self_t, hsm, hsm);
  auto args = reinterpret_cast<const tiny_gea2_erd_client_on_activity_args_t *>(data);

  switch(signal) {
    case tiny_hsm_signal_entry:
      DisarmLostApplianceTimer(self);
      ResetLostApplianceTimer(self);
      ESP_LOGI(TAG, "Polling %u ERDs", (unsigned)self->pollingListCount);
      [[fallthrough]];

    case signal_timer_expired:
      SendNextPollReadRequest(self);
      break;

    case signal_read_completed:
      DisarmRetryTimer(self);
      DisarmLostApplianceTimer(self);
      ResetLostApplianceTimer(self);
      mqtt_client_update_erd(
        self->mqtt_client,
        args->read_completed.erd,
        args->read_completed.data,
        args->read_completed.data_size);

      self->lastErdPolledSuccessfully = args->read_completed.erd;
      SendNextPollReadRequest(self);
      break;

    case signal_mqtt_disconnected:
      tiny_hsm_transition(&self->hsm, State_IdentifyAppliance);
      break;

    case tiny_hsm_signal_exit:
      DisarmRetryTimer(self);
      break;

    default:
      return tiny_hsm_result_signal_deferred;
  }

  return tiny_hsm_result_signal_consumed;
}

static const tiny_hsm_state_descriptor_t hsm_state_descriptors[] = {
  {.state = State_Top, .parent = nullptr},
  {.state = State_IdentifyAppliance, .parent = State_Top},
  {.state = State_AddCommonErds, .parent = State_Top},
  {.state = State_AddEnergyErds, .parent = State_Top},
  {.state = State_AddApplianceErds, .parent = State_Top},
  {.state = State_PollErdsFromList, .parent = State_Top}
};
static const tiny_hsm_configuration_t hsm_configuration = {
  .states = hsm_state_descriptors,
  .state_count = element_count(hsm_state_descriptors)
};

void gea2_mqtt_bridge_init(
  self_t *self,
  tiny_timer_group_t *timer_group,
  i_tiny_gea2_erd_client_t *erd_client,
  i_mqtt_client_t *mqtt_client)
{
  ESP_LOGI(TAG, "Bridge init start");
  self->timer_group = timer_group;
  self->erd_client = erd_client;
  self->mqtt_client = mqtt_client;
  self->erd_set = reinterpret_cast<void *>(new set<tiny_erd_t>());
  startMqttInfoTimer(self);

  tiny_event_subscription_init(
    &self->erd_client_activity_subscription, self, +[](void *context, const void *_args) {
      auto self = reinterpret_cast<self_t *>(context);
      auto args = reinterpret_cast<const tiny_gea2_erd_client_on_activity_args_t *>(_args);

      switch(args->type) {
        case tiny_gea2_erd_client_activity_type_read_completed:
          tiny_hsm_send_signal(&self->hsm, signal_read_completed, args);
          break;

        case tiny_gea2_erd_client_activity_type_read_failed:
          tiny_hsm_send_signal(&self->hsm, signal_read_failed, args);
          break;

        case tiny_gea2_erd_client_activity_type_write_completed:
          mqtt_client_update_erd_write_result(self->mqtt_client, args->write_completed.erd, true, 0);
          break;

        case tiny_gea2_erd_client_activity_type_write_failed:
          mqtt_client_update_erd_write_result(self->mqtt_client, args->write_failed.erd, false, args->write_failed.reason);
          break;
      }
    });
  tiny_event_subscribe(tiny_gea2_erd_client_on_activity(erd_client), &self->erd_client_activity_subscription);

  tiny_event_subscription_init(
    &self->mqtt_write_request_subscription, self, +[](void *context, const void *_args) {
      auto self = reinterpret_cast<self_t *>(context);
      auto args = reinterpret_cast<const mqtt_client_on_write_request_args_t *>(_args);
      tiny_hsm_send_signal(&self->hsm, signal_write_requested, args);
    });
  tiny_event_subscribe(mqtt_client_on_write_request(mqtt_client), &self->mqtt_write_request_subscription);

  tiny_event_subscription_init(
    &self->mqtt_disconnect_subscription, self, +[](void *context, const void *) {
      auto self = reinterpret_cast<self_t *>(context);
      reinterpret_cast<set<tiny_erd_t> *>(self->erd_set)->clear();
      tiny_hsm_send_signal(&self->hsm, signal_mqtt_disconnected, nullptr);
    });
  tiny_event_subscribe(mqtt_client_on_mqtt_disconnect(mqtt_client), &self->mqtt_disconnect_subscription);

  // Always start by identifying the appliance; ERD discovery happens fresh
  // on each boot (no NV storage required).
  tiny_hsm_init(&self->hsm, &hsm_configuration, State_IdentifyAppliance);
  ESP_LOGI(TAG, "Bridge init done");
}

void gea2_mqtt_bridge_destroy(self_t *self)
{
  ESP_LOGI(TAG, "Bridge destroy start");
  stopMqttInfoTimer(self);
  delete reinterpret_cast<set<tiny_erd_t> *>(self->erd_set);
  ESP_LOGI(TAG, "Bridge destroy done");
}
