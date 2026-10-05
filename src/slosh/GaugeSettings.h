#pragma once

#include <cstdint>

// Diameter of the round display in pixels.
constexpr int DISPLAY_SIZE = 466;

// Everything that shapes how the gauge looks and how much work it costs.
// The tuner edits these and exports them as TunedSettings.h. Keep the field order
// in sync with the tuner's FIELDS list, which writes them as designated initializers.
struct GaugeSettings {
  // Level
  float fullChargeFill;      // liquid at 100% charge relative to a tightly packed full tank;
                             // above 1 makes up for the liquid compressing under its own weight

  // Physics
  float tankDiameter;        // m; a bigger virtual tank sloshes more slowly
  float gravityScale;
  int gridResolution;        // pressure grid cells across the tank
  float particleRadiusRatio; // particle radius as a fraction of a grid cell
  int substeps;
  int pressureIterations;
  int separationIterations;
  float overRelaxation;
  float flipRatio;           // 0 = PIC (smooth, viscous), 1 = FLIP (lively, noisy)
  float driftCompensation;   // pushes apart over-dense regions so volume is kept

  // Appearance
  int densityResolution;     // density field cells across the tank
  int smoothingPasses;
  float surfaceThreshold;    // density (1 = at rest) where the surface is drawn
  float surfaceSoftness;
  float rimWidth;
  float glowStrength;
  uint32_t coreColor;        // 0xRRGGBB
  uint32_t rimColor;
  uint32_t glowColor;

  // Performance
  float targetFrameRate;     // frames per second the ESP32 should sustain
};
