#include "FuelSensor.h"

#include <Arduino.h>

#include <algorithm>
#include <cmath>

#include "BoardPins.h"
#include "PowerBusConfig.h"

namespace {

// Resting LiPo cell voltage at each 5% of remaining capacity, 0% to 100%.
constexpr float CELL_VOLTAGE_CURVE[] = {3.27f, 3.61f, 3.69f, 3.71f, 3.73f, 3.75f, 3.77f, 3.79f, 3.80f, 3.82f, 3.84f,
                                        3.85f, 3.87f, 3.91f, 3.95f, 3.98f, 4.02f, 4.08f, 4.11f, 4.15f, 4.20f};
constexpr int CURVE_POINTS = sizeof(CELL_VOLTAGE_CURVE) / sizeof(CELL_VOLTAGE_CURVE[0]);

}  // namespace

void FuelSensor::begin() {
  analogSetPinAttenuation(BUS_VOLTAGE_PIN, ADC_11db);
  _busVoltage = readBusVoltage();
}

void FuelSensor::update(float timeStep) {
  float blend = 1.0f - std::exp(-timeStep / VOLTAGE_FILTER_TIME_CONSTANT);
  _busVoltage += (readBusVoltage() - _busVoltage) * blend;
}

bool FuelSensor::connected() const { return _busVoltage >= CELL_COUNT * MINIMUM_CONNECTED_CELL_VOLTAGE; }

float FuelSensor::fillLevel() const {
  if (!connected()) return BENCH_FILL_LEVEL;
  float cellVoltage = _busVoltage / CELL_COUNT;
  if (cellVoltage <= CELL_VOLTAGE_CURVE[0]) return 0.0f;
  for (int i = 1; i < CURVE_POINTS; i++) {
    if (cellVoltage < CELL_VOLTAGE_CURVE[i]) {
      float fraction = (cellVoltage - CELL_VOLTAGE_CURVE[i - 1]) / (CELL_VOLTAGE_CURVE[i] - CELL_VOLTAGE_CURVE[i - 1]);
      return (i - 1 + fraction) / (CURVE_POINTS - 1);
    }
  }
  return 1.0f;
}

float FuelSensor::readBusVoltage() const {
  float pinVoltage = analogReadMilliVolts(BUS_VOLTAGE_PIN) / 1000.0f;
  return pinVoltage * (DIVIDER_TOP_RESISTANCE + DIVIDER_BOTTOM_RESISTANCE) / DIVIDER_BOTTOM_RESISTANCE;
}
