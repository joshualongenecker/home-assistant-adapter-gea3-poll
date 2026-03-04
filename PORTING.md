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

## Framework: Arduino (via ESPHome/PlatformIO)

The reference firmware uses the Arduino framework directly in PlatformIO.
The ESPHome port also uses the **Arduino framework** so that all Arduino APIs
(`HardwareSerial`, `Preferences`, `String`, `Stream`) remain available to the
bridge code unchanged, and PlatformIO Arduino libraries (especially
`geappliances/home-assistant-bridge`) resolve correctly via `lib_deps`.

> **Design note:** An earlier version of this port used `framework: esp-idf`
> with `espressif/arduino-esp32` declared as an IDF component via
> `idf_component.yml`.  That approach failed to compile because:
> 1. ESPHome's build system does not forward a component's `idf_component.yml`
>    to the IDF Component Manager, so `Arduino.h` was not found.
> 2. `geappliances/home-assistant-bridge` is a PlatformIO Arduino library, not
>    an IDF component; its headers (e.g. `tiny_erd.h`) cannot be resolved via
>    `idf_component.yml` alone.
>
> The `idf_component.yml` file is retained in the component directory for
> reference but is not used in the Arduino-framework build.

---

## File-by-file change log

### Files copied **without modification** from the reference

| File | Purpose |
|---|---|
| `components/gea2_bridge/Gea2MqttBridge.h` | Bridge struct and `gea2_mqtt_bridge_init` / `gea2_mqtt_bridge_destroy` declarations |

> `Gea2MqttBridge.cpp` was originally copied verbatim but has since been
> updated — see the section below.

---

### `Gea2MqttBridge.cpp` — logging updated (Serial → ESP_LOGI / ESP_LOGD)

**Reason for change:** All `Serial.print` / `Serial.println` calls in the
reference file were replaced with ESPHome log macros so that log output is
routed through ESPHome's logging infrastructure (native API over WiFi) instead
of a hardware UART.

| Before | After |
|---|---|
| `Serial.println("message")` | `ESP_LOGI(TAG, "message")` |
| `sprintf(buf, fmt, …); Serial.print(buf)` | `ESP_LOGI(TAG, fmt, …)` |
| `Serial.println(String(n) + " erds")` | `ESP_LOGI(TAG, "%d erds", n)` |
| `Serial.print(".")` (poll heartbeat) | `ESP_LOGD(TAG, "polling erd 0x%04X", erd)` |
| `Serial.print("X")` (poll retry) | `ESP_LOGD(TAG, "poll retry")` |

`#include "esphome/core/log.h"` and `static const char* TAG = "gea2_bridge"`
were added at the top of the file.  All other logic — HSM, timer usage, NV
storage, ERD probing — is **identical** to the reference.

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

### `ApplianceErds.h` (new — copied without modification from reference)

The reference `ApplianceErds.h` declares a function-based API
(`GetCommonErdList()`, `GetEnergyErdList()`, `GetApplianceErdList(type)`)
used by `Gea2MqttBridge.cpp`.  The header is copied verbatim so that no
changes to `Gea2MqttBridge.cpp` are required.

---

### `ApplianceErds.cpp` (new — copied without modification from reference)

The reference `ApplianceErds.cpp` defines all ERD arrays inline and
implements the three accessor functions.  It is copied verbatim.

> **Earlier approach (replaced):** An earlier version of `ApplianceErds.cpp`
> attempted to reuse `src/ErdLists.h` from the repository root via a relative
> include (`../../src/ErdLists.h`).  This path resolves correctly when building
> with PlatformIO from the repo root, but fails in ESPHome's build tree where
> the component is copied to `src/esphome/components/gea2_bridge/` and the
> relative path no longer points to the repository root.  Copying the reference
> file directly avoids this problem entirely.

---

### `idf_component.yml` (retained, not used)

This file was written for the original `framework: esp-idf` design (see
Framework section above).  It declares `espressif/arduino-esp32 ^3.3.7` as
an IDF Component Manager dependency.  With `framework: arduino` it is not
processed and has no effect on the build.

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

## ESPHome loop rate — `HighFrequencyLoopRequester`

**Why this is required:**

ESPHome's default main-loop interval is **16 ms (~60 Hz)**.  When the loop
runs faster than 16 ms, the scheduler inserts a `delay()` to cap the rate.

