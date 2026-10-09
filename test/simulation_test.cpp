// Checks the shared core on the host: c++ -std=c++20 -O2 -Isrc src/slosh/*.cpp test/simulation_test.cpp
// Pass a path to also write the final frame as a PPM image.
#include <cmath>
#include <cstdio>
#include <vector>

#include "slosh/DisplayStrips.h"
#include "slosh/FluidRenderer.h"
#include "slosh/FluidSimulation.h"
#include "slosh/FuelLevel.h"
#include "slosh/PerformanceModel.h"
#include "slosh/TunedSettings.h"

namespace {

// Fixed here so that re-exporting TunedSettings.h can't change what the checks expect.
constexpr GaugeSettings TEST_SETTINGS{
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

int failures = 0;

void check(bool condition, const char* description) {
  std::printf("%s %s\n", condition ? "pass" : "FAIL", description);
  if (!condition) failures++;
}

void simulate(FluidSimulation& simulation, const GaugeSettings& settings, float seconds) {
  for (int frame = 0; frame < seconds * settings.targetFrameRate; frame++) simulation.step();
}

float centroidY(const FluidSimulation& simulation) {
  float sum = 0.0f;
  for (int i = 0; i < simulation.particleCount(); i++) sum += simulation.particlePositionsY()[i];
  return sum / simulation.particleCount();
}

bool particlesInsideTank(const FluidSimulation& simulation) {
  for (int i = 0; i < simulation.particleCount(); i++) {
    float x = simulation.particlePositionsX()[i] - simulation.tankCenter();
    float y = simulation.particlePositionsY()[i] - simulation.tankCenter();
    if (!std::isfinite(x) || !std::isfinite(y)) return false;
    if (std::hypot(x, y) > simulation.tankRadius() * 1.001f) return false;
  }
  return true;
}

void writeImage(const char* path, const std::vector<uint16_t>& pixels) {
  FILE* file = std::fopen(path, "wb");
  std::fprintf(file, "P6 %d %d 255\n", DISPLAY_SIZE, DISPLAY_SIZE);
  for (uint16_t bigEndianPixel : pixels) {
    uint16_t pixel = static_cast<uint16_t>((bigEndianPixel << 8) | (bigEndianPixel >> 8));
    unsigned char rgb[3] = {static_cast<unsigned char>((pixel >> 11) << 3), static_cast<unsigned char>(((pixel >> 5) & 63) << 2),
                            static_cast<unsigned char>((pixel & 31) << 3)};
    std::fwrite(rgb, 1, 3, file);
  }
  std::fclose(file);
}

void checkSimulation(int argumentCount, char** arguments) {
  FluidSimulation simulation;
  simulation.setFillLevel(0.5f);
  simulation.configure(TEST_SETTINGS);
  int halfFull = simulation.particleCount();
  check(std::abs(halfFull - simulation.particleCapacity() / 2) <= 1, "half fill seeds half the capacity");

  simulate(simulation, TEST_SETTINGS, 3.0f);
  check(particlesInsideTank(simulation), "particles stay finite and inside the tank");
  check(centroidY(simulation) > simulation.tankCenter() + 0.1f * simulation.tankRadius(), "liquid settles at the bottom");

  // Tip the tank on its side: the liquid should run to the right.
  simulation.setGravity(9.80665f, 0.0f);
  simulate(simulation, TEST_SETTINGS, 3.0f);
  float centroidX = 0.0f;
  for (int i = 0; i < simulation.particleCount(); i++) centroidX += simulation.particlePositionsX()[i];
  centroidX /= simulation.particleCount();
  check(centroidX > simulation.tankCenter() + 0.1f * simulation.tankRadius(), "liquid follows gravity sideways");
  check(particlesInsideTank(simulation), "particles stay inside after sloshing");

  simulation.setFillLevel(0.8f);
  check(simulation.particleCount() == static_cast<int>(std::lround(0.8f * simulation.particleCapacity())), "raising the fill level adds particles");
  simulate(simulation, TEST_SETTINGS, 1.0f);
  check(particlesInsideTank(simulation), "spawned particles stay inside");
  simulation.setFillLevel(0.2f);
  int lowered = simulation.particleCount();
  check(lowered == static_cast<int>(std::lround(0.2f * simulation.particleCapacity())), "lowering the fill level removes particles");
  simulation.setFillLevel(NAN);
  simulation.setFillLevel(0.2f);
  check(simulation.particleCount() == lowered, "a NaN fill level is ignored");

  simulation.setGravity(0.0f, 9.80665f);
  simulate(simulation, TEST_SETTINGS, 2.0f);

  FluidRenderer renderer;
  renderer.configure(TEST_SETTINGS, DISPLAY_SIZE);
  renderer.prepare(simulation);
  std::vector<uint16_t> pixels(DISPLAY_SIZE * DISPLAY_SIZE);
  for (int row = 0; row < DISPLAY_SIZE; row += STRIP_HEIGHT) {
    renderer.renderRows(pixels.data() + row * DISPLAY_SIZE, row, std::min(STRIP_HEIGHT, DISPLAY_SIZE - row), 0, DISPLAY_SIZE);
  }
  check(pixels[(DISPLAY_SIZE - 40) * DISPLAY_SIZE + DISPLAY_SIZE / 2] != 0, "bottom of the tank is lit");
  check(pixels[40 * DISPLAY_SIZE + DISPLAY_SIZE / 2] == 0, "top of the tank is dark");
  check(pixels[0] == 0, "corners outside the round display are black");

  bool stripsMatch = true;
  bool stripsCoverCircle = true;
  std::vector<uint16_t> strip(DISPLAY_SIZE * STRIP_HEIGHT);
  for (int row = 0; row < DISPLAY_SIZE; row += STRIP_HEIGHT) {
    int rows = std::min(STRIP_HEIGHT, DISPLAY_SIZE - row);
    ColumnSpan sent = stripColumns(row, rows, DISPLAY_SIZE);
    int columns = sent.end - sent.first;
    stripsCoverCircle &= sent.first % 2 == 0 && columns % 2 == 0;
    renderer.renderRows(strip.data(), row, rows, sent.first, columns);
    for (int y = 0; y < rows; y++) {
      ColumnSpan lit = litColumns(row + y, DISPLAY_SIZE);
      stripsCoverCircle &= lit.first >= sent.first && lit.end <= sent.end;
      for (int x = 0; x < columns; x++) {
        stripsMatch &= strip[y * columns + x] == pixels[(row + y) * DISPLAY_SIZE + sent.first + x];
      }
    }
  }
  check(stripsMatch, "strips rendered with only their columns match whole rows");
  check(stripsCoverCircle, "strip columns are even and cover every lit pixel");
  if (argumentCount > 1) writeImage(arguments[1], pixels);
}

void checkSettings() {
  check(settingsValid(TEST_SETTINGS), "test settings are valid");
  GaugeSettings settings = TEST_SETTINGS;
  settings.particleRadiusRatio = 0.0f;
  check(!settingsValid(settings), "a zero particle radius is invalid");
  settings = TEST_SETTINGS;
  settings.densityResolution = 1;
  check(!settingsValid(settings), "a one-cell density field is invalid");
  settings = TEST_SETTINGS;
  settings.particleRadiusRatio = 10.0f;
  check(!settingsValid(settings), "particles wider than a cell are invalid");
  settings = TEST_SETTINGS;
  settings.targetFrameRate = NAN;
  check(!settingsValid(settings), "a NaN frame rate is invalid");

  check(estimatePerformance(TEST_SETTINGS, DISPLAY_SIZE).withinMemoryBudget, "test settings fit the heap budget");
  check(refreshesPerFrame(30.0f) == 2 && refreshesPerFrame(60.0f) == 1, "30 fps shows each frame for two panel refreshes");
  settings = TEST_SETTINGS;
  settings.targetFrameRate = 40.0f;
  check(!estimatePerformance(settings, DISPLAY_SIZE).withinFrameBudget, "a target the panel can't hold is over budget");
  settings = TEST_SETTINGS;
  settings.gridResolution = 64;
  settings.particleRadiusRatio = 0.2f;
  settings.fullChargeFill = 4.0f;
  check(!estimatePerformance(settings, DISPLAY_SIZE).withinMemoryBudget, "the densest slider settings are over the heap budget");
}

void checkFuelLevel() {
  check(fillLevelFromCellVoltage(3.0f) == 0.0f, "a flat cell is empty");
  check(fillLevelFromCellVoltage(4.25f) == 1.0f, "a full cell is full");
  check(std::abs(fillLevelFromCellVoltage(3.80f) - 0.4f) < 1e-4f, "a cell at 3.80 V has 40% left");
  check(settleFillLevel(0.5f, 0.51f) == 0.5f, "small changes in the level are held");
  check(settleFillLevel(0.5f, 0.45f) == 0.45f, "large changes in the level go through");
  check(settleFillLevel(0.01f, 0.0f) == 0.0f && settleFillLevel(0.99f, 1.0f) == 1.0f, "empty and full always show");
}

// The exported settings can change at any time, so only check that they run.
void checkTunedSettings() {
  FluidSimulation simulation;
  simulation.setFillLevel(1.0f);
  simulation.configure(TUNED_SETTINGS);
  simulate(simulation, TUNED_SETTINGS, 2.0f);
  check(particlesInsideTank(simulation), "tuned settings run and stay inside the tank");

  PerformanceEstimate estimate = estimatePerformance(TUNED_SETTINGS, DISPLAY_SIZE);
  std::printf("tuned estimate: simulation %.1f ms, render %.1f ms, transfer %.1f ms, %.0f fps, %d KB heap\n",
              estimate.simulationTime * 1e3f, estimate.renderTime * 1e3f, estimate.transferTime * 1e3f, estimate.frameRate,
              estimate.memory / 1000);
}

}  // namespace

int main(int argumentCount, char** arguments) {
  checkSimulation(argumentCount, arguments);
  checkSettings();
  checkFuelLevel();
  checkTunedSettings();
  std::printf(failures == 0 ? "all passed\n" : "%d failed\n", failures);
  return failures == 0 ? 0 : 1;
}
