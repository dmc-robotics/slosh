#include "FuelLevel.h"

#include <cmath>

namespace {

// Resting LiPo cell voltage at each 5% of remaining capacity, 0% to 100%.
constexpr float CELL_VOLTAGE_CURVE[] = {3.27f, 3.61f, 3.69f, 3.71f, 3.73f, 3.75f, 3.77f, 3.79f, 3.80f, 3.82f, 3.84f,
                                        3.85f, 3.87f, 3.91f, 3.95f, 3.98f, 4.02f, 4.08f, 4.11f, 4.15f, 4.20f};
constexpr int CURVE_POINTS = sizeof(CELL_VOLTAGE_CURVE) / sizeof(CELL_VOLTAGE_CURVE[0]);

constexpr float FILL_DEADBAND = 0.02f;

}  // namespace

float fillLevelFromCellVoltage(float cellVoltage) {
  if (cellVoltage <= CELL_VOLTAGE_CURVE[0]) return 0.0f;
  for (int i = 1; i < CURVE_POINTS; i++) {
    if (cellVoltage < CELL_VOLTAGE_CURVE[i]) {
      float fraction = (cellVoltage - CELL_VOLTAGE_CURVE[i - 1]) / (CELL_VOLTAGE_CURVE[i] - CELL_VOLTAGE_CURVE[i - 1]);
      return (i - 1 + fraction) / (CURVE_POINTS - 1);
    }
  }
  return 1.0f;
}

float settleFillLevel(float shownLevel, float measuredLevel) {
  if (measuredLevel == 0.0f || measuredLevel == 1.0f) return measuredLevel;
  return std::abs(measuredLevel - shownLevel) > FILL_DEADBAND ? measuredLevel : shownLevel;
}
