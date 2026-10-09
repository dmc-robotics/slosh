#include "FluidSimulation.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <numeric>

namespace {

constexpr float PI = 3.14159265358979f;
constexpr float SQRT_THREE = 1.7320508f;
constexpr float SEPARATION_SPACING_RATIO = 2.2f;  // hash cell size in particle radii
constexpr int SPAWN_ATTEMPTS = 16;
constexpr float LATTICE_SQUEEZE = 0.97f;  // spacing factor per try when seeding more than fit at rest
// 1 / the number of open sides around a cell, indexed by that number (openness is 0 or 1 per side).
constexpr float INVERSE_SIDE_COUNT[5] = {0.0f, 1.0f, 0.5f, 1.0f / 3.0f, 0.25f};

// Particles sit on a hexagonal lattice at rest, two radii apart.
float latticeSpacingX(float particleRadius) { return 2.0f * particleRadius; }
float latticeSpacingY(float particleRadius) { return SQRT_THREE * particleRadius; }

// Calls visit(x, y) for each hexagonal lattice point within radius of the origin, spaced for
// particles of particleRadius.
template <typename Visit>
void forEachLatticePoint(float radius, float particleRadius, Visit visit) {
  float spacingX = latticeSpacingX(particleRadius);
  float spacingY = latticeSpacingY(particleRadius);
  int rows = static_cast<int>(2.0f * radius / spacingY) + 1;
  for (int row = 0; row < rows; row++) {
    float y = -radius + row * spacingY;
    float offset = (row % 2) * particleRadius;
    for (float x = -radius + offset; x <= radius; x += spacingX) {
      if (x * x + y * y <= radius * radius) visit(x, y);
    }
  }
}

int latticePointCount(float radius, float particleRadius) {
  int count = 0;
  forEachLatticePoint(radius, particleRadius, [&](float, float) { count++; });
  return count;
}

// Particles that fit the tank at rest spacing.
int restParticleCount(const GaugeSettings& settings) {
  float particleRadius = settings.particleRadiusRatio * (settings.tankDiameter / settings.gridResolution);
  return latticePointCount(0.5f * settings.tankDiameter - particleRadius, particleRadius);
}

// Cells across the spatial hash that keeps particles apart.
int separationGridSize(int gridSize, float cellSize, float particleRadius) {
  return static_cast<int>(gridSize * cellSize / (SEPARATION_SPACING_RATIO * particleRadius)) + 1;
}

}  // namespace

int fullTankParticleCount(const GaugeSettings& settings) {
  return static_cast<int>(std::lround(restParticleCount(settings) * settings.fullChargeFill));
}

int simulationMemory(const GaugeSettings& settings) {
  int gridSize = settings.gridResolution + 2;
  float cellSize = settings.tankDiameter / settings.gridResolution;
  int separationSize = separationGridSize(gridSize, cellSize, settings.particleRadiusRatio * cellSize);
  int particles = fullTankParticleCount(settings);
  // Seeding lays out at least the rest lattice, squeezed past the count needed when it is too small.
  int latticePoints = std::max(restParticleCount(settings), static_cast<int>(particles / (LATTICE_SQUEEZE * LATTICE_SQUEEZE)));
  int gridBytes = gridSize * gridSize * (8 * sizeof(float) + sizeof(uint8_t));  // velocities, weights, density, openness, cell type
  int separationBytes = separationSize * separationSize * 2 * sizeof(int);
  int particleBytes = particles * (4 * sizeof(float) + sizeof(int));
  int seedingBytes = latticePoints * (2 * sizeof(float) + sizeof(int));  // lattice and sort order, freed after seeding
  return gridBytes + separationBytes + particleBytes + seedingBytes;
}

float FluidSimulation::particleArea() const {
  return latticeSpacingX(_particleRadius) * latticeSpacingY(_particleRadius);
}

void FluidSimulation::configure(const GaugeSettings& settings) {
  bool geometryChanged = settings.tankDiameter != _settings.tankDiameter ||
                         settings.gridResolution != _settings.gridResolution ||
                         settings.particleRadiusRatio != _settings.particleRadiusRatio ||
                         settings.fullChargeFill != _settings.fullChargeFill;
  _settings = settings;
  if (geometryChanged) buildGrid();
}

