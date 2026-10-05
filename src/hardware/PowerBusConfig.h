#pragma once

// The robot's LiPo power bus, measured through a resistor divider into BUS_VOLTAGE_PIN.
// 100k/20k divides by 6, which keeps up to 4 full cells (16.8 V) under the pin's limit.
constexpr int CELL_COUNT = 3;
constexpr float DIVIDER_TOP_RESISTANCE = 100000.0f;   // Ω, bus to pin
constexpr float DIVIDER_BOTTOM_RESISTANCE = 20000.0f; // Ω, pin to ground

constexpr float FULL_CELL_VOLTAGE = 4.2f;    // V
constexpr float MAXIMUM_PIN_VOLTAGE = 3.1f;  // V, top of the ESP32-S3 ADC range at 11 dB
static_assert(CELL_COUNT * FULL_CELL_VOLTAGE * DIVIDER_BOTTOM_RESISTANCE / (DIVIDER_TOP_RESISTANCE + DIVIDER_BOTTOM_RESISTANCE) <=
                  MAXIMUM_PIN_VOLTAGE,
              "A full pack would overdrive BUS_VOLTAGE_PIN: raise DIVIDER_TOP_RESISTANCE");

// Load sag makes the raw voltage jumpy, so it is low-pass filtered.
constexpr float VOLTAGE_FILTER_TIME_CONSTANT = 2.0f; // s

// Below this the input is treated as unplugged and the gauge shows BENCH_FILL_LEVEL.
constexpr float MINIMUM_CONNECTED_CELL_VOLTAGE = 2.5f; // V
constexpr float BENCH_FILL_LEVEL = 0.6f;
