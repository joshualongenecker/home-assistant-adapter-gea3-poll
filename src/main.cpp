#include <Arduino.h>
#include <PubSubClient.h>
#include "esp_zigbee_core.h"
#include "esp_zigbee_type.h"
#include "esp_log.h"
#include "Config.h"
#include "ZigbeeMqttBridge.h"
#include "HomeAssistantGea3Bridge.h"

static const char* TAG = "GEA3_ZIGBEE";

// Zigbee configuration
#define ESP_ZB_PRIMARY_CHANNEL_MASK ESP_ZB_TRANSCEIVER_ALL_CHANNELS_MASK
#define ESP_ZB_ZED_CONFIG()                                         \
    {                                                               \
        .esp_zb_role = ESP_ZB_DEVICE_TYPE_ED,                      \
        .install_code_policy = INSTALLCODE_POLICY_ENABLE,           \
        .nwk_cfg = {                                               \
            .zed_cfg = {                                           \
                .ed_timeout = ESP_ZB_ED_AGING_TIMEOUT_64MIN,       \
                .keep_alive = 3000,                                 \
            },                                                      \
        },                                                          \
    }

#define ESP_ZB_DEFAULT_RADIO_CONFIG()                               \
    {                                                               \
        .radio_mode = RADIO_MODE_NATIVE,                           \
    }

#define ESP_ZB_DEFAULT_HOST_CONFIG()                                \
    {                                                               \
        .host_connection_mode = HOST_CONNECTION_MODE_NONE,         \
    }

static ZigbeeClient zigbeeClient;
static PubSubClient mqttClient(zigbeeClient);
static HomeAssistantGea3Bridge bridge;
static bool zigbee_connected = false;

// Zigbee attribute handler
static esp_err_t zb_attribute_handler(const esp_zb_zcl_set_attr_value_message_t *message)
{
    esp_err_t ret = ESP_OK;
    
    ESP_LOGI(TAG, "Received Zigbee attribute: cluster(0x%x), attribute(0x%x)",
             message->info.cluster, message->attribute.id);
    
    return ret;
}

// Zigbee action handler
static esp_err_t zb_action_handler(esp_zb_core_action_callback_id_t callback_id, const void *message)
{
    esp_err_t ret = ESP_OK;
    
    switch (callback_id) {
    case ESP_ZB_CORE_SET_ATTR_VALUE_CB_ID:
        ret = zb_attribute_handler((esp_zb_zcl_set_attr_value_message_t *)message);
        break;
        
    default:
        ESP_LOGW(TAG, "Receive Zigbee action(0x%x) callback", callback_id);
        break;
    }
    
    return ret;
}

// Zigbee signal handler
void esp_zb_app_signal_handler(esp_zb_app_signal_t *signal_struct)
{
    uint32_t *p_sg_p = signal_struct->p_app_signal;
    esp_err_t err_status = signal_struct->esp_err_status;
    esp_zb_app_signal_type_t sig_type = (esp_zb_app_signal_type_t)*p_sg_p;
    
    switch (sig_type) {
    case ESP_ZB_ZDO_SIGNAL_SKIP_STARTUP:
        ESP_LOGI(TAG, "Zigbee stack initialized");
        esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_INITIALIZATION);
        break;
        
    case ESP_ZB_BDB_SIGNAL_DEVICE_FIRST_START:
    case ESP_ZB_BDB_SIGNAL_DEVICE_REBOOT:
        if (err_status == ESP_OK) {
            ESP_LOGI(TAG, "Start network steering");
            esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_STEERING);
        } else {
            ESP_LOGE(TAG, "Failed to initialize Zigbee stack (status: %d)", err_status);
        }
        break;
        
    case ESP_ZB_BDB_SIGNAL_STEERING:
        if (err_status == ESP_OK) {
            esp_zb_ieee_addr_t extended_pan_id;
            esp_zb_get_extended_pan_id(extended_pan_id);
            ESP_LOGI(TAG, "Joined network successfully (Extended PAN ID: %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x)",
                     extended_pan_id[7], extended_pan_id[6], extended_pan_id[5], extended_pan_id[4],
                     extended_pan_id[3], extended_pan_id[2], extended_pan_id[1], extended_pan_id[0]);
            zigbee_connected = true;
            zigbeeClient.setZigbeeConnected(true);
            digitalWrite(LED_ZIGBEE, HIGH);
        } else {
            ESP_LOGI(TAG, "Network steering was not successful (status: %d)", err_status);
            esp_zb_scheduler_alarm((esp_zb_callback_t)esp_zb_bdb_start_top_level_commissioning,
                                   ESP_ZB_BDB_MODE_NETWORK_STEERING, 1000);
        }
        break;
        
    default:
        ESP_LOGI(TAG, "Zigbee signal: %d, status: %d", sig_type, err_status);
        break;
    }
}

