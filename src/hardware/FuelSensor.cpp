#include "FuelSensor.h"

#include <cmath>
#include <cstdlib>

#include "../slosh/FuelLevel.h"
#include "BoardPins.h"
#include "PowerBusConfig.h"

void FuelSensor::begin(HardwareSerial& port) {
  _port = &port;
  _port->begin(BUS_DATA_BAUD_RATE, SERIAL_8N1, BUS_DATA_PIN, -1);
  _fillLevel = measuredFillLevel();
}

void FuelSensor::update(float elapsedTime) {
  bool wasConnected = connected();
  _signalAge += elapsedTime;
  float reading;
  while (readVoltage(reading)) {
    _latestVoltage = reading;
    _signalAge = 0.0f;
    _signalSeen = true;
  }
  // The provider may come back partway through a line, whose tail would parse as a wrong reading.
  if (wasConnected && !connected()) _discardLine = true;
  if (connected()) {
    // After a gap, jump to the new voltage rather than filtering up from stale data.
    if (!wasConnected) {
      _busVoltage = _latestVoltage;
    } else {
      _busVoltage += (_latestVoltage - _busVoltage) * (1.0f - std::exp(-elapsedTime / VOLTAGE_FILTER_TIME_CONSTANT));
    }
  }
  _fillLevel = settleFillLevel(_fillLevel, measuredFillLevel());
}

bool FuelSensor::connected() const { return _signalSeen && _signalAge < SIGNAL_TIMEOUT; }

float FuelSensor::measuredFillLevel() const {
  return connected() ? fillLevelFromCellVoltage(_busVoltage / CELL_COUNT) : BENCH_FILL_LEVEL;
}

// Reads the next complete line from the provider. Malformed, overlong and implausible lines are dropped.
bool FuelSensor::readVoltage(float& voltage) {
  while (_port->available() > 0) {
    char character = static_cast<char>(_port->read());
    if (character == '\r') continue;
    if (character != '\n') {
      if (_lineLength < LINE_CAPACITY - 1) {
        _line[_lineLength++] = character;
      } else {
        _discardLine = true;
      }
      continue;
    }

    bool usable = _lineLength > 0 && !_discardLine;
    _line[_lineLength] = '\0';
    _lineLength = 0;
    _discardLine = false;
    if (!usable) continue;

    char* end;
    float value = std::strtof(_line, &end);
    bool parsed = end != _line;
    while (*end == ' ') end++;
    // The range check also rejects NaN and infinity.
    if (parsed && *end == '\0' && value >= CELL_COUNT * MINIMUM_CELL_VOLTAGE && value <= CELL_COUNT * MAXIMUM_CELL_VOLTAGE) {
      voltage = value;
      return true;
    }
  }
  return false;
}
