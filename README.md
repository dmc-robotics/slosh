# Slosh

A fuel gauge for robots. A glowing liquid sloshes around a round AMOLED display as the robot moves, and its level follows the charge left in the robot's LiPo battery.

Runs on the [Waveshare ESP32-S3-Touch-AMOLED-1.75](https://www.waveshare.com/esp32-s3-touch-amoled-1.75.htm). A browser tuner runs the same simulation so you can set the look before flashing.

## Layout

| Path | What |
|---|---|
| `slosh.ino` | Firmware main loop: read the sensors, step, render in strips, report timings |
| `src/slosh/` | Shared core, plain C++ used by both the firmware and the tuner: FLIP simulation, renderer, ESP32 cost model, LiPo fuel level, `TunedSettings.h` |
| `src/hardware/` | Firmware only: pins, power bus config, accelerometer and bus voltage sensors |
| `tuner/` | Browser tuner: `index.html` plus the core compiled to WebAssembly |
| `test/` | Host check of the shared core |

## Tuner

```zsh
brew install emscripten   # once
tuner/build.sh            # after changing anything in src/slosh/
open tuner/index.html
```

Drag the gauge to shake it. Tilt, rocking and fuel level are under **Test conditions**. The frame budget panel estimates whether the ESP32 can keep up. When it looks right, **Export TunedSettings.h** and replace `src/slosh/TunedSettings.h` with it.

## Firmware

Needs the esp32 core 3.1.x plus these libraries:

```zsh
arduino-cli lib install "GFX Library for Arduino@1.6.4" "SensorLib@0.3.1"
grot build
grot load
```

Over USB serial the firmware prints `fps`, per-stage timings, bus voltage and fill level once a second as `key:value` lines that Gremlin plots. Use the timings to calibrate the constants in `src/slosh/PerformanceModel.cpp`.

## Wiring

Feed the robot's power bus through a resistor divider into GPIO16 (expansion header pin 8), and connect the grounds. The default 100 kΩ / 20 kΩ divider suits up to a 4-cell pack (16.8 V). A larger pack needs a bigger top resistor, and the build fails if a full pack would put more than 3.1 V on the pin. For extra protection, add a 1 kΩ series resistor and a 3.3 V clamp diode at the pin. Set the cell count and resistor values in `src/hardware/PowerBusConfig.h`. With nothing connected, the gauge shows a fixed bench level.

## Test

```zsh
c++ -std=c++20 -O2 -Isrc src/slosh/*.cpp test/simulation_test.cpp -o /tmp/simulation_test && /tmp/simulation_test
```
