#include "PerformanceModel.h"

#include "FluidSimulation.h"

namespace {

// Seconds per unit of work on the ESP32-S3 at 240 MHz. These are first guesses;
// replace them with the per-stage timings the firmware prints over serial.
constexpr float PARTICLE_STEP_COST = 1.5e-6f;       // integrate, walls, grid transfers, density
constexpr float PARTICLE_SEPARATION_COST = 1.5e-6f; // per particle per separation iteration
constexpr float CELL_PRESSURE_COST = 0.08e-6f;      // per grid cell per pressure iteration
constexpr float PARTICLE_SPLAT_COST = 0.2e-6f;
constexpr float DENSITY_CELL_SMOOTH_COST = 0.1e-6f;  // per density cell per smoothing pass
constexpr float DENSITY_CELL_WALL_COST = 0.1e-6f;    // per density cell, extending the liquid past the wall
constexpr float PIXEL_COST = 0.06e-6f;

constexpr float DISPLAY_BYTES_PER_SECOND = DISPLAY_BUS_FREQUENCY * 4.0f / 8.0f;
constexpr float CIRCLE_AREA_FRACTION = 0.785398f;

}  // namespace

PerformanceEstimate estimatePerformance(const GaugeSettings& settings, int outputSize) {
  float particles = static_cast<float>(fullTankParticleCount(settings));
  float gridCells = static_cast<float>((settings.gridResolution + 2) * (settings.gridResolution + 2));
  float densityCells = static_cast<float>(settings.densityResolution * settings.densityResolution);
  float pixels = static_cast<float>(outputSize * outputSize);

  PerformanceEstimate estimate{};
  estimate.simulationTime = settings.substeps * (particles * (PARTICLE_STEP_COST + settings.separationIterations * PARTICLE_SEPARATION_COST) +
                                                 gridCells * settings.pressureIterations * CELL_PRESSURE_COST);
  estimate.renderTime = particles * PARTICLE_SPLAT_COST +
                        densityCells * (DENSITY_CELL_WALL_COST + settings.smoothingPasses * DENSITY_CELL_SMOOTH_COST) +
                        pixels * CIRCLE_AREA_FRACTION * PIXEL_COST;
  estimate.transferTime = pixels * 2.0f / DISPLAY_BYTES_PER_SECOND;
  estimate.frameTime = estimate.simulationTime + estimate.renderTime + estimate.transferTime;
  estimate.frameRate = 1.0f / estimate.frameTime;
  estimate.withinFrameBudget = estimate.frameRate >= settings.targetFrameRate;

  // The renderer keeps the density field, a scratch copy and one row.
  int rendererMemory = (2 * settings.densityResolution + 1) * settings.densityResolution * sizeof(float);
  estimate.memory = simulationMemory(settings) + rendererMemory;
  estimate.withinMemoryBudget = estimate.memory <= MEMORY_BUDGET;
  return estimate;
}
