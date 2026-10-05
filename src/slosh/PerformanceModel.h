#pragma once

#include "GaugeSettings.h"

// Estimated ESP32-S3 cost of one frame with a full tank, in seconds.
struct PerformanceEstimate {
  float simulationTime;
  float renderTime;
  float transferTime;
  float frameTime;
  float frameRate;
  bool withinBudget;  // sustains settings.targetFrameRate
};

PerformanceEstimate estimatePerformance(const GaugeSettings& settings, int outputSize);
