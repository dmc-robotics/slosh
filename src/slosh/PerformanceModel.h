#pragma once

#include <cstdint>

#include "DisplayStrips.h"
#include "GaugeSettings.h"

// The CO5300 takes 16-bit pixels over a 4-bit-wide QSPI bus at this clock.
constexpr int32_t DISPLAY_BUS_FREQUENCY = 40000000;  // Hz

// Heap the simulation and renderer may use on an ESP32-S3 without PSRAM, after the strip buffers.
// About 193 KB is free when they allocate (free_heap plus their own use); this keeps a margin.
constexpr int MEMORY_BUDGET = 170000;  // bytes

// Estimated ESP32-S3 cost of one frame with a full tank, in seconds, and peak heap use.
struct PerformanceEstimate {
  float simulationTime;  // core 1
  float renderTime;      // core 1, including preparing the density field
  float transferTime;    // core 0, alongside the other two
  float frameTime;
  float frameRate;
  bool withinFrameBudget;  // sustains settings.targetFrameRate
  int memory;              // bytes
  bool withinMemoryBudget;
};

PerformanceEstimate estimatePerformance(const GaugeSettings& settings, int outputSize);
