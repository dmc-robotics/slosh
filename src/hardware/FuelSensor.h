#pragma once

#include <HardwareSerial.h>

// Turns the power bus voltage, as sent by the data provider, into a remaining-capacity fraction.
class FuelSensor {
 public:
  void begin(HardwareSerial& port);
  // elapsedTime: seconds since the last update.
  void update(float elapsedTime);
  float busVoltage() const { return _busVoltage; }
  // True while readings keep arriving.
  bool connected() const;
  // Remaining capacity to show, 0 to 1.
  float fillLevel() const { return _fillLevel; }

 private:
  static constexpr int LINE_CAPACITY = 16;

  bool readVoltage(float& voltage);
  float measuredFillLevel() const;

  HardwareSerial* _port = nullptr;
  char _line[LINE_CAPACITY] = {};
  int _lineLength = 0;
  bool _discardLine = true;  // overlong, or maybe joined partway through: at startup and after a gap
  float _signalAge = 0.0f;  // s since the last reading
  bool _signalSeen = false;
  float _latestVoltage = 0.0f;
  float _busVoltage = 0.0f;
  float _fillLevel = 0.0f;
};
