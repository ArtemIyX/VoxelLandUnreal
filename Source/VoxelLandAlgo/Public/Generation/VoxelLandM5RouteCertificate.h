#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandM5Diagnostic.h"
#include "Data/Structs/VoxelLandM5PointCm.h"
#include "Generation/VoxelLandM5ClosedBoxCm.h"
#include "Templates/Function.h"

namespace VoxelLand::M5::RouteCertificate
{
// A sufficient broad-phase separation check, NOT a walkability certificate.
// StartFeet and EndFeet are the bottom points of an upright capsule translated
// linearly between the endpoints. RadiusCm and total HeightCm include the
// hemispherical ends. MarginCm expands all six sides. Integer bounds enclose
// the entire continuous sweep, not merely samples along its path.
//
// Obstacle boxes use inclusive bounds. Touching the swept box is Unproven;
// disjointness from every supplied box returns None. None proves only this
// box-separation fact. The caller MUST establish that the supplied boxes
// conservatively enclose every relevant actual final-mesh obstacle, including
// conversion errors; independently certify support, contact, slope, steps,
// path transitions, and region/mesh completeness. Missing obstacles or support
// cannot be detected here. Do not publish a cave using this result alone.
// MaxChecks is a deterministic cap on obstacle inspections. All input is
// borrowed for this call; no UObjects or mutable worker state are captured.
VOXELLANDALGO_API FVoxelLandM5Diagnostic CheckSweptCapsuleBoxSeparation(const FVoxelLandM5PointCm& StartFeet,
	const FVoxelLandM5PointCm& EndFeet, int64 RadiusCm, int64 HeightCm, int64 MarginCm,
	TConstArrayView<FVoxelLandM5ClosedBoxCm> Obstacles, int64 MaxChecks, TFunctionRef<bool()> IsCancelled);
}
