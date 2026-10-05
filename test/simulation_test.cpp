// Checks the shared core on the host: c++ -std=c++20 -O2 -Isrc src/slosh/*.cpp test/simulation_test.cpp
// Pass a path to also write the final frame as a PPM image.
#include <cmath>
#include <cstdio>
#include <vector>

#include "slosh/FluidRenderer.h"
#include "slosh/FluidSimulation.h"
#include "slosh/PerformanceModel.h"
#include "slosh/TunedSettings.h"

namespace {

int failures = 0;

void check(bool condition, const char* description) {
  std::printf("%s %s\n", condition ? "pass" : "FAIL", description);
  if (!condition) failures++;
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
  for (uint16_t pixel : pixels) {
    unsigned char rgb[3] = {static_cast<unsigned char>((pixel >> 11) << 3), static_cast<unsigned char>(((pixel >> 5) & 63) << 2),
                            static_cast<unsigned char>((pixel & 31) << 3)};
    std::fwrite(rgb, 1, 3, file);
  }
  std::fclose(file);
}

}  // namespace

int main(int argumentCount, char** arguments) {
  FluidSimulation simulation;
  simulation.setFillLevel(0.5f);
  simulation.configure(TUNED_SETTINGS);
  int halfFull = simulation.particleCount();
  check(std::abs(halfFull - simulation.particleCapacity() / 2) <= 1, "half fill seeds half the capacity");
  check(std::abs(simulation.particleCapacity() - fullTankParticleCount(TUNED_SETTINGS)) < simulation.particleCapacity() / 20,
        "lattice capacity matches the estimate");

  for (int frame = 0; frame < 180; frame++) simulation.step(1.0f / 60.0f);
  check(particlesInsideTank(simulation), "particles stay finite and inside the tank");
  check(centroidY(simulation) > simulation.tankCenter() + 0.1f * simulation.tankRadius(), "liquid settles at the bottom");

  // Tip the tank on its side: the liquid should run to the right.
  simulation.setGravity(9.80665f, 0.0f);
  for (int frame = 0; frame < 180; frame++) simulation.step(1.0f / 60.0f);
  float centroidX = 0.0f;
  for (int i = 0; i < simulation.particleCount(); i++) centroidX += simulation.particlePositionsX()[i];
  centroidX /= simulation.particleCount();
  check(centroidX > simulation.tankCenter() + 0.1f * simulation.tankRadius(), "liquid follows gravity sideways");
  check(particlesInsideTank(simulation), "particles stay inside after sloshing");

  simulation.setFillLevel(0.8f);
  check(std::abs(simulation.particleCount() - static_cast<int>(0.8f * simulation.particleCapacity())) <= 1, "raising the fill level adds particles");
  for (int frame = 0; frame < 60; frame++) simulation.step(1.0f / 60.0f);
  check(particlesInsideTank(simulation), "spawned particles stay inside");
  simulation.setFillLevel(0.2f);
  check(simulation.particleCount() == static_cast<int>(std::lround(0.2f * simulation.particleCapacity())), "lowering the fill level removes particles");

  simulation.setGravity(0.0f, 9.80665f);
  for (int frame = 0; frame < 120; frame++) simulation.step(1.0f / 60.0f);

  FluidRenderer renderer;
  renderer.configure(TUNED_SETTINGS, DISPLAY_SIZE);
  renderer.prepare(simulation);
  std::vector<uint16_t> pixels(DISPLAY_SIZE * DISPLAY_SIZE);
  for (int row = 0; row < DISPLAY_SIZE; row += 16) {
    renderer.renderRows(pixels.data() + row * DISPLAY_SIZE, row, std::min(16, DISPLAY_SIZE - row));
  }
  check(pixels[(DISPLAY_SIZE - 40) * DISPLAY_SIZE + DISPLAY_SIZE / 2] != 0, "bottom of the tank is lit");
  check(pixels[40 * DISPLAY_SIZE + DISPLAY_SIZE / 2] == 0, "top of the tank is dark");
  check(pixels[0] == 0, "corners outside the round display are black");
  if (argumentCount > 1) writeImage(arguments[1], pixels);

  PerformanceEstimate estimate = estimatePerformance(TUNED_SETTINGS, DISPLAY_SIZE);
  std::printf("estimate: simulation %.1f ms, render %.1f ms, transfer %.1f ms, %.0f fps\n", estimate.simulationTime * 1e3f,
              estimate.renderTime * 1e3f, estimate.transferTime * 1e3f, estimate.frameRate);

  std::printf(failures == 0 ? "all passed\n" : "%d failed\n", failures);
  return failures == 0 ? 0 : 1;
}