void FluidSimulation::setGravity(float x, float y) {
  _gravityX = x;
  _gravityY = y;
}

void FluidSimulation::setFillLevel(float level) {
  if (std::isnan(level)) return;
  _fillLevel = std::clamp(level, 0.0f, 1.0f);
  int target = static_cast<int>(std::lround(_fillLevel * _particleCapacity));
  while (_particleCount < target) spawnParticle();
  _particleCount = std::min(_particleCount, target);
}

void FluidSimulation::buildGrid() {
  _cellSize = _settings.tankDiameter / _settings.gridResolution;
  _inverseCellSize = 1.0f / _cellSize;
  _gridSize = _settings.gridResolution + 2;  // one wall cell beyond the tank on each side
  _tankRadius = 0.5f * _settings.tankDiameter;
  _tankCenter = 0.5f * _gridSize * _cellSize;
  _particleRadius = _settings.particleRadiusRatio * _cellSize;
  _restDensity = _cellSize * _cellSize / particleArea();

  int cellCount = _gridSize * _gridSize;
  for (auto* field : {&_u, &_v, &_previousU, &_previousV, &_uWeights, &_vWeights, &_particleDensity}) {
    field->assign(cellCount, 0.0f);
  }
  _openness.assign(cellCount, 0.0f);
  _cellType.assign(cellCount, AIR);
  for (int x = 0; x < _gridSize; x++) {
    for (int y = 0; y < _gridSize; y++) {
      float centerX = (x + 0.5f) * _cellSize - _tankCenter;
      float centerY = (y + 0.5f) * _cellSize - _tankCenter;
      bool inside = centerX * centerX + centerY * centerY <= _tankRadius * _tankRadius;
      _openness[x * _gridSize + y] = inside ? 1.0f : 0.0f;
    }
  }

  _separationInverseSpacing = 1.0f / (SEPARATION_SPACING_RATIO * _particleRadius);
  _separationGridSize = separationGridSize(_gridSize, _cellSize, _particleRadius);
  int separationCellCount = _separationGridSize * _separationGridSize;
  _separationCellCounts.assign(separationCellCount, 0);
  _separationCellFirst.assign(separationCellCount + 1, 0);

  _particleCapacity = fullTankParticleCount(_settings);
  for (auto* field : {&_positionX, &_positionY, &_velocityX, &_velocityY}) {
    field->assign(_particleCapacity, 0.0f);
  }
  _separationParticleIds.assign(_particleCapacity, 0);
  seedParticles(static_cast<int>(std::lround(_fillLevel * _particleCapacity)));
}

// Fills a lattice from the bottom up, where "down" follows gravity. When more particles are
// asked for than fit at rest spacing, the lattice is squeezed until they do.
void FluidSimulation::seedParticles(int count) {
  float gravityLength = std::hypot(_gravityX, _gravityY);
  float downX = gravityLength > 0.0f ? _gravityX / gravityLength : 0.0f;
  float downY = gravityLength > 0.0f ? _gravityY / gravityLength : 1.0f;

  float latticeRadius = _tankRadius - _particleRadius;
  float spacingRadius = _particleRadius;
  while (latticePointCount(latticeRadius, spacingRadius) < count) spacingRadius *= LATTICE_SQUEEZE;
  std::vector<float> latticeX, latticeY;
  latticeX.reserve(latticePointCount(latticeRadius, spacingRadius));
  latticeY.reserve(latticeX.capacity());
  forEachLatticePoint(latticeRadius, spacingRadius, [&](float x, float y) {
    latticeX.push_back(x);
    latticeY.push_back(y);
  });

  std::vector<int> order(latticeX.size());
  std::iota(order.begin(), order.end(), 0);
  std::sort(order.begin(), order.end(), [&](int a, int b) {
    return latticeX[a] * downX + latticeY[a] * downY > latticeX[b] * downX + latticeY[b] * downY;
  });

  _particleCount = std::min(count, _particleCapacity);
  for (int i = 0; i < _particleCount; i++) {
    _positionX[i] = _tankCenter + latticeX[order[i]];
    _positionY[i] = _tankCenter + latticeY[order[i]];
    _velocityX[i] = 0.0f;
    _velocityY[i] = 0.0f;
  }
}

