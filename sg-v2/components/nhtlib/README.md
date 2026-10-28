# nhtlib

A small bundle of reusable ESP-IDF components for unattended IoT devices. Each
component is independent, documented in Javadoc/Doxygen style, and knows nothing
about any particular product. They were extracted from a smart-irrigation
controller but contain no irrigation logic.

Target: ESP-IDF 5.x, any ESP32 family chip (tested on ESP32-C3). License: MIT.

## Design rules

* **Time is a parameter.** The logic components (`nht_actuator`, `nht_valve`,
  `nht_dinput`, `nht_sched`) never read a clock. You pass `now_ms`, so a
  multi-hour scenario runs as a unit test on a PC in milliseconds.
* **Event-driven, not polled.** The state machines tell you when they next need
  attention (`nht_*_next_wake_ms()`), so a task can sleep instead of ticking.
* **Nothing blocks the caller.** Network calls are queued or run in their own
  task; losing the network never stalls control logic.
* **Fail safe.** Hard ceilings, safety inhibits, and "close on start-up" are part
  of the components, not left to the application.
* **Layering is enforced.** `nht_valve` depends on `nht_actuator`; every other
  component is standalone. A product's own code lives outside `nhtlib` and is
  never referenced from it.

## Components

| Component | What it does | Needs |
|---|---|---|
| [`nht_actuator`](nht_actuator/include/nht_actuator.h) | A stateful output (relay, MOSFET, virtual): auto-off timer, turn-on delay, minimum on/off times, safety inhibit, event callbacks. Duplicate commands are harmless. | – |
| [`nht_valve`](nht_valve/include/nht_valve.h) | Motorised ball valve or normally-closed solenoid behind one API, with optional pump and master-AC relays. Hard run-time ceiling, closes at start-up, never reverses the motor under power. | `nht_actuator` |
| [`nht_dinput`](nht_dinput/include/nht_dinput.h) | Discrete input for noisy two-state sensors: Schmitt trigger plus asymmetric hold times (fast to alarm, slow to clear). Also a median helper. | – |
| [`nht_sched`](nht_sched/include/nht_sched.h) | Time-of-day scheduler with a catch-up window (a late loop does not lose the day's run) and an application-defined payload. | – |
| [`nht_store`](nht_store/include/nht_store.h) | Key-value settings on NVS. Getters always take a default, so a blank NVS works. | `nvs_flash` |
| [`nht_sensor`](nht_sensor/include/nht_adc.h) | `nht_adc`: one-shot ADC with calibration, median filter and shared handles. `nht_pulse`: 64-bit pulse counter for flow/energy meters (PCNT or GPIO interrupt, chosen per chip). | – |
| [`nht_net`](nht_net/include/nht_net.h) | WiFi station with gradual reconnect, strongest-AP selection on mesh networks, SoftAP + captive-portal fallback, SNTP. | – |
| [`nht_webkit`](nht_webkit/include/nht_webkit.h) | HTTP + WebSocket server helpers and a LAN OTA endpoint that streams firmware straight to the OTA partition. | – |
| [`nht_fbrtdb`](nht_fbrtdb/include/nht_fbrtdb.h) | Firebase Realtime Database client: sign-in, token refresh, REST CRUD, SSE stream with 307-redirect handling. Includes a pure SSE parser (`nht_sse`). | – |

## Using nhtlib in a project

Copy or symlink this folder into your project (for example to
`components/nhtlib`) and tell ESP-IDF where the components are. In the project's
top-level `CMakeLists.txt`, **before** `include(project.cmake)`:

```cmake
list(APPEND EXTRA_COMPONENT_DIRS ${CMAKE_CURRENT_LIST_DIR}/components/nhtlib)
```

(`list(APPEND ...)` rather than `set(...)` matters under PlatformIO, which passes
its own `EXTRA_COMPONENT_DIRS` on the command line.) Then require what you use:

```cmake
idf_component_register(SRCS "main.c" REQUIRES nht_valve nht_net)
```

## Examples

Each example is a complete ESP-IDF project in [`examples/`](examples). Build one
with `idf.py` or let the repository check them all:

```
cd examples/valve && idf.py set-target esp32c3 build
python3 ../../../tools/check_examples.py      # builds every example
```

| Example | Shows |
|---|---|
| [`actuator`](examples/actuator/main/main.c) | A relay that switches itself off, with an event-driven loop |
| [`valve`](examples/valve/main/main.c) | Opening a ball valve for a fixed time |
| [`dinput`](examples/dinput/main/main.c) | A debounced float switch with asymmetric hold times |
| [`sched`](examples/sched/main/main.c) | A daily task with a payload and catch-up window |
| [`store`](examples/store/main/main.c) | A boot counter and a string setting in NVS |
| [`sensor`](examples/sensor/main/main.c) | Reading a divider voltage and counting flow pulses |
| [`net`](examples/net/main/main.c) | Joining WiFi, AP fallback, SNTP |
| [`webkit`](examples/webkit/main/main.c) | A JSON endpoint, WebSocket broadcast and OTA |
| [`fbrtdb`](examples/fbrtdb/main/main.c) | Sign in, listen to `/desired`, report to `/reported` |

## Unit tests

The pure-logic components compile on a PC and are tested with Unity. In the
product repository: `pio test -e native` (the tests live in `test/`).

## API documentation

Every public header uses Javadoc-style Doxygen comments (`@file`, `@brief`,
`@param`, `@return`). Generate HTML with:

```
cd components/nhtlib && doxygen Doxyfile      # output in docs/html
```

## Conventions

* `@param[out]` marks values written through a pointer; `@return` states what a
  failure looks like.
* Public symbols are prefixed `nht_<component>_`; types end in `_t`.
* Structs are public only so they can live on the stack; treat their runtime
  fields as read-only and use the API.
