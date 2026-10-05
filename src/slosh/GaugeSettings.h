#pragma once

#include <cstdint>
#include <initializer_list>

// Diameter of the round display in pixels.
constexpr int DISPLAY_SIZE = 466;

// Everything that shapes how the gauge looks and how much work it costs.
// The tuner edits these and exports them as TunedSettings.h. A new field also goes in
// tuner/bindings.cpp and, in the same order as here, the tuner's FIELDS list.
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

// False for settings that would hang or crash the simulation or renderer, or overflow its sizes.
constexpr bool settingsValid(const GaugeSettings& settings) {
  auto finite = [](float value) { return value - value == 0.0f; };
  for (float value : {settings.fullChargeFill, settings.tankDiameter, settings.gravityScale, settings.particleRadiusRatio,
                      settings.overRelaxation, settings.flipRatio, settings.driftCompensation, settings.surfaceThreshold,
                      settings.surfaceSoftness, settings.rimWidth, settings.glowStrength, settings.targetFrameRate}) {
    if (!finite(value)) return false;
  }
  return settings.fullChargeFill > 0.0f && settings.fullChargeFill <= 10.0f && settings.tankDiameter > 0.0f &&
         settings.gridResolution >= 2 && settings.gridResolution <= 256 && settings.particleRadiusRatio >= 0.1f &&
         settings.particleRadiusRatio <= 0.5f && settings.substeps >= 1 && settings.pressureIterations >= 0 &&
         settings.separationIterations >= 0 && settings.densityResolution >= 2 && settings.densityResolution <= 256 &&
         settings.smoothingPasses >= 0 && settings.targetFrameRate > 0.0f;
}
