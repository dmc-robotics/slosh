#pragma once

#include <cstdint>

#include "DisplayStrips.h"
#include "GaugeSettings.h"

// The CO5300 takes 16-bit pixels over a 4-bit-wide QSPI bus at this clock.
constexpr int32_t DISPLAY_BUS_FREQUENCY = 40000000;  // Hz

// Heap the gauge may use on an ESP32-S3 without PSRAM. A first guess; check it against the
// free_heap the firmware prints over serial.
constexpr int MEMORY_BUDGET = 200000;  // bytes

// Estimated ESP32-S3 cost of one frame with a full tank, in seconds, and peak heap use.
struct PerformanceEstimate {
  float simulationTime;  // core 1
  float renderTime;      // core 1
  float transferTime;    // core 0, alongside the other two
  float frameTime;
  float frameRate;
  bool withinFrameBudget;  // sustains settings.targetFrameRate
  int memory;              // bytes
  bool withinMemoryBudget;
};

PerformanceEstimate estimatePerformance(const GaugeSettings& settings, int outputSize);