// Adds a particle somewhere in the tank's empty space, from where it falls into the liquid.
void FluidSimulation::spawnParticle() {
  float usableRadius = _tankRadius - _particleRadius;
  float x = _tankCenter;
  float y = _tankCenter;
  for (int attempt = 0; attempt < SPAWN_ATTEMPTS; attempt++) {
    float angle = 2.0f * PI * randomFraction();
    float radius = usableRadius * std::sqrt(randomFraction());
    x = _tankCenter + radius * std::cos(angle);
    y = _tankCenter + radius * std::sin(angle);
    if (_cellType[cellIndex(x, y)] == AIR) break;
  }
  int i = _particleCount++;
  _positionX[i] = x;
  _positionY[i] = y;
  _velocityX[i] = 0.0f;
  _velocityY[i] = 0.0f;
}

void FluidSimulation::step() {
  if (_particleCount == 0) return;
  float substepTime = 1.0f / (_settings.targetFrameRate * _settings.substeps);
  using Clock = std::chrono::steady_clock;
  Clock::time_point mark = Clock::now();
  auto lap = [&](float& total) {
    Clock::time_point now = Clock::now();
    total += std::chrono::duration<float>(now - mark).count();
    mark = now;
  };
  for (int substep = 0; substep < _settings.substeps; substep++) {
    integrateParticles(substepTime);
    lap(_profile.integrate);
    pushParticlesApart();
    lap(_profile.separate);
    handleWallCollisions();
    lap(_profile.walls);
    transferVelocitiesToGrid();
    lap(_profile.toGrid);
    updateParticleDensity();
    lap(_profile.density);
    solveIncompressibility(substepTime);
    lap(_profile.pressure);
    transferVelocitiesFromGrid();
    lap(_profile.fromGrid);
  }
}

void FluidSimulation::integrateParticles(float timeStep) {
  float accelerationX = _gravityX * _settings.gravityScale;
  float accelerationY = _gravityY * _settings.gravityScale;
  for (int i = 0; i < _particleCount; i++) {
    _velocityX[i] += accelerationX * timeStep;
    _velocityY[i] += accelerationY * timeStep;
    _positionX[i] += _velocityX[i] * timeStep;
    _positionY[i] += _velocityY[i] * timeStep;
  }
}

void FluidSimulation::pushParticlesApart() {
  int size = _separationGridSize;
  auto separationCell = [&](float position) {
    return std::clamp(static_cast<int>(position * _separationInverseSpacing), 0, size - 1);
  };

  // Bucket particles by cell: counting sort into _separationParticleIds.
  std::fill(_separationCellCounts.begin(), _separationCellCounts.end(), 0);
  for (int i = 0; i < _particleCount; i++) {
    _separationCellCounts[separationCell(_positionX[i]) * size + separationCell(_positionY[i])]++;
  }
  int first = 0;
  for (int cell = 0; cell < size * size; cell++) {
    first += _separationCellCounts[cell];
    _separationCellFirst[cell] = first;
  }
  _separationCellFirst[size * size] = first;
  for (int i = 0; i < _particleCount; i++) {
    int cell = separationCell(_positionX[i]) * size + separationCell(_positionY[i]);
    _separationParticleIds[--_separationCellFirst[cell]] = i;
  }

  float minimumDistance = 2.0f * _particleRadius;
  float minimumDistanceSquared = minimumDistance * minimumDistance;
  for (int iteration = 0; iteration < _settings.separationIterations; iteration++) {
    for (int i = 0; i < _particleCount; i++) {
      int cellX = separationCell(_positionX[i]);
      int cellY = separationCell(_positionY[i]);
      for (int x = std::max(cellX - 1, 0); x <= std::min(cellX + 1, size - 1); x++) {
        for (int y = std::max(cellY - 1, 0); y <= std::min(cellY + 1, size - 1); y++) {
          int cell = x * size + y;
          for (int slot = _separationCellFirst[cell]; slot < _separationCellFirst[cell + 1]; slot++) {
            int other = _separationParticleIds[slot];
            if (other == i) continue;
            float deltaX = _positionX[other] - _positionX[i];
            float deltaY = _positionY[other] - _positionY[i];
            float distanceSquared = deltaX * deltaX + deltaY * deltaY;
            if (distanceSquared > minimumDistanceSquared || distanceSquared == 0.0f) continue;
            float distance = std::sqrt(distanceSquared);
            float scale = 0.5f * (minimumDistance - distance) / distance;
            deltaX *= scale;
            deltaY *= scale;
            _positionX[i] -= deltaX;
            _positionY[i] -= deltaY;
            _positionX[other] += deltaX;
            _positionY[other] += deltaY;
          }
        }
      }
    }
  }
}

