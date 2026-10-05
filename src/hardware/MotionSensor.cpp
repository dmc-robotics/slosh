#include "MotionSensor.h"

#include "BoardPins.h"

namespace {

constexpr float STANDARD_GRAVITY = 9.80665f;

// How the sensor's axes map onto the display's. Unverified: confirm on the hardware that
// tilting the board to the right sends the liquid to the right, and flip these if not.
constexpr float SENSOR_X_TO_DISPLAY_X = 1.0f;
constexpr float SENSOR_Y_TO_DISPLAY_Y = -1.0f;

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
  // The accelerometer reads +1 g upward at rest; the liquid falls the other way.
  x = -sensorX * SENSOR_X_TO_DISPLAY_X * STANDARD_GRAVITY;
  y = -sensorY * SENSOR_Y_TO_DISPLAY_Y * STANDARD_GRAVITY;
  return true;
}
