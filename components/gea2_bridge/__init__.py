"""
ESPHome external component for GEA2 appliance polling over MQTT.

Ported from paulgoodjohn/home-assistant-adapter (Arduino/PlatformIO) to an
ESPHome external component using ESP-IDF with Arduino as a component.

MQTT is handled by ESPHome's native mqtt: component; the GEA2 protocol stack
and polling logic are kept from the reference with minimal changes.
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

CODEOWNERS = ["@joshualongenecker"]

# mqtt must be present so esphome_mqtt_adapter can call global_mqtt_client
DEPENDENCIES = ["mqtt"]

gea2_bridge_ns = cg.esphome_ns.namespace("gea2_bridge")
Gea2BridgeComponent = gea2_bridge_ns.class_("Gea2BridgeComponent", cg.Component)

CONF_DEVICE_ID = "device_id"
CONF_CLIENT_ADDRESS = "client_address"
CONF_TX_PIN = "tx_pin"
CONF_RX_PIN = "rx_pin"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(Gea2BridgeComponent),
        cv.Required(CONF_DEVICE_ID): cv.string,
        cv.Required(CONF_TX_PIN): cv.positive_int,
        cv.Required(CONF_RX_PIN): cv.positive_int,
        cv.Optional(CONF_CLIENT_ADDRESS, default=0xE4): cv.uint8_t,
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    cg.add(var.set_device_id(config[CONF_DEVICE_ID]))
    cg.add(var.set_tx_pin(config[CONF_TX_PIN]))
    cg.add(var.set_rx_pin(config[CONF_RX_PIN]))
    cg.add(var.set_client_address(config[CONF_CLIENT_ADDRESS]))

    # GEA2 protocol stack + Arduino UART/time-source adapters.
    # PubSubClient is a transitive dep of home-assistant-bridge (mqtt_client_adapter)
    # and must be present so that file compiles, even though our adapter replaces it.
    cg.add_library("geappliances/home-assistant-bridge", "^1.3.0")
    cg.add_library("knolleary/PubSubClient", "^2.8")
