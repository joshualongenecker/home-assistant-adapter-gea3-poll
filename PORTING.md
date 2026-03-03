# PORTING.md — GEA2 ESPHome External Component

## Overview

This document tracks every change made when porting
[paulgoodjohn/home-assistant-adapter](https://github.com/paulgoodjohn/home-assistant-adapter)
(Arduino/PlatformIO, GEA2 appliance polling) into an ESPHome external
component.

The goal was **minimal code changes**: the GEA2 protocol stack, timer logic,
and polling state-machine were kept exactly as in the reference.  Only the
host-platform integrations (MQTT client, UART initialisation, application
entry point) were replaced with ESPHome equivalents.

---

## Framework: Arduino as ESP-IDF Component

The reference firmware uses the Arduino framework directly in PlatformIO.
The ESPHome port uses **ESP-IDF** as the base framework and adds
**arduino-esp32 as an IDF component** so that all Arduino APIs
(`HardwareSerial`, `Preferences`, `String`, `Stream`) remain available to the
bridge code unchanged.

This is the approach described at
<https://docs.espressif.com/projects/arduino-esp32/en/latest/esp-idf_component.html>:

```
idf.py add-dependency "espressif/arduino-esp32^3.3.7"
```

In the ESPHome component this dependency is declared in
`components/gea2_bridge/idf_component.yml`, which the ESPHome build system
picks up automatically when `framework: esp-idf` is selected in the YAML.

---

## File-by-file change log

### Files copied **without modification** from the reference

| File | Purpose |
|---|---|
| `components/gea2_bridge/Gea2MqttBridge.h` | Bridge struct and `gea2_mqtt_bridge_init` / `gea2_mqtt_bridge_destroy` declarations |
| `components/gea2_bridge/Gea2MqttBridge.cpp` | Full polling HSM: appliance discovery, common/energy/appliance ERD probing, poll loop, NV storage |

> **No lines were changed in these files.**  Any future update to the
> reference should be drop-in compatible.

---

### `main.cpp` → removed (replaced by ESPHome component lifecycle)

The reference `main.cpp` contained:
- WiFi connection management (`connectToWifi`, `configureWifi`)
- MQTT connection management (`connectToMqtt`, `configureMqtt`)
- Arduino `setup()` / `loop()`

In ESPHome, WiFi and MQTT are managed by their respective built-in
components.  The `setup()` / `loop()` entry points are provided by
`Gea2BridgeComponent` (see below).

---

### `HomeAssistantGea2Bridge.h/.cpp` → `components/gea2_bridge/gea2_bridge.h/.cpp`

**Reason for change:** The reference class managed its own `PubSubClient`
reference and called `pubSubClient->loop()` in `loop()`.  ESPHome drives MQTT
internally, so those references are removed.

| Aspect | Reference | This port |
|---|---|---|
| Class base | Plain C++ class | `esphome::Component` |
| `setup()` / `loop()` names | `begin()` / `loop()` | `setup()` / `loop()` |
| UART | Hard-coded `Serial1.begin(baud, SERIAL_8N1, D10, D9)` | `Serial1.begin(kBaud, SERIAL_8N1, rx_pin_, tx_pin_)` — pins from YAML |
| MQTT adapter | `mqtt_client_adapter_t` (PubSubClient) | `esphome_mqtt_adapter_t` (ESPHome native MQTT) |
| MQTT loop | `pubSubClient->loop()` in `loop()` | Removed — ESPHome handles it |
| Disconnect notification | `notifyMqttDisconnected()` called from `main.cpp` | `esphome_mqtt_adapter_poll()` called in `loop()` |
| Logging | `Serial.println("GEA2 bridge startup")` | `ESP_LOGI(TAG, "GEA2 bridge startup")` |
| Setup priority | n/a | `setup_priority::AFTER_WIFI` so ESPHome MQTT is ready |

The **GEA2 interface initialisation block** — including the fake-msec-interrupt
timer, all `tiny_gea2_interface_init` parameters, `tiny_gea2_erd_client_init`
parameters, and buffer sizes — is **identical** to the reference.

---

### `mqtt_client_adapter.hpp/.cpp` → `components/gea2_bridge/esphome_mqtt_adapter.h/.cpp`

**Reason for change:** `mqtt_client_adapter` wraps `PubSubClient`.  ESPHome
has its own MQTT client (`mqtt::global_mqtt_client`) so a new adapter was
written that implements the same `i_mqtt_client_t` vtable.

| Aspect | Reference `mqtt_client_adapter` | `esphome_mqtt_adapter` |
|---|---|---|
| Underlying MQTT | `PubSubClient` | `esphome::mqtt::global_mqtt_client` |
| subscribe | `client->subscribe(topic)` + single global `mqtt_callback` | `global_mqtt_client->subscribe(topic, lambda)` per ERD |
| publish | `client->publish(topic, payload, retain)` | `global_mqtt_client->publish(topic, payload, qos, retain)` |
| Disconnect detection | `notifyMqttDisconnected()` called externally | `esphome_mqtt_adapter_poll()` checks `is_connected()` each loop |
| Global state | `static mqtt_client_adapter_t* mqtt_callback_self` (required because PubSubClient callback has no context) | None — lambda captures `self` directly |
| Topic format | Identical: `geappliances/<id>/erd/0x<XXXX>/{value,write,write_result}` | Identical |
| Hex encoding/decoding | Identical logic | Identical logic |

---

### `ApplianceErds.h` (new, matching reference interface)

The reference uses a function-based API (`GetCommonErdList()`,
`GetEnergyErdList()`, `GetApplianceErdList(type)`) declared in `ApplianceErds.h`.
`Gea2MqttBridge.cpp` calls these functions, so the header was re-created with
the same signatures.

The `tiny_erd_list_t` struct (two fields: `erdList`, `erdCount`) is
layout-compatible with `applianceTypeToErdListAndCount_t` in `ErdLists.h`;
`static_assert` checks confirm this at compile time.

---

### `ApplianceErds.cpp` (new, wraps `src/ErdLists.h`)

Rather than duplicating the 6 000-line `ErdLists.h`, `ApplianceErds.cpp`
includes it via a relative path (`../../src/ErdLists.h`).  In C++, `const`
variables at namespace scope have internal linkage by default, so including the
header in a single translation unit is safe.

`GetApplianceErdList()` uses `reinterpret_cast` to return a pointer into
`applianceTypeToErdGroupTranslation[]`; the cast is validated by the
`static_assert` size/offset checks.

---

### `idf_component.yml` (new)

Declares `espressif/arduino-esp32 ^3.3.7` as an IDF Component Manager
dependency.  ESPHome picks this up automatically when building with
`framework: esp-idf`, equivalent to running:

```
idf.py add-dependency "espressif/arduino-esp32^3.3.7"
```

---

### `__init__.py` (new)

ESPHome component definition.  Adds:
- `geappliances/home-assistant-bridge ^1.3.0` via `cg.add_library()` — provides
  `tiny_uart_adapter`, `tiny_gea2_interface`, `tiny_gea2_erd_client`,
  `tiny_timer`, `tiny_hsm`, and the `i_mqtt_client` / `i_tiny_gea2_erd_client`
  interfaces.
- `knolleary/PubSubClient ^2.8` — `mqtt_client_adapter.cpp` (compiled as part
  of `home-assistant-bridge`) includes `PubSubClient.h`; this dep satisfies
  the compiler even though our code never calls those functions.

---

## Timers — no changes

The GEA2 interface requires a 1 ms periodic event on the `msec_interrupt`
input to drive its internal collision-avoidance and retry timers.  The
reference synthesises this with:

```cpp
tiny_timer_start_periodic(&timer_group, &fakeMsecTimer, 1, &fakeMsecInterrupt,
  +[](void* context) {
    tiny_event_publish(reinterpret_cast<tiny_event_t*>(context), nullptr);
  });
```

This block is reproduced **verbatim** in `gea2_bridge.cpp`.

The `retry_delay` (3000 ms), `appliance_lost_timeout` (60 000 ms),
`mqtt_info_update_period` (1000 ms), `request_timeout` (250 ms), and
`request_retries` (10) values in `Gea2MqttBridge.cpp` are also **unchanged**.

---

## Platform differences

| Item | Reference (PlatformIO) | This port (ESPHome + ESP-IDF) |
|---|---|---|
| Build system | PlatformIO | ESPHome → PlatformIO → ESP-IDF toolchain |
| Framework | `framework = arduino` | `framework: esp-idf` + `espressif/arduino-esp32` IDF component |
| WiFi | Managed in `main.cpp` | ESPHome `wifi:` component |
| MQTT | `PubSubClient` in `main.cpp` | ESPHome `mqtt:` component |
| NV storage | `Preferences` (unchanged, available via Arduino IDF component) | Same |
| Logging | `Serial.println` in bridge code | `Serial.println` in bridge code (unchanged); `ESP_LOGI` in component glue |
| OTA | PlatformIO upload | ESPHome OTA |
| LED heartbeat | `digitalWrite(LED_HEARTBEAT, millis() % 1000 < 500)` | Removed (use ESPHome `status_led:` if desired) |
