#pragma once

#include "GaugeSettings.h"

// Exported from the Slosh tuner on 2026-10-05.
inline constexpr GaugeSettings TUNED_SETTINGS{
    .fullChargeFill = 1.4f,
    .tankDiameter = 1.21f,
    .gravityScale = 1.85f,
    .gridResolution = 20,
    .particleRadiusRatio = 0.4f,
    .substeps = 1,
    .pressureIterations = 8,
    .separationIterations = 1,
    .overRelaxation = 1.0f,
    .flipRatio = 0.69f,
    .driftCompensation = 0.5f,
    .densityResolution = 16,
    .smoothingPasses = 1,
    .surfaceThreshold = 0.79f,
    .surfaceSoftness = 0.02f,
    .rimWidth = 0.42f,
    .glowStrength = 0.61f,
    .coreColor = 0x1BE7A0,
    .rimColor = 0xB8FFE6,
    .glowColor = 0x0E5A44,
    .targetFrameRate = 30.0f,
};