void FluidSimulation::handleWallCollisions() {
  float limit = _tankRadius - _particleRadius;
  for (int i = 0; i < _particleCount; i++) {
    float offsetX = _positionX[i] - _tankCenter;
    float offsetY = _positionY[i] - _tankCenter;
    float distanceSquared = offsetX * offsetX + offsetY * offsetY;
    if (distanceSquared <= limit * limit) continue;
    float inverseDistance = 1.0f / std::sqrt(distanceSquared);
    float normalX = offsetX * inverseDistance;
    float normalY = offsetY * inverseDistance;
    _positionX[i] = _tankCenter + normalX * limit;
    _positionY[i] = _tankCenter + normalY * limit;
    float outward = _velocityX[i] * normalX + _velocityY[i] * normalY;
    if (outward > 0.0f) {
      _velocityX[i] -= outward * normalX;
      _velocityY[i] -= outward * normalY;
    }
  }
}

FluidSimulation::Stencil FluidSimulation::stencil(float x, float y, float offsetX, float offsetY) const {
  int n = _gridSize;
  x = std::clamp(x, _cellSize, (n - 1) * _cellSize) - offsetX;
  y = std::clamp(y, _cellSize, (n - 1) * _cellSize) - offsetY;

  int x0 = std::min(static_cast<int>(x * _inverseCellSize), n - 2);
  int y0 = std::min(static_cast<int>(y * _inverseCellSize), n - 2);
  float fractionX = (x - x0 * _cellSize) * _inverseCellSize;
  float fractionY = (y - y0 * _cellSize) * _inverseCellSize;
  int x1 = std::min(x0 + 1, n - 2);
  int y1 = std::min(y0 + 1, n - 2);
  float remainderX = 1.0f - fractionX;
  float remainderY = 1.0f - fractionY;

  return {{x0 * n + y0, x1 * n + y0, x1 * n + y1, x0 * n + y1},
          {remainderX * remainderY, fractionX * remainderY, fractionX * fractionY, remainderX * fractionY}};
}

int FluidSimulation::cellIndex(float x, float y) const {
  int cellX = std::clamp(static_cast<int>(x * _inverseCellSize), 0, _gridSize - 1);
  int cellY = std::clamp(static_cast<int>(y * _inverseCellSize), 0, _gridSize - 1);
  return cellX * _gridSize + cellY;
}

void FluidSimulation::transferVelocitiesToGrid() {
  _previousU = _u;
  _previousV = _v;
  for (auto* field : {&_u, &_v, &_uWeights, &_vWeights}) std::fill(field->begin(), field->end(), 0.0f);

  for (size_t cell = 0; cell < _cellType.size(); cell++) {
    _cellType[cell] = _openness[cell] == 0.0f ? SOLID : AIR;
  }
  for (int i = 0; i < _particleCount; i++) {
    int cell = cellIndex(_positionX[i], _positionY[i]);
    if (_cellType[cell] == AIR) _cellType[cell] = FLUID;
  }

  float halfCell = 0.5f * _cellSize;
  for (int i = 0; i < _particleCount; i++) {
    Stencil uStencil = stencil(_positionX[i], _positionY[i], 0.0f, halfCell);
    Stencil vStencil = stencil(_positionX[i], _positionY[i], halfCell, 0.0f);
    for (int corner = 0; corner < 4; corner++) {
      _u[uStencil.index[corner]] += _velocityX[i] * uStencil.weight[corner];
      _uWeights[uStencil.index[corner]] += uStencil.weight[corner];
      _v[vStencil.index[corner]] += _velocityY[i] * vStencil.weight[corner];
      _vWeights[vStencil.index[corner]] += vStencil.weight[corner];
    }
  }
  for (size_t face = 0; face < _u.size(); face++) {
    if (_uWeights[face] > 0.0f) _u[face] /= _uWeights[face];
    if (_vWeights[face] > 0.0f) _v[face] /= _vWeights[face];
  }

  // Faces touching a wall keep their previous (zero) velocity.
  int n = _gridSize;
  for (int x = 0; x < n; x++) {
    for (int y = 0; y < n; y++) {
      int cell = x * n + y;
      bool solid = _cellType[cell] == SOLID;
      if (solid || (x > 0 && _cellType[cell - n] == SOLID)) _u[cell] = _previousU[cell];
      if (solid || (y > 0 && _cellType[cell - 1] == SOLID)) _v[cell] = _previousV[cell];
    }
  }
}

