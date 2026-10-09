# Slosh

A fuel gauge for robots. A glowing liquid sloshes around a round AMOLED display as the robot moves, and its level follows the charge left in the robot's LiPo battery.

Runs on the [Waveshare ESP32-S3-Touch-AMOLED-1.75](https://www.waveshare.com/esp32-s3-touch-amoled-1.75.htm). A browser tuner runs the same simulation so you can set the look before flashing.

## Layout

| Path | What |
|---|---|
| `slosh.ino` | Firmware main loop: read the sensors, step, render in strips, report timings |
| `src/slosh/` | Shared core, plain C++ used by both the firmware and the tuner: FLIP simulation, renderer, display strips, ESP32 cost model, LiPo fuel level, `TunedSettings.h` |
| `src/hardware/` | Firmware only: pins, power bus config, accelerometer and bus voltage input |
| `tuner/` | Browser tuner: `index.html` plus the core compiled to WebAssembly |
| `test/` | Host check of the shared core |

## Tuner

```zsh
brew install emscripten   # once
tuner/build.sh            # after changing anything in src/slosh/
open tuner/index.html
```

Drag the gauge to shake it; a drag stands for moving the real 1.75″ display that far. Tilt, rocking and fuel level are under **Test conditions**. The tuner steps the liquid at the target frame rate, as the firmware does, so it moves the same on both. The budget panel estimates whether the ESP32 can keep up and whether the settings fit in its memory. When it looks right, **Export TunedSettings.h** and replace `src/slosh/TunedSettings.h` with it.

## Firmware

Needs the esp32 core 3.1.x plus these libraries:

```zsh
arduino-cli lib install "GFX Library for Arduino@1.6.4" "SensorLib@0.3.1"
grot build
grot load
```

`.grotconfig` is local and gitignored, since it holds this machine's serial port. Create it with `grot init`, then set `fqbn = "esp32:esp32:esp32s3"`, `port` and `sketch_path = "."` under `[basic]`.

The QMI8658 accelerometer is mounted a quarter turn from the display: its +x axis points to the display's top and its +y axis to the right (checked on the board; mapped in `src/hardware/MotionSensor.cpp`). The display's image is turned too: with the USB port pointing down, its bottom edge is on your right. The liquid follows real gravity either way.

Over USB serial the firmware prints `fps`, per-stage timings, particle count, free heap, gravity, bus voltage and fill level once a second as `key:value` lines that Gremlin plots. Use them to calibrate the constants in `src/slosh/PerformanceModel.cpp`. The model costs a full tank, so divide by `particles` when fitting the per-particle constants. Fit the display transfer and per-pixel costs first, since they dominate the frame. If the device can't reach the target frame rate, the liquid plays in slow motion.

## Wiring

The gauge doesn't measure the bus itself. A data provider, such as an Arduino on the robot, measures the bus voltage and sends it over UART at 9600 baud as text: one reading in volts per line, at least once a second.

```cpp
void setup() { Serial.begin(9600); }

void loop() {
  Serial.println(busVoltage(), 2);  // however the provider measures it, e.g. "11.84"
  delay(250);
}
```

Connect the provider's TX to GPIO16 (expansion header pin 8), and connect the grounds. ESP32 pins take 3.3 V at most, so from a 5 V board such as an Uno, drop TX through a divider: 1 kΩ from TX to the pin and 2 kΩ from the pin to ground. Set the cell count in `src/hardware/PowerBusConfig.h`. If no reading arrives for 3 s, the gauge shows a fixed bench level.

## Test

```zsh
c++ -std=c++20 -O2 -Isrc src/slosh/*.cpp test/simulation_test.cpp -o /tmp/simulation_test && /tmp/simulation_test
```
