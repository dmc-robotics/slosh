#pragma once

#include <cstdint>
#include <vector>

#include "FluidSimulation.h"
#include "GaugeSettings.h"

// Draws the liquid as a smoothed particle density field mapped through a color palette.
// Rendering works on bands of rows so the firmware can stream a frame through a small buffer.
class FluidRenderer {
 public:
  void configure(const GaugeSettings& settings, int outputSize);
  // Builds the density field for the current particle positions.
  void prepare(const FluidSimulation& simulation);
  // Writes rowCount full-width rows of RGB565 pixels, starting at firstRow.
  void renderRows(uint16_t* destination, int firstRow, int rowCount);

 private:
  static constexpr int PALETTE_SIZE = 256;
  static constexpr float PALETTE_MAXIMUM_DENSITY = 2.0f;

  void buildPalette();
  void extendPastWall(float downX, float downY);
  void smooth(std::vector<float>& field);

  GaugeSettings _settings{};
  int _outputSize = 0;
  int _resolution = 0;
  float _densityCellsPerPixel = 0.0f;
  std::vector<float> _density, _scratch, _rowValues;
  uint16_t _palette[PALETTE_SIZE] = {};
};