static void configureZigbee()
{
    ESP_LOGI(TAG, "Initializing Zigbee stack");
    
    // Initialize Zigbee stack
    esp_zb_cfg_t zb_nwk_cfg = ESP_ZB_ZED_CONFIG();
    esp_zb_init(&zb_nwk_cfg);
    
    // Create endpoint list
    esp_zb_ep_list_t *esp_zb_ep_list = esp_zb_ep_list_create();
    
    // Create custom cluster list for GEA3 data
    esp_zb_cluster_list_t *esp_zb_cluster_list = esp_zb_zcl_cluster_list_create();
    
    // Add basic cluster (mandatory)
    esp_zb_attribute_list_t *esp_zb_basic_cluster = esp_zb_zcl_attr_list_create(ESP_ZB_ZCL_CLUSTER_ID_BASIC);
    esp_zb_basic_cluster_add_attr(esp_zb_basic_cluster, ESP_ZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID, (void *)ZIGBEE_MANUFACTURER);
    esp_zb_basic_cluster_add_attr(esp_zb_basic_cluster, ESP_ZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID, (void *)ZIGBEE_MODEL);
    esp_zb_cluster_list_add_basic_cluster(esp_zb_cluster_list, esp_zb_basic_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
    
    // Add identify cluster (mandatory)
    esp_zb_attribute_list_t *esp_zb_identify_cluster = esp_zb_zcl_attr_list_create(ESP_ZB_ZCL_CLUSTER_ID_IDENTIFY);
    esp_zb_identify_cluster_add_attr(esp_zb_identify_cluster, ESP_ZB_ZCL_ATTR_IDENTIFY_IDENTIFY_TIME_ID, (void *)0);
    esp_zb_cluster_list_add_identify_cluster(esp_zb_cluster_list, esp_zb_identify_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
    
    // Create endpoint
    esp_zb_endpoint_config_t endpoint_config = {
        .endpoint = ZIGBEE_ENDPOINT,
        .app_profile_id = ESP_ZB_AF_HA_PROFILE_ID,
        .app_device_id = ESP_ZB_HA_CUSTOM_ATTR_DEVICE_ID,
        .app_device_version = 0
    };
    
    esp_zb_ep_list_add_ep(esp_zb_ep_list, esp_zb_cluster_list, endpoint_config);
    esp_zb_device_register(esp_zb_ep_list);
    
    // Register callback for handling Zigbee actions
    esp_zb_core_action_handler_register(zb_action_handler);
    
    // Set channel mask
    esp_zb_set_primary_network_channel_set(ESP_ZB_PRIMARY_CHANNEL_MASK);
    
    ESP_LOGI(TAG, "Zigbee stack configured");
}

static void configureMqtt()
{
    // Set MQTT server (not used in Zigbee mode, but kept for compatibility)
    mqttClient.setServer(mqtt_server, mqtt_server_port);
}

static void connectToZigbee()
{
    if (!zigbee_connected) {
        digitalWrite(LED_ZIGBEE, millis() % 500 < 250); // Blink while connecting
    }
}

static void connectToMqtt()
{
    connectToZigbee();
    
    if(zigbee_connected) {
        digitalWrite(LED_ZIGBEE, HIGH);
        
        if(!mqttClient.connected()) {
            digitalWrite(LED_MQTT, LOW);
            
            unsigned retries = 0;
            while(!mqttClient.connected()) {
                if(retries++ > 10) {
                    Serial.println("MQTT connection failed, restarting...");
                    ESP.restart();
                }
                
                Serial.print("Attempting MQTT connection over Zigbee...");
                
                if(mqttClient.connect("", mqttUser, mqttPassword)) {
                    Serial.println("connected");
                    digitalWrite(LED_MQTT, HIGH);
                }
                else {
                    Serial.println("failed, rc=" + String(mqttClient.state()) + " will try again in 1 second");
                    delay(1000);
                }
            }
            
            bridge.notifyMqttDisconnected();
        }
    }
}

void setup()
{
    Serial.begin(115200);
    Serial.println();
    Serial.println("GEA3 Zigbee adapter startup");
    
    pinMode(LED_HEARTBEAT, OUTPUT);
    pinMode(LED_MQTT, OUTPUT);
    pinMode(LED_ZIGBEE, OUTPUT);
    
    // Initialize Zigbee
    configureZigbee();
    configureMqtt();
    
    // Start Zigbee stack
    ESP_ERROR_CHECK(esp_zb_start(false));
    
    // Initialize serial for GEA3 communication
    // Note: Pin numbers for ESP32-C6 (adjust as needed for your board)
    Serial1.begin(HomeAssistantGea3Bridge::baud, SERIAL_8N1, GPIO6, GPIO7);
    bridge.begin(mqttClient, Serial1, deviceId);
    
    Serial.println("GEA3 Zigbee adapter initialized");
}

void loop()
{
    connectToMqtt();
    bridge.loop();
    digitalWrite(LED_HEARTBEAT, millis() % 1000 < 500);
    
    // Zigbee stack runs its own task, so we just need to yield
    vTaskDelay(10 / portTICK_PERIOD_MS);
}
