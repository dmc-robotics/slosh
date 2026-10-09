#pragma once

#include <cstdint>

#include "GaugeSettings.h"

// The CO5300 takes 16-bit pixels over a 4-bit-wide QSPI bus at this clock.
constexpr int32_t DISPLAY_BUS_FREQUENCY = 40000000;  // Hz

// The firmware streams each frame as strips of rows. Core 1 renders them while core 0 sends them to
// the display, and strips still queued at the end of a frame keep the display busy while core 1
// runs the next simulation step.
constexpr int STRIP_HEIGHT = 16;  // rows; the CO5300 wants even row windows
constexpr int STRIP_COUNT = 6;

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
