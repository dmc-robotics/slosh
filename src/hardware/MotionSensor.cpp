#include "MotionSensor.h"

#include "BoardPins.h"

namespace {

constexpr float STANDARD_GRAVITY = 9.80665f;

}  // namespace

bool MotionSensor::begin(TwoWire& wire) {
  _ready = _sensor.begin(wire, QMI8658_L_SLAVE_ADDRESS, I2C_DATA_PIN, I2C_CLOCK_PIN);
  if (!_ready) return false;
  _sensor.configAccelerometer(SensorQMI8658::ACC_RANGE_4G, SensorQMI8658::ACC_ODR_250Hz, SensorQMI8658::LPF_MODE_0);
  _sensor.enableAccelerometer();
  return true;
}

bool MotionSensor::readLiquidAcceleration(float& x, float& y) {
  float sensorX, sensorY, sensorZ;
  if (!_ready || !_sensor.getDataReady() || !_sensor.getAccelerometer(sensorX, sensorY, sensorZ)) return false;
  // The sensor's +x points to the top of the display and its +y to the right (checked on the
  // board). It reads +1 g upward at rest; the liquid falls the other way.
  x = -sensorY * STANDARD_GRAVITY;
  y = sensorX * STANDARD_GRAVITY;
  return true;
}
