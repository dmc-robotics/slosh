# Slosh

Slosh is a "fuel gage" for robotics projects. It uses a Waveshare display to show a level of simulated "fuel" that's proportional to the remaining capacity of a LiPo battery. It's a general gage, no tied to any specific project. It has a companion HTML app to help the user fine tune the appearance and performance of the simulated fuel.

## Hardware

 Waveshare ESP32-S3 1.75inch AMOLED Round Touch Display Development Board, 32-bit LX7 Dual-core Processor, 466×466 Pixels, QSPI Interface, Onboard Dual Microphone Array, ESP32 With Display

 Part number ESP32-S3-Touch-AMOLED-1.75

 Store: https://www.waveshare.com/esp32-s3-touch-amoled-1.75.htm

 Docs: https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-1.75


 ## Simulation

 The fuel gage uses a FLIP fluid simulation to mimick a glowing liquid sloshing about on the display based on the built in accelerometer readings.

 ## Connections

 The fuel gage will have power and data connections. Power is from a suitable source for hte display The data is the voltage from the full power bus being measured.

 ## Visual Tuning

 Getting the appearance of the sloshing correct requires human taste. In order to set the parameters, a tuning app is requried. This app is a static HTML page that can run in a browser. It will display the simulated fuel gage and have various settings that impact appearance and performane. The app will warn the user if the settings are too much for the ESP32 to handle. Once hte look is right, the settings can be exported.

 ## Tooling

 This app will be developed with arduino-xli wrapped with `grot` and Grrmlin (see ~/code/robotics for details).

## Structure

- `src/slosh/`: shared core (simulation, renderer, cost model, settings, fuel level). Plain C++20, no Arduino or Emscripten includes: it compiles into both the firmware and the WASM tuner, so what is tuned is what runs.
- `src/hardware/`: firmware-only sensor and pin code. `slosh.ino`: main loop.
- `tuner/`: `index.html` and `bindings.cpp`; `build.sh` produces `tuner/slosh.js` (gitignored, WASM embedded so the page opens from `file://`).
- A new `GaugeSettings` field also goes in the embind fields in `tuner/bindings.cpp` and in `FIELDS` in `tuner/index.html`, in struct order (the export writes designated initializers). A `static_assert` in `bindings.cpp` and a startup check in the page catch omissions.
- Arduino compiles everything under `src/` recursively, so keep tuner and test code out of it.

## Commands

```zsh
grot build                                   # firmware; FQBN esp32:esp32:esp32s3, no PSRAM needed
tuner/build.sh                               # WASM tuner (needs emscripten)
c++ -std=c++20 -O2 -Isrc src/slosh/*.cpp test/simulation_test.cpp -o /tmp/simulation_test && /tmp/simulation_test
```

## Code Style
- **Naming:** no abbreviations (`publisher`, not `pub`), in every language.
- **C++:** `PascalCase` classes, `camelCase` methods, `_camelCase` private members, `UPPER_SNAKE_CASE` constants.

## General Rules
- **Keep it simple.** Solo hobby project: prefer the simplest thing that works while maintaining good architectural practices; no process or tooling for its own sake. Beware of YAGNI.
- **Hardware safety:** never flash firmware or command motor motion without the user's explicit go-ahead in the current conversation.
- **Backwards Compatibility:** Do not support unless specifically requested.- **Git:** the user handles commits and pushes unless they explicitly ask Claude to. When asked to commit, keep the message short and put the model name in parentheses at the end, e.g. `Add balance card (Opus 5.5)`. No "Co-Authored-By" or "Generated with Claude Code" lines. Commit on `main`; don't push unless asked.
- **Comments:** Use only when necessary to explain code - no need for ceremonial boilerpate.
- **Units:** SI units. Use radians internally, degrees can be used when human facing when appropriate.