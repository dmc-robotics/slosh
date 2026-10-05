#pragma once

#include <SensorQMI8658.hpp>
#include <Wire.h>

// Reads the QMI8658 accelerometer as the acceleration the liquid feels.
class MotionSensor {
 public:
  bool begin(TwoWire& wire);
  // Gravity minus the board's own acceleration, in display axes (x right, y down), m/s².
  // Returns false when no new sample is available.
  bool readLiquidAcceleration(float& x, float& y);

 private:
  SensorQMI8658 _sensor;
  bool _ready = false;
};
