#include "FuelSensor.h"

#include <Arduino.h>

#include <cmath>

#include "../slosh/FuelLevel.h"
#include "BoardPins.h"
#include "PowerBusConfig.h"

namespace {

bool connectedAt(float busVoltage) { return busVoltage >= CELL_COUNT * MINIMUM_CONNECTED_CELL_VOLTAGE; }

}  // namespace

void FuelSensor::begin() {
  analogSetPinAttenuation(BUS_VOLTAGE_PIN, ADC_11db);
  _busVoltage = readBusVoltage();
  _fillLevel = measuredFillLevel();
}

void FuelSensor::update(float elapsedTime) {
  float reading = readBusVoltage();
  // Plugging or unplugging the bus jumps straight to the new voltage instead of filtering
  // through an empty tank on the way.
  if (connectedAt(reading) != connected()) {
    _busVoltage = reading;
  } else {
    _busVoltage += (reading - _busVoltage) * (1.0f - std::exp(-elapsedTime / VOLTAGE_FILTER_TIME_CONSTANT));
  }
  _fillLevel = settleFillLevel(_fillLevel, measuredFillLevel());
}

bool FuelSensor::connected() const { return connectedAt(_busVoltage); }

float FuelSensor::measuredFillLevel() const {
  return connected() ? fillLevelFromCellVoltage(_busVoltage / CELL_COUNT) : BENCH_FILL_LEVEL;
}

float FuelSensor::readBusVoltage() const {
  float pinVoltage = analogReadMilliVolts(BUS_VOLTAGE_PIN) / 1000.0f;
  return pinVoltage * (DIVIDER_TOP_RESISTANCE + DIVIDER_BOTTOM_RESISTANCE) / DIVIDER_BOTTOM_RESISTANCE;
}
