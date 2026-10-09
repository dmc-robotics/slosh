#include "PerformanceModel.h"

#include <algorithm>
#include <cmath>

#include "FluidSimulation.h"

namespace {

// Seconds per unit of work on the ESP32-S3 at 240 MHz with -O2, fitted to the firmware's timings
// for the 2026-10-05 tuned settings with a full tank of 720 particles (2026-10-08). The simulation
// costs come from its per-phase timings while the board was being moved (integrate 0.10 ms,
// separate 4.2, walls 0.12, to grid 1.51, density 0.41, pressure 1.12, from grid 1.63). Render:
// density field 1.44 ms, 9.7 ms in all, whose split between preparation costs keeps its
// first-guess proportions. Transfer 21.4 ms. Separation is costed at a full tank's packing under
// strong gravity; looser liquid has fewer overlaps to resolve.
constexpr float PARTICLE_STEP_COST = 5.2e-6f;        // integrate, walls, grid transfers, density
constexpr float PARTICLE_SEPARATION_COST = 5.8e-6f;  // per particle per separation iteration
constexpr float CELL_PRESSURE_COST = 0.29e-6f;       // per grid cell per pressure iteration
constexpr float PARTICLE_SPLAT_COST = 1.7e-6f;
constexpr float DENSITY_CELL_SMOOTH_COST = 0.4e-6f;  // per density cell per smoothing pass
constexpr float DENSITY_CELL_WALL_COST = 0.4e-6f;    // per density cell, extending the liquid past the wall
constexpr float PIXEL_COST = 0.0485e-6f;
// Untimed work on core 1 each frame: reading the accelerometer and fuel sensor, the loop itself.
constexpr float FRAME_OVERHEAD = 1.3e-3f;

// Measured; the bus itself peaks at DISPLAY_BUS_FREQUENCY * 4 bits.
constexpr float DISPLAY_BYTES_PER_SECOND = 16.6e6f;

}  // namespace

PerformanceEstimate estimatePerformance(const GaugeSettings& settings, int outputSize) {
  float particles = static_cast<float>(fullTankParticleCount(settings));
  float gridCells = static_cast<float>((settings.gridResolution + 2) * (settings.gridResolution + 2));
  float densityCells = static_cast<float>(settings.densityResolution * settings.densityResolution);
  int litPixels = 0;
  for (int row = 0; row < outputSize; row++) {
    ColumnSpan lit = litColumns(row, outputSize);
    litPixels += lit.end - lit.first;
  }
  int sentPixels = 0, queuedSentPixels = 0, queuedLitPixels = 0;
  for (int strip = 0, row = 0; row < outputSize; strip++, row += STRIP_HEIGHT) {
    int rows = std::min(STRIP_HEIGHT, outputSize - row);
    ColumnSpan sent = stripColumns(row, rows, outputSize);
    sentPixels += (sent.end - sent.first) * rows;
    if (strip >= STRIP_COUNT) continue;
    queuedSentPixels += (sent.end - sent.first) * rows;
    for (int stripRow = row; stripRow < row + rows; stripRow++) {
      ColumnSpan lit = litColumns(stripRow, outputSize);
      queuedLitPixels += lit.end - lit.first;
    }
  }

  PerformanceEstimate estimate{};
  estimate.simulationTime = settings.substeps * (particles * (PARTICLE_STEP_COST + settings.separationIterations * PARTICLE_SEPARATION_COST) +
                                                 gridCells * settings.pressureIterations * CELL_PRESSURE_COST);
  float prepareTime = particles * PARTICLE_SPLAT_COST +
                      densityCells * (DENSITY_CELL_WALL_COST + settings.smoothingPasses * DENSITY_CELL_SMOOTH_COST);
  estimate.renderTime = prepareTime + litPixels * PIXEL_COST;
  estimate.transferTime = sentPixels * 2.0f / DISPLAY_BYTES_PER_SECOND;
  // The display starts a frame only as a panel refresh begins, and core 1 can only render the
  // queued strips ahead of it. The rest of the frame's rendering waits on the write, so core 1's
  // next simulation step, and rendering the next frame's queued strips, have to fit after it. The
  // top and bottom strips are alike, so the queued strips stand in for the last ones too.
  float queuedTransferTime = queuedSentPixels * 2.0f / DISPLAY_BYTES_PER_SECOND;
  float syncedCycle = estimate.transferTime - queuedTransferTime + FRAME_OVERHEAD + estimate.simulationTime +
                      prepareTime + queuedLitPixels * PIXEL_COST;
  float cycle = std::max({FRAME_OVERHEAD + estimate.simulationTime + estimate.renderTime, estimate.transferTime, syncedCycle});
  int refreshes = std::max(refreshesPerFrame(settings.targetFrameRate), static_cast<int>(std::ceil(cycle * PANEL_REFRESH_RATE)));
  estimate.frameTime = refreshes / PANEL_REFRESH_RATE;
  estimate.frameRate = 1.0f / estimate.frameTime;
  // A percent of slack for the refresh rate not dividing evenly into the target.
  estimate.withinFrameBudget = estimate.frameRate >= 0.99f * settings.targetFrameRate;

  // The renderer keeps the density field, a scratch copy and one row.
  int rendererMemory = (2 * settings.densityResolution + 1) * settings.densityResolution * sizeof(float);
  estimate.memory = simulationMemory(settings) + rendererMemory;
  estimate.withinMemoryBudget = estimate.memory <= MEMORY_BUDGET;
  return estimate;
}
