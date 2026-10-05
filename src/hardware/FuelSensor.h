#pragma once

// Turns the power bus voltage into a remaining-capacity fraction.
class FuelSensor {
 public:
  void begin();
  // elapsedTime: seconds since the last update.
  void update(float elapsedTime);
  float busVoltage() const { return _busVoltage; }
  bool connected() const;
  // Remaining capacity to show, 0 to 1.
  float fillLevel() const { return _fillLevel; }

 private:
  float measuredFillLevel() const;
  float readBusVoltage() const;

  float _busVoltage = 0.0f;
  float _fillLevel = 0.0f;
};
