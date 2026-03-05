"""ESPHome external component for GE Appliances GEA2 polling bridge.

This component connects to a GE Appliances device via the GEA2 protocol,
discovers and polls ERDs (Electronic Reference Designators), and publishes
their values to MQTT for use with Home Assistant.

Based on PaulGoodJohn's GEA2 Polling Adapter:
https://github.com/paulgoodjohn/home-assistant-adapter
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import uart
from esphome.const import CONF_ID

CODEOWNERS = ["@joshualongenecker"]
DEPENDENCIES = ["mqtt", "uart"]

geappliances_bridge_ns = cg.esphome_ns.namespace("geappliances_bridge")
GEAppliancesBridgeComponent = geappliances_bridge_ns.class_(
    "GEAppliancesBridgeComponent", cg.Component, uart.UARTDevice
)

CONF_DEVICE_ID = "device_id"
CONF_GEA2_UART_ID = "gea2_uart_id"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(GEAppliancesBridgeComponent),
        cv.Optional(CONF_DEVICE_ID, default="ge_appliance"): cv.string,
        cv.Required(CONF_GEA2_UART_ID): cv.use_id(uart.UARTComponent),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    uart_component = await cg.get_variable(config[CONF_GEA2_UART_ID])
    cg.add(var.set_uart_parent(uart_component))

    cg.add(var.set_device_id(config[CONF_DEVICE_ID]))

    # Use direct GitHub URLs (the same sources the reference ESPHome integration
    # uses) so PlatformIO can resolve and download the libraries regardless of
    # whether the geappliances org is accessible via the PlatformIO registry.
    cg.add_library("https://github.com/ryanplusplus/tiny.git", None)
    cg.add_library("https://github.com/geappliances/tiny-gea-api.git#develop", None)
