#pragma once

// Remaining capacity, 0 to 1, of a resting LiPo cell at this voltage.
float fillLevelFromCellVoltage(float cellVoltage);

// The level to show next: the shown level holds until the measured one moves past a small
// deadband, so measurement noise doesn't keep trickling particles in and out of the tank.
float settleFillLevel(float shownLevel, float measuredLevel);
