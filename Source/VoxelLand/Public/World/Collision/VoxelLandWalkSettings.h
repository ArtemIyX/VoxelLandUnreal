#pragma once

struct FVoxelLandWalkSettings
{
	static constexpr double FixedStepSeconds = 1.0 / 60.0;

	double RadiusCm = 34.0;
	double HalfHeightCm = 88.0;
	double SkinCm = 1.5;
	double SpeedCmPerSecond = 420.0;
	double GravityCmPerSecondSquared = 980.0;
	double TerminalSpeedCmPerSecond = 1600.0;
	double FloorProbeCm = 8.0;
	double GroundClearanceCm = 0.75;
	double StepHeightCm = 28.0;
	double MinFloorNormalZ = 0.7071067811865476;
};
