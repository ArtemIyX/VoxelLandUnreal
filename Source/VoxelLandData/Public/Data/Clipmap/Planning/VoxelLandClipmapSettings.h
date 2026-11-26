#pragma once

#include "CoreMinimal.h"

struct FVoxelLandM7ClipmapSettings
{
	FVector ViewCm = FVector::ZeroVector;
	float BaseVoxelSizeCm = 100.f;
	uint8 MaxLod = 5;
	double HorizontalRadiusCm = 300000.0;
	double VerticalRadiusCm = 12800.0;
	double FirstSplitDistanceCm = 9600.0;
	double ExitHysteresis = 0.1;
	int32 MaxLeaves = 512;
	// Keep slots for source/geometry fallback, after admitting the mandatory roots.
	int32 FallbackReserveLeaves = 64;
	// Optional XY detail look-ahead for the height surface. Coverage remains centered on ViewCm.
	TOptional<FVector2D> HeightDetailViewCm;
};
