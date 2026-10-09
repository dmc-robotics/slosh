#include "PerformanceModel.h"

#include <algorithm>

#include "FluidSimulation.h"

namespace {

// Seconds per unit of work on the ESP32-S3 at 240 MHz, scaled to match the firmware's timings
// for the 2026-10-05 tuned settings with 432 particles (simulation 8.95 ms, render 18.3 ms,
// transfer 26.2 ms for whole frames, corners included). One measurement can't separate the
// simulation's costs, so they keep their first-guess proportions.
constexpr float PARTICLE_STEP_COST = 8.4e-6f;        // integrate, walls, grid transfers, density
constexpr float PARTICLE_SEPARATION_COST = 8.4e-6f;  // per particle per separation iteration
constexpr float CELL_PRESSURE_COST = 0.45e-6f;       // per grid cell per pressure iteration
constexpr float PARTICLE_SPLAT_COST = 0.2e-6f;
constexpr float DENSITY_CELL_SMOOTH_COST = 0.1e-6f;  // per density cell per smoothing pass
constexpr float DENSITY_CELL_WALL_COST = 0.1e-6f;    // per density cell, extending the liquid past the wall
constexpr float PIXEL_COST = 0.107e-6f;

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
  int sentPixels = 0;
  for (int row = 0; row < outputSize; row += STRIP_HEIGHT) {
    int rows = std::min(STRIP_HEIGHT, outputSize - row);
    ColumnSpan sent = stripColumns(row, rows, outputSize);
    sentPixels += (sent.end - sent.first) * rows;
  }

  PerformanceEstimate estimate{};
  estimate.simulationTime = settings.substeps * (particles * (PARTICLE_STEP_COST + settings.separationIterations * PARTICLE_SEPARATION_COST) +
                                                 gridCells * settings.pressureIterations * CELL_PRESSURE_COST);
  estimate.renderTime = particles * PARTICLE_SPLAT_COST +
                        densityCells * (DENSITY_CELL_WALL_COST + settings.smoothingPasses * DENSITY_CELL_SMOOTH_COST) +
                        litPixels * PIXEL_COST;
  estimate.transferTime = sentPixels * 2.0f / DISPLAY_BYTES_PER_SECOND;
  // The display can only run ahead of core 1 by the queued strips, so the simulation hides at most
  // that much of the transfer.
  float queuedTransferTime = estimate.transferTime * STRIP_COUNT * STRIP_HEIGHT / outputSize;
  float displayTime = estimate.transferTime + std::max(estimate.simulationTime - queuedTransferTime, 0.0f);
  estimate.frameTime = std::max(estimate.simulationTime + estimate.renderTime, displayTime);
  estimate.frameRate = 1.0f / estimate.frameTime;
  estimate.withinFrameBudget = estimate.frameRate >= settings.targetFrameRate;

  // The renderer keeps the density field, a scratch copy and one row.
  int rendererMemory = (2 * settings.densityResolution + 1) * settings.densityResolution * sizeof(float);
  estimate.memory = simulationMemory(settings) + rendererMemory;
  estimate.withinMemoryBudget = estimate.memory <= MEMORY_BUDGET;
  return estimate;
}
