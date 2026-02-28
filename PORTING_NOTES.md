# Porting the GEA2 Bridge to an ESPHome External Component

This document explains every change required to port
[PaulGoodJohn's GEA2 Polling Adapter](https://github.com/paulgoodjohn/home-assistant-adapter)
(a PlatformIO / Arduino firmware project) into the **ESPHome external component**
found in `components/geappliances_bridge/`.

The working reference for the ESPHome integration pattern is
[joshualongenecker/home-assistant-bridge-esphome PR #1](https://github.com/joshualongenecker/home-assistant-bridge-esphome/pull/1).

---

## 1. Library dependency references

### Original approach (broken)

```python
# __init__.py
cg.add_library("geappliances/home-assistant-bridge", "^1.3.0")
```

`geappliances/home-assistant-bridge` refers to the PlatformIO registry.
In practice the library is not listed in the public registry (the
`geappliances` GitHub organisation enforces SAML SSO), so PlatformIO
silently skips the dependency and the component headers (`tiny_erd.h`,
`tiny_gea2_interface.h`, `i_mqtt_client.h`, …) are never placed on the
include path.

### Fix — use direct GitHub URLs

```python
# __init__.py
cg.add_library("https://github.com/ryanplusplus/tiny.git", None)
cg.add_library("https://github.com/geappliances/tiny-gea-api.git#develop", None)
```

| Library | Provides |
|---------|----------|
| `ryanplusplus/tiny` | `tiny_erd.h`, `tiny_timer.h`, `tiny_event.h`, `tiny_hsm.h`, `i_tiny_time_source.h`, `i_tiny_uart.h`, `tiny_utils.h`, … |
| `geappliances/tiny-gea-api#develop` | `tiny_gea2_interface.h`, `tiny_gea2_erd_client.h`, `tiny_gea3_interface.h`, `tiny_gea3_erd_client.h`, `i_mqtt_client.h`, `tiny_gea_constants.h`, … |

Both repos are public and do not require authentication, so PlatformIO
can download them in any build environment.

---

## 2. Arduino `Stream`-based UART adapter → `esphome_uart_adapter`

### Original approach (broken)

```cpp
// geappliances_bridge.h
#include <Stream.h>
#include "tiny_uart_adapter.hpp"   // wraps Arduino Stream

class ESPHomeUARTStream : public Stream { … };  // Arduino-only

tiny_uart_adapter_t uart_adapter_;             // expects a Stream*
ESPHomeUARTStream   uart_stream_;
```

```cpp
// geappliances_bridge.cpp
uart_stream_.set_device(this);
tiny_uart_adapter_init(&uart_adapter_, &timer_group_, uart_stream_);
```

`tiny_uart_adapter.hpp` is part of the `home-assistant-bridge` library
and wraps an Arduino `Stream` object.  There is no `Stream` class when
the **ESP-IDF** framework is selected, so both the include and the class
definition fail to compile.

### Fix — polling-based `esphome_uart_adapter`

New files `esphome_uart_adapter.h/.cpp` implement `i_tiny_uart_t` directly
against `esphome::uart::UARTComponent` without any Arduino dependency:

```cpp
// esphome_uart_adapter.h
typedef struct {
  i_tiny_uart_t                  interface;
  tiny_timer_group_t            *timer_group;
  esphome::uart::UARTComponent  *uart;
  tiny_event_t                   send_complete_event;
  tiny_event_t                   receive_event;
  tiny_timer_t                   timer;
  bool                           sent;
} esphome_uart_adapter_t;

void esphome_uart_adapter_init(
  esphome_uart_adapter_t        *self,
  tiny_timer_group_t            *timer_group,
  esphome::uart::UARTComponent  *uart);
```

The `poll` callback drains all bytes from `uart->read_byte()` into
`receive_event`, and fires `send_complete_event` after each transmitted byte.

### Critical: poll period must be 0 (fire every loop)

The reference implementation (`geappliances/home-assistant-bridge`,
`tiny_uart_adapter.cpp`) uses **period = 0**:

```cpp
// Reference — fires on every call to tiny_timer_group_run()
tiny_timer_start_periodic(timer_group, &self->timer, 0, self, poll);
```

**Setting period = 1 breaks response parsing.** At 19200 baud a byte arrives
every ~0.52 ms. With a 1 ms poll interval, the GEA2 interface's inter-byte gap
timer fires between polls and treats the mid-packet pause as an end-of-frame,
silently discarding the partial (or complete) response. The result is that the
appliance identification read (ERD 0x0008) always times out and retries — the
bridge never advances past `State_IdentifyAppliance`.

```cpp
// Correct — period 0: poll on every loop() iteration
tiny_timer_start_periodic(timer_group, &self->timer, 0, self, poll);
```

Usage in the component:

```cpp
// geappliances_bridge.cpp
esphome_uart_adapter_init(&uart_adapter_, &timer_group_, this->parent_);
//                                                              ^
//  GEAppliancesBridgeComponent extends uart::UARTDevice, which stores
//  the wired-up UARTComponent* as the protected member 'parent_'.
//  Passing 'this' would be a type error (GEAppliancesBridgeComponent*
//  is not implicitly convertible to UARTComponent*).
```

---

## 3. Arduino `millis()` time source → `esphome_time_source`

### Original approach (broken)

```cpp
// geappliances_bridge.cpp
extern "C" { #include "tiny_time_source.h" }  // from home-assistant-bridge lib
…
tiny_timer_group_init(&timer_group_, tiny_time_source_init());
tiny_gea2_interface_init(…, tiny_time_source_init(), …);
```

`tiny_time_source.h` is bundled inside `home-assistant-bridge` and wraps
`millis()` (Arduino).  Without that library the header is missing; without
the Arduino framework `millis()` is undefined.

### Fix — `esphome_time_source`

New files `esphome_time_source.h/.cpp` implement `i_tiny_time_source_t`
using `esphome::millis()` from `esphome/core/hal.h`, which is available
on both Arduino and ESP-IDF builds of ESPHome:

```cpp
// esphome_time_source.cpp
#include "esphome/core/hal.h"

static tiny_time_source_ticks_t ticks(i_tiny_time_source_t *) {
  return esphome::millis();
}
```

Usage:

```cpp
tiny_timer_group_init(&timer_group_, esphome_time_source_init());
tiny_gea2_interface_init(…, esphome_time_source_init(), …);
```

---

## 4. Arduino-specific code in `Gea2MqttBridge.cpp`

PaulGoodJohn's bridge used Arduino APIs for logging, string formatting,
and non-volatile storage.  Each was replaced:

| Arduino API | Replacement | Notes |
|---|---|---|
| `#include <Arduino.h>` | removed | no longer needed |
| `#include <Preferences.h>` | removed | see §5 |
| `String(value)` | `snprintf` into a `char[]` | standard C |
| `String(erd, HEX)` | `snprintf(buf, …, "0x%04x", erd)` | standard C |
| `Serial.println(…)` | `ESP_LOGI(TAG, …)` | ESPHome logging |
| `Serial.print(".")` | removed (was progress noise) | |
| `esp_get_free_heap_size()` | `#include "esp_system.h"` (ESP-IDF, guarded by `#if defined(ESP32)`) | already available on ESP-IDF without Arduino |
| `__attribute__((fallthrough))` | `[[fallthrough]]` | standard C++17 attribute |

---

## 5. Non-volatile (NV) storage removal

The original Arduino sketch saved the discovered poll list to flash via
`Preferences` so the next boot could skip re-discovery.  ESPHome does not
provide a drop-in equivalent, and `<Preferences.h>` is Arduino-specific.

The three NV functions (`ValidPollingListLoaded`, `SavePollingListToNVStore`,
`ClearNVStorage`) and their call sites were removed.  The bridge now always
starts from the `State_IdentifyAppliance` state on each boot.

```cpp
// Before
if (ValidPollingListLoaded(self)) {
  tiny_hsm_init(&self->hsm, &hsm_configuration, State_PollErdsFromList);
} else {
  tiny_hsm_init(&self->hsm, &hsm_configuration, State_IdentifyAppliance);
}

// After — always re-discover
tiny_hsm_init(&self->hsm, &hsm_configuration, State_IdentifyAppliance);
```

Discovery typically completes in a few seconds, so the user-visible
impact is minimal.

---

## 6. Vendored `i_mqtt_client.h`

`i_mqtt_client.h` lives in `geappliances/home-assistant-bridge`, not in
`geappliances/tiny-gea-api`.  Adding the full `home-assistant-bridge`
library would pull in Arduino-only source files (`mqtt_client_adapter.cpp`,
`tiny_uart_adapter.cpp`, etc.) that conflict with the ESP-IDF build.

Instead, the single pure-C interface header was copied directly into the
component directory as `components/geappliances_bridge/i_mqtt_client.h`.
This is the only file needed from that library; no Arduino-only `.cpp`
files are compiled.

---

## 7. C++ designated-initializer field ordering

C++ (unlike C99) requires designated initializers to appear **in the same
order as the struct field declarations**.  Two structs were initialised
out of order and caused compiler errors:

### `i_mqtt_client_api_t` vtable

```cpp
// Wrong order (caused: "designator order for field … does not match
//   declaration order")
static const i_mqtt_client_api_t mqtt_client_api = {
  .publish_sub_topic = _publish_sub_topic,   // ← declared 4th
  .register_erd      = _register_erd,        // ← declared 1st
  …
};

// Correct order — matches struct declaration in i_mqtt_client.h
static const i_mqtt_client_api_t mqtt_client_api = {
  .register_erd           = _register_erd,
  .update_erd             = _update_erd,
  .update_erd_write_result = _update_erd_write_result,
  .publish_sub_topic      = _publish_sub_topic,
  .on_write_request       = _on_write_request,
  .on_mqtt_disconnect     = _on_mqtt_disconnect,
};
```

### `mqtt_client_on_write_request_args_t`

```cpp
// Wrong order (.value before .size; declaration order is erd, size, value)
mqtt_client_on_write_request_args_t args = {
  .erd   = captured_erd,
  .value = bytes.data(),       // ← declared 3rd
  .size  = …,                  // ← declared 2nd
};

// Correct order
mqtt_client_on_write_request_args_t args = {
  .erd   = captured_erd,
  .size  = static_cast<uint8_t>(bytes.size()),
  .value = bytes.data(),
};
```

### Ambiguous `publish()` overload

`esphome::mqtt::MQTTClientComponent::publish` has two overloads that
accept `(std::string, X, uint8_t, bool)`:

```cpp
bool publish(const std::string &topic, const std::string &payload, uint8_t qos, bool retain);
bool publish(const std::string &topic, const char *payload, size_t payload_length, uint8_t qos, bool retain);
```

Passing a bare `const char *` with integer `0` and `false` is ambiguous
because `0` could be `size_t payload_length` **or** `uint8_t qos`.
The fix is to cast the payload to `std::string` so the first overload is
selected unambiguously:

```cpp
// Before (warning: ambiguous)
global_mqtt_client->publish(topic, payload, 0, false);

// After (unambiguous)
global_mqtt_client->publish(topic, std::string(payload), 0, false);
```

---

## 8. ESPHome component Python layer (`__init__.py`)

The Python layer is responsible for:

1. Declaring `DEPENDENCIES = ["mqtt", "uart"]` so ESPHome pulls in the
   correct component modules before this one.
2. Defining the YAML config schema (`gea2_uart_id`, `device_id`).
3. Generating C++ init calls (`set_uart_parent`, `set_device_id`) via
   `cg.add()` in `to_code()`.
4. Adding the two library dependencies via `cg.add_library()`.

No framework-level validators (`cv.only_with_arduino`) are needed because
all Arduino-specific code has been replaced — the component compiles under
both Arduino and ESP-IDF frameworks.

---

## 9. Example YAML configuration

```yaml
esphome:
  name: ge-appliance-bridge
  platformio_options:
    build_flags: ["-std=gnu++17"]
    build_unflags: ["-std=gnu++11"]

esp32:
  board: seeed_xiao_esp32c3
  framework:
    type: arduino          # or: type: esp-idf

mqtt:
  broker: !secret mqtt_broker
  username: !secret mqtt_username
  password: !secret mqtt_password

uart:
  - id: gea2_uart
    tx_pin: GPIO9
    rx_pin: GPIO10
    baud_rate: 19200

external_components:
  - source:
      type: git
      url: https://github.com/joshualongenecker/home-assistant-adapter-gea3-poll
      ref: copilot/create-esphome-gea2-polling-feature
    components: [geappliances_bridge]

geappliances_bridge:
  gea2_uart_id: gea2_uart
  device_id: "my_ge_appliance"
```

---

## 10. Summary of all changed / new files

| File | Change |
|------|--------|
| `__init__.py` | Replace `home-assistant-bridge` PlatformIO ref with two GitHub URLs |
| `geappliances_bridge.h` | Remove Arduino stream types; add ESPHome adapters; define named constants for buffer sizes (`kSendQueueBufferSize = 10000`, `kClientQueueBufferSize = 8096`) |
| `geappliances_bridge.cpp` | Use `esphome_uart_adapter_init` (pass `this->parent_`) + `esphome_time_source_init`; remove Arduino-specific UART stream setup |
| `Gea2MqttBridge.cpp` | Remove `Arduino.h`, `Preferences.h`, `String`, `Serial`, NV storage; replace with `ESP_LOGI`, `snprintf`, `esp_system.h`; add bounds check on `erd_polling_list` write |
| `esphome_mqtt_client_adapter.h` | Add `extern "C"` guards; use `typedef struct … _t` naming |
| `esphome_mqtt_client_adapter.cpp` | Fix vtable / struct designated-initializer field order; resolve ambiguous `publish()` overload; widen hex-encode loop variable to `uint16_t` |
| `i_mqtt_client.h` *(vendored)* | Pure-C interface header copied from `geappliances/home-assistant-bridge` to avoid pulling in Arduino-only source files |
| `esphome_uart_adapter.h` *(new)* | Polling-based `i_tiny_uart_t` adapter for `uart::UARTComponent`; removed redundant `extern "C"` guards (function uses C++ types) |
| `esphome_uart_adapter.cpp` *(new)* | Implementation; **poll period changed 1→0** to match reference and prevent inter-byte gap misfire at 19200 baud |
| `esphome_time_source.h` *(new)* | `i_tiny_time_source_t` adapter header |
| `esphome_time_source.cpp` *(new)* | Implementation wrapping `esphome::millis()` |
| `ApplianceErds.cpp` | Add `static` to `smallApplianceErdCount` and `energyErds` to prevent external linkage conflicts |
| `Gea2MqttBridge.h` | Unchanged — already a pure C-compatible header |