void FluidSimulation::updateParticleDensity() {
  std::fill(_particleDensity.begin(), _particleDensity.end(), 0.0f);
  float halfCell = 0.5f * _cellSize;
  for (int i = 0; i < _particleCount; i++) {
    Stencil cellStencil = stencil(_positionX[i], _positionY[i], halfCell, halfCell);
    for (int corner = 0; corner < 4; corner++) {
      _particleDensity[cellStencil.index[corner]] += cellStencil.weight[corner];
    }
  }
}

void FluidSimulation::solveIncompressibility(float timeStep) {
  _previousU = _u;
  _previousV = _v;
  int n = _gridSize;
  // Converts excess particles per cell into the outflow that would clear them in one step.
  float driftScale = _settings.driftCompensation * _cellSize / (_restDensity * timeStep);

  for (int iteration = 0; iteration < _settings.pressureIterations; iteration++) {
    for (int x = 1; x < n - 1; x++) {
      for (int y = 1; y < n - 1; y++) {
        int center = x * n + y;
        if (_cellType[center] != FLUID) continue;
        int left = center - n;
        int right = center + n;
        int top = center - 1;
        int bottom = center + 1;

        float openness = _openness[left] + _openness[right] + _openness[top] + _openness[bottom];
        if (openness == 0.0f) continue;

        float divergence = _u[right] - _u[center] + _v[bottom] - _v[center];
        float compression = _particleDensity[center] - _restDensity;
        if (compression > 0.0f) divergence -= driftScale * compression;

        float pressure = -divergence * INVERSE_SIDE_COUNT[static_cast<int>(openness)] * _settings.overRelaxation;
        _u[center] -= _openness[left] * pressure;
        _u[right] += _openness[right] * pressure;
        _v[center] -= _openness[top] * pressure;
        _v[bottom] += _openness[bottom] * pressure;
      }
    }
  }
}

void FluidSimulation::transferVelocitiesFromGrid() {
  float halfCell = 0.5f * _cellSize;
  float flipRatio = _settings.flipRatio;

  // A face is valid when a cell on either side of it holds liquid or wall.
  auto sample = [&](const Stencil& faceStencil, int neighborOffset, const std::vector<float>& field,
                    const std::vector<float>& previousField, float& velocity) {
    float weightSum = 0.0f;
    float picVelocity = 0.0f;
    float correction = 0.0f;
    for (int corner = 0; corner < 4; corner++) {
      int face = faceStencil.index[corner];
      bool valid = _cellType[face] != AIR || _cellType[face - neighborOffset] != AIR;
      if (!valid) continue;
      float weight = faceStencil.weight[corner];
      weightSum += weight;
      picVelocity += weight * field[face];
      correction += weight * (field[face] - previousField[face]);
    }
    if (weightSum == 0.0f) return;
    float inverseWeightSum = 1.0f / weightSum;
    picVelocity *= inverseWeightSum;
    float flipVelocity = velocity + correction * inverseWeightSum;
    velocity = (1.0f - flipRatio) * picVelocity + flipRatio * flipVelocity;
  };

  for (int i = 0; i < _particleCount; i++) {
    sample(stencil(_positionX[i], _positionY[i], 0.0f, halfCell), _gridSize, _u, _previousU, _velocityX[i]);
    sample(stencil(_positionX[i], _positionY[i], halfCell, 0.0f), 1, _v, _previousV, _velocityY[i]);
  }
}

float FluidSimulation::randomFraction() {
  _randomState ^= _randomState << 13;
  _randomState ^= _randomState >> 17;
  _randomState ^= _randomState << 5;
  return (_randomState >> 8) * (1.0f / 16777216.0f);
}
