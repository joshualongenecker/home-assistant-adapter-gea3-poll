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

The `poll` callback fires on every timer tick, drains all bytes from
`uart->read_byte()` into `receive_event`, and fires `send_complete_event`
after each transmitted byte — exactly what `i_tiny_uart_t` requires.

Usage in the component:

```cpp
// geappliances_bridge.cpp
esphome_uart_adapter_init(&uart_adapter_, &timer_group_, this);
//                                                         ^
//  'this' IS a uart::UARTComponent (GEAppliancesBridgeComponent
//  extends uart::UARTDevice which delegates to UARTComponent)
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

## 6. ESPHome component Python layer (`__init__.py`)

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

## 7. Example YAML configuration

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

## 8. Summary of all changed / new files

| File | Change |
|------|--------|
| `__init__.py` | Replace `home-assistant-bridge` PlatformIO ref with two GitHub URLs |
| `geappliances_bridge.h` | Remove `ESPHomeUARTStream`, `<Stream.h>`, `tiny_uart_adapter.hpp`; add `esphome_uart_adapter.h`, `esphome_time_source.h` |
| `geappliances_bridge.cpp` | Use `esphome_uart_adapter_init` + `esphome_time_source_init`; remove Arduino-specific UART stream setup |
| `Gea2MqttBridge.cpp` | Remove `Arduino.h`, `Preferences.h`, `String`, `Serial`, NV storage; replace with `ESP_LOGI`, `snprintf`, `esp_system.h` |
| `esphome_mqtt_client_adapter.h` | Add `extern "C"` guards; use `typedef struct … _t` naming |
| `esphome_mqtt_client_adapter.cpp` | Add `<cctype>` include; mark init/notify functions `extern "C"` |
| `esphome_uart_adapter.h` *(new)* | Polling-based `i_tiny_uart_t` adapter for `uart::UARTComponent` |
| `esphome_uart_adapter.cpp` *(new)* | Implementation of the polling UART adapter |
| `esphome_time_source.h` *(new)* | `i_tiny_time_source_t` adapter header |
| `esphome_time_source.cpp` *(new)* | Implementation wrapping `esphome::millis()` |
| `ApplianceErds.h/.cpp` | Unchanged — pure C/C++, no Arduino dependencies |
| `Gea2MqttBridge.h` | Unchanged — already a pure C-compatible header |
