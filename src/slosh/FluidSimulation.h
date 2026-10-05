#pragma once

#include <cstdint>
#include <vector>

#include "GaugeSettings.h"

// Particles at full charge for these settings.
int fullTankParticleCount(const GaugeSettings& settings);

// 2D FLIP liquid in a circular tank, after Matthias Müller's "Ten Minute Physics" FLIP demo.
// Positions are in meters with x right and y down, measured from the grid corner.
class FluidSimulation {
 public:
  // Rebuilds the grid and refills the tank when the tank geometry changes.
  void configure(const GaugeSettings& settings);
  // Acceleration the liquid feels in display coordinates, m/s².
  void setGravity(float x, float y);
  // Charge left, 0 to 1. Full charge holds settings.fullChargeFill of a tightly packed tank.
  void setFillLevel(float level);
  void step(float timeStep);

  int particleCount() const { return _particleCount; }
  int particleCapacity() const { return _particleCapacity; }
  const float* particlePositionsX() const { return _positionX.data(); }
  const float* particlePositionsY() const { return _positionY.data(); }
  float gravityX() const { return _gravityX; }
  float gravityY() const { return _gravityY; }
  float tankCenter() const { return _tankCenter; }
  float tankRadius() const { return _tankRadius; }
  // Area each particle covers when the liquid is at rest, m².
  float particleArea() const;

 private:
  enum CellType : uint8_t { FLUID, AIR, SOLID };

  struct Stencil {
    int index[4];
    float weight[4];
  };

  void buildGrid();
  void seedParticles(int count);
  void spawnParticle();
  void integrateParticles(float timeStep);
  void pushParticlesApart();
  void handleWallCollisions();
  void transferVelocitiesToGrid();
  void updateParticleDensity();
  void solveIncompressibility(float timeStep);
  void transferVelocitiesFromGrid();
  Stencil stencil(float x, float y, float offsetX, float offsetY) const;
  int cellIndex(float x, float y) const;
  float randomFraction();

  GaugeSettings _settings{};
  float _gravityX = 0.0f;
  float _gravityY = 9.80665f;
  float _fillLevel = 0.5f;
  uint32_t _randomState = 0x9E3779B9;

  // Grid: _gridSize² cells of _cellSize, indexed x * _gridSize + y.
  // u is stored on each cell's left face and v on its top face.
  int _gridSize = 0;
  float _cellSize = 0.0f;
  float _tankCenter = 0.0f;
  float _tankRadius = 0.0f;
  float _restDensity = 0.0f;
  std::vector<float> _u, _v, _previousU, _previousV, _uWeights, _vWeights;
  std::vector<float> _openness;  // 1 inside the tank, 0 for walls
  std::vector<float> _particleDensity;
  std::vector<CellType> _cellType;

  // Particles: the first _particleCount entries are live.
  float _particleRadius = 0.0f;
  int _particleCapacity = 0;
  int _particleCount = 0;
  std::vector<float> _positionX, _positionY, _velocityX, _velocityY;

  // Spatial hash used to keep particles apart.
  int _separationGridSize = 0;
  float _separationInverseSpacing = 0.0f;
  std::vector<int> _separationCellCounts, _separationCellFirst, _separationParticleIds;
};