The GEA2 interface (`tiny_gea2_interface`) uses an echo-based collision
detection mechanism: after sending each byte, it waits for that byte to
be echoed back on the RX pin (via the RS-485 transceiver loopback) before
sending the next one.  The window for detecting a valid echo is on the order
of one character time — ~0.52 ms at 19 200 baud.

At the default 60 Hz loop rate (16 ms per iteration), `tiny_gea2_interface_run`
is called every 16 ms.  The UART adapter's poll function (which publishes
incoming bytes as events) runs inside `tiny_timer_group_run` on the same
cadence.  Any echo that arrives 0.52 ms after TX is not processed until up to
16 ms later — well past the collision-detection timeout.  As a result the
interface never successfully completes a TX handshake, and the TX pin stays
silent.

The reference firmware avoids this issue because Arduino's `loop()` runs at
tens-of-kHz when there is no blocking code.

**Fix:** `Gea2BridgeComponent::setup()` calls `high_freq_.start()` on an
`esphome::HighFrequencyLoopRequester` member.  This removes the artificial
`delay()` from the ESPHome scheduler, allowing the loop to run as fast as
possible (limited only by FreeRTOS task scheduling, typically >5 kHz), which
is sufficient for the GEA2 echo window.

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

| Item | Reference (PlatformIO) | This port (ESPHome + Arduino) |
|---|---|---|
| Build system | PlatformIO | ESPHome → PlatformIO → Arduino toolchain |
| Framework | `framework = arduino` | `framework: arduino` (ESPHome) |
| WiFi | Managed in `main.cpp` | ESPHome `wifi:` component |
| MQTT | `PubSubClient` in `main.cpp` | ESPHome `mqtt:` component |
| NV storage | `Preferences` (Arduino) | Same — Arduino framework |
| Logging | `Serial.println` in bridge code | `ESP_LOGI` / `ESP_LOGD` everywhere; output via ESPHome native API (WiFi) only — `logger: baud_rate: 0` |
| OTA | PlatformIO upload | ESPHome OTA |
| LED heartbeat | `digitalWrite(LED_HEARTBEAT, millis() % 1000 < 500)` | Removed (use ESPHome `status_led:` if desired) |

---

## Build flags — `ARDUINO_USB_CDC_ON_BOOT=0`

**Why this flag is required:**

`arduino-tiny` (a transitive dependency of `geappliances/home-assistant-bridge`)
contains `tiny_uart.cpp` which defines `tiny_uart_init()`.  That function calls:

```cpp
Serial.begin(baud, static_cast<SerialConfig>(SERIAL_8N1));
```

On the Seeed XIAO ESP32-C3, the board definition sets `ARDUINO_USB_CDC_ON_BOOT=1`
which maps `Serial` to `HWCDC` (native USB CDC class).  `HWCDC::begin()` only
accepts a single baud-rate argument; the two-argument overload does not exist,
so the file fails to compile:

```
error: no matching function for call to 'HWCDC::begin(uint32_t&, SerialConfig)'
```

**Our component does not use `tiny_uart_init`.**  We pass an already-opened
`Serial1` (`HardwareSerial`) stream to `tiny_uart_adapter_init()`.  However,
`lib_ldf_mode: deep+` (required so that PlatformIO can resolve `PubSubClient.h`
as a transitive include of `home-assistant-bridge`) causes ALL library source
files to be compiled, including the unused `tiny_uart.cpp`.

**Fix:** The component's `__init__.py` `to_code()` function automatically calls
`cg.add_build_unflag("-DARDUINO_USB_CDC_ON_BOOT=1")` and
`cg.add_build_flag("-DARDUINO_USB_CDC_ON_BOOT=0")`.  This applies the fix for
every user of the component without requiring any manual `platformio_options`
entries in their ESPHome YAML.

**Side effect:** `Serial` resolves to `HardwareSerial(0)` (UART0, GPIO20=RX,
GPIO21=TX on ESP32-C3) instead of HWCDC.  The ESPHome logger is explicitly
configured to use `hardware_uart: UART0`.  USB CDC serial is not available.

For development logging, connect a USB-serial adapter to GPIO20/GPIO21.  For
normal production operation, use WiFi logging via the ESPHome API (visible in
the ESPHome dashboard or any ESPHome-compatible app).
