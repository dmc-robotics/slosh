#include "FluidRenderer.h"

#include <algorithm>
#include <cmath>

#include "DisplayStrips.h"

namespace {

// In density cells: splatted cells this close to the wall come out thin and are refilled,
// sampling from this far further inside.
constexpr float WALL_MARGIN = 1.0f;
constexpr float SAMPLE_INSET = 0.5f;

float smoothStep(float edge0, float edge1, float value) {
  if (edge1 <= edge0) return value >= edge0 ? 1.0f : 0.0f;
  float t = std::clamp((value - edge0) / (edge1 - edge0), 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}

float channel(uint32_t color, int shift) { return ((color >> shift) & 0xFF) / 255.0f; }

uint16_t toRgb565(float red, float green, float blue) {
  auto quantize = [](float value, int levels) {
    return static_cast<uint16_t>(std::clamp(value, 0.0f, 1.0f) * levels + 0.5f);
  };
  return (quantize(red, 31) << 11) | (quantize(green, 63) << 5) | quantize(blue, 31);
}

}  // namespace

void FluidRenderer::configure(const GaugeSettings& settings, int outputSize) {
  _settings = settings;
  _outputSize = outputSize;
  _resolution = settings.densityResolution;
  _densityCellsPerPixel = static_cast<float>(_resolution) / outputSize;
  _density.assign(_resolution * _resolution, 0.0f);
  _scratch.assign(_resolution * _resolution, 0.0f);
  _rowIndices.assign(_resolution + 1, 0.0f);
  _segments.clear();
  bool previousHeld = false;
  for (int x = 0; x < outputSize; x++) {
    float position = (x + 0.5f) * _densityCellsPerPixel - 0.5f;
    bool held = position < 0.0f || position > _resolution - 1.0f;
    float gridX = std::clamp(position, 0.0f, _resolution - 1.0f);
    int cell = static_cast<int>(gridX);
    if (_segments.empty() || _segments.back().cell != cell || held != previousHeld) {
      _segments.push_back({x, x, cell, gridX - cell, held ? 0.0f : _densityCellsPerPixel});
    }
    _segments.back().endColumn = x + 1;
    previousHeld = held;
  }
  buildPalette();
}

void FluidRenderer::prepare(const FluidSimulation& simulation) {
  std::fill(_density.begin(), _density.end(), 0.0f);
  float tankDiameter = 2.0f * simulation.tankRadius();
  float origin = simulation.tankCenter() - simulation.tankRadius();
  float cellSize = tankDiameter / _resolution;
  // Normalized so liquid at rest has density 1.
  float particleWeight = simulation.particleArea() / (cellSize * cellSize);

  const float* positionsX = simulation.particlePositionsX();
  const float* positionsY = simulation.particlePositionsY();
  for (int i = 0; i < simulation.particleCount(); i++) {
    float gridX = (positionsX[i] - origin) / cellSize - 0.5f;
    float gridY = (positionsY[i] - origin) / cellSize - 0.5f;
    int x0 = static_cast<int>(std::floor(gridX));
    int y0 = static_cast<int>(std::floor(gridY));
    float fractionX = gridX - x0;
    float fractionY = gridY - y0;
    for (int corner = 0; corner < 4; corner++) {
      int x = x0 + (corner & 1);
      int y = y0 + (corner >> 1);
      if (x < 0 || y < 0 || x >= _resolution || y >= _resolution) continue;
      float weightX = (corner & 1) ? fractionX : 1.0f - fractionX;
      float weightY = (corner >> 1) ? fractionY : 1.0f - fractionY;
      _density[y * _resolution + x] += particleWeight * weightX * weightY;
    }
  }

  float gravityLength = std::hypot(simulation.gravityX(), simulation.gravityY());
  if (gravityLength > 0.0f) {
    extendPastWall(simulation.gravityX() / gravityLength, simulation.gravityY() / gravityLength);
  } else {
    extendPastWall(0.0f, 1.0f);
  }
  for (int pass = 0; pass < _settings.smoothingPasses; pass++) smooth(_density);
}

// The wall cuts the splat and blur short, which bends the drawn surface at the edges. Cells
// near and beyond the wall are refilled from just inside it, along the line across gravity
// (the way a level surface runs), so the blur sees the liquid carry on past the wall at the
// same level whatever the wall's slope.
void FluidRenderer::extendPastWall(float downX, float downY) {
  int n = _resolution;
  float center = 0.5f * n;
  float validRadius = center - WALL_MARGIN;
  float sampleRadius = validRadius - SAMPLE_INSET;
  float acrossX = -downY;
  float acrossY = downX;

  auto sample = [&](float x, float y) {
    float gridX = std::clamp(x + center - 0.5f, 0.0f, n - 1.0f);
    float gridY = std::clamp(y + center - 0.5f, 0.0f, n - 1.0f);
    int x0 = std::min(static_cast<int>(gridX), n - 2);
    int y0 = std::min(static_cast<int>(gridY), n - 2);
    float fractionX = gridX - x0;
    float fractionY = gridY - y0;
    float top = _density[y0 * n + x0] + (_density[y0 * n + x0 + 1] - _density[y0 * n + x0]) * fractionX;
    float bottom = _density[(y0 + 1) * n + x0] + (_density[(y0 + 1) * n + x0 + 1] - _density[(y0 + 1) * n + x0]) * fractionX;
    return top + (bottom - top) * fractionY;
  };

  _scratch = _density;
  for (int y = 0; y < n; y++) {
    for (int x = 0; x < n; x++) {
      float pointX = x + 0.5f - center;
      float pointY = y + 0.5f - center;
      float distanceSquared = pointX * pointX + pointY * pointY;
      if (distanceSquared <= validRadius * validRadius) continue;
      // Nearest point inside the sampling circle along the line across gravity; above the top
      // or below the bottom of the tank, where that line misses, the nearest point radially.
      float along = pointX * acrossX + pointY * acrossY;
      float discriminant = along * along - distanceSquared + sampleRadius * sampleRadius;
      float sourceX, sourceY;
      if (discriminant >= 0.0f) {
        float root = std::sqrt(discriminant);
        float shift = along > 0.0f ? -along + root : -along - root;
        sourceX = pointX + shift * acrossX;
        sourceY = pointY + shift * acrossY;
      } else {
        float scale = sampleRadius / std::sqrt(distanceSquared);
        sourceX = pointX * scale;
        sourceY = pointY * scale;
      }
      _scratch[y * n + x] = sample(sourceX, sourceY);
    }
  }
  std::swap(_density, _scratch);
}

// One separable [1 2 1] blur.
void FluidRenderer::smooth(std::vector<float>& field) {
  int n = _resolution;
  for (int y = 0; y < n; y++) {
    for (int x = 0; x < n; x++) {
      int left = std::max(x - 1, 0);
      int right = std::min(x + 1, n - 1);
      _scratch[y * n + x] = 0.25f * (field[y * n + left] + 2.0f * field[y * n + x] + field[y * n + right]);
    }
  }
  for (int y = 0; y < n; y++) {
    int above = std::max(y - 1, 0);
    int below = std::min(y + 1, n - 1);
    for (int x = 0; x < n; x++) {
      field[y * n + x] = 0.25f * (_scratch[above * n + x] + 2.0f * _scratch[y * n + x] + _scratch[below * n + x]);
    }
  }
}

void FluidRenderer::renderRows(uint16_t* destination, int firstRow, int rowCount, int firstColumn, int columnCount) {
  int n = _resolution;
  float paletteScale = (PALETTE_SIZE - 1) / PALETTE_MAXIMUM_DENSITY;

  for (int row = firstRow; row < firstRow + rowCount; row++) {
    uint16_t* pixels = destination + (row - firstRow) * columnCount;
    std::fill(pixels, pixels + columnCount, 0);

    ColumnSpan lit = litColumns(row, _outputSize);
    int start = std::max(lit.first, firstColumn);
    int end = std::min(lit.end, firstColumn + columnCount);
    if (start >= end) continue;

    float gridY = std::clamp((row + 0.5f) * _densityCellsPerPixel - 0.5f, 0.0f, n - 1.0f);
    int y0 = static_cast<int>(gridY);
    int y1 = std::min(y0 + 1, n - 1);
    float fractionY = gridY - y0;
    for (int x = 0; x < n; x++) {
      float value = _density[y0 * n + x] + (_density[y1 * n + x] - _density[y0 * n + x]) * fractionY;
      _rowIndices[x] = std::clamp(value, 0.0f, PALETTE_MAXIMUM_DENSITY) * paletteScale;
    }
    _rowIndices[n] = _rowIndices[n - 1];

    // Steps a 16.16 fixed-point palette index along each segment: the inner loop runs for every
    // lit pixel. Truncating toward zero keeps the index between the segment's end values.
    for (const Segment& segment : _segments) {
      int first = std::max(segment.firstColumn, start);
      int last = std::min(segment.endColumn, end);
      if (first >= last) continue;
      float left = _rowIndices[segment.cell];
      float change = _rowIndices[segment.cell + 1] - left;
      float weight = segment.startWeight + (first - segment.firstColumn) * segment.weightStep;
      int32_t index = static_cast<int32_t>((left + change * weight) * 65536.0f);
      int32_t step = static_cast<int32_t>(change * segment.weightStep * 65536.0f);
      for (uint16_t *pixel = pixels + (first - firstColumn), *stop = pixels + (last - firstColumn); pixel < stop; pixel++) {
        *pixel = _palette[index >> 16];
        index += step;
      }
    }
  }
}

void FluidRenderer::buildPalette() {
  float threshold = _settings.surfaceThreshold;
  float softness = _settings.surfaceSoftness;
  for (int i = 0; i < PALETTE_SIZE; i++) {
    float density = i * PALETTE_MAXIMUM_DENSITY / (PALETTE_SIZE - 1);
    float surface = smoothStep(threshold - softness, threshold + softness, density);
    float rim = 1.0f - smoothStep(threshold, threshold + _settings.rimWidth, density);
    float glow = _settings.glowStrength * smoothStep(0.0f, threshold, density) * (1.0f - surface);

    float color[3];
    for (int component = 0; component < 3; component++) {
      int shift = 16 - 8 * component;
      float liquid = channel(_settings.coreColor, shift) +
                     (channel(_settings.rimColor, shift) - channel(_settings.coreColor, shift)) * rim;
      color[component] = liquid * surface + channel(_settings.glowColor, shift) * glow;
    }
    uint16_t pixel = toRgb565(color[0], color[1], color[2]);
    _palette[i] = static_cast<uint16_t>((pixel << 8) | (pixel >> 8));
  }
}
