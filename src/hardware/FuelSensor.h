#pragma once

// Turns the power bus voltage into a remaining-capacity fraction.
class FuelSensor {
 public:
  void begin();
  void update(float timeStep);
  float busVoltage() const { return _busVoltage; }
  bool connected() const;
  // Remaining capacity, 0 to 1.
  float fillLevel() const;

 private:
  float readBusVoltage() const;

  float _busVoltage = 0.0f;
};
