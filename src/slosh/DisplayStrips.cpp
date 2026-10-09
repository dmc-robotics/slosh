#include "DisplayStrips.h"

#include <algorithm>
#include <cmath>

ColumnSpan litColumns(int row, int outputSize) {
  float half = 0.5f * outputSize;
  float offsetY = row + 0.5f - half;
  if (offsetY * offsetY >= half * half) return {0, 0};
  float span = std::sqrt(half * half - offsetY * offsetY);
  return {std::max(static_cast<int>(half - span), 0), std::min(static_cast<int>(std::ceil(half + span)), outputSize)};
}

ColumnSpan stripColumns(int firstRow, int rowCount, int outputSize) {
  ColumnSpan strip{outputSize, 0};
  for (int row = firstRow; row < firstRow + rowCount; row++) {
    ColumnSpan lit = litColumns(row, outputSize);
    if (lit.first >= lit.end) continue;
    strip.first = std::min(strip.first, lit.first);
    strip.end = std::max(strip.end, lit.end);
  }
  if (strip.first >= strip.end) return {0, 0};
  return {strip.first & ~1, std::min((strip.end + 1) & ~1, outputSize)};
}
