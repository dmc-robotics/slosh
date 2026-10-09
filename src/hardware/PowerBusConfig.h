#pragma once

#include <cstdint>

// The robot's LiPo power bus. A data provider (an Arduino, say) measures it and sends the
// voltage to BUS_DATA_PIN as text, one reading in volts per line: "11.84\n".
constexpr int CELL_COUNT = 3;
constexpr uint32_t BUS_DATA_BAUD_RATE = 9600;

// With no reading for this long the input is treated as unplugged and the gauge shows BENCH_FILL_LEVEL.
constexpr float SIGNAL_TIMEOUT = 3.0f;  // s
// Readings outside this range per cell can't come from a LiPo pack, so they're dropped as garbled.
constexpr float MINIMUM_CELL_VOLTAGE = 2.5f;  // V
constexpr float MAXIMUM_CELL_VOLTAGE = 4.4f;  // V
constexpr float BENCH_FILL_LEVEL = 1.0f;

// Load sag makes the voltage jumpy, so it is low-pass filtered.
constexpr float VOLTAGE_FILTER_TIME_CONSTANT = 2.0f;  // s
