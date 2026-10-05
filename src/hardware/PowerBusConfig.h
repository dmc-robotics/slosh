#pragma once

// The robot's LiPo power bus, measured through a resistor divider into BUS_VOLTAGE_PIN.
// Keep the pin under 3.1 V: 100k/20k divides by 6, which suits up to 4 cells.
constexpr int CELL_COUNT = 3;
constexpr float DIVIDER_TOP_RESISTANCE = 100000.0f;   // Ω, bus to pin
constexpr float DIVIDER_BOTTOM_RESISTANCE = 20000.0f; // Ω, pin to ground

// Load sag makes the raw voltage jumpy, so it is low-pass filtered.
constexpr float VOLTAGE_FILTER_TIME_CONSTANT = 2.0f; // s

// Below this the input is treated as unplugged and the gauge shows BENCH_FILL_LEVEL.
constexpr float MINIMUM_CONNECTED_CELL_VOLTAGE = 2.5f; // V
constexpr float BENCH_FILL_LEVEL = 0.6f;
