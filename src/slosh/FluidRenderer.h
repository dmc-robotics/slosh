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
  // Writes rowCount rows of big-endian RGB565 pixels starting at firstRow, each holding columns
  // firstColumn to firstColumn + columnCount, packed. Big-endian is the display's byte order, so
  // the firmware sends strips to it without copying them.
  void renderRows(uint16_t* destination, int firstRow, int rowCount, int firstColumn, int columnCount);

 private:
  static constexpr int PALETTE_SIZE = 256;
  static constexpr float PALETTE_MAXIMUM_DENSITY = 2.0f;

  // Output columns over which the palette index changes linearly: between two density cell
  // centers, or held at the edge cell beyond the outermost centers.
  struct Segment {
    int firstColumn;
    int endColumn;
    int cell;
    float startWeight;  // toward cell + 1 at firstColumn, 0 to 1
    float weightStep;   // per column
  };

  void buildPalette();
  void extendPastWall(float downX, float downY);
  void smooth(std::vector<float>& field);

  GaugeSettings _settings{};
  int _outputSize = 0;
  int _resolution = 0;
  float _densityCellsPerPixel = 0.0f;
  std::vector<float> _density, _scratch;
  std::vector<Segment> _segments;
  std::vector<float> _rowIndices;  // palette index per density cell, the last one repeated
  uint16_t _palette[PALETTE_SIZE] = {};
};
