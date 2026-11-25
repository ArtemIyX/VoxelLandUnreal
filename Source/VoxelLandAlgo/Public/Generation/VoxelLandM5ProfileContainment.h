#pragma once

#include "CoreMinimal.h"
#include "Data/Enums/VoxelLandM5ErrorCode.h"

struct FVoxelLandM5CapsuleProfile;
struct FVoxelLandM5FeetProposal;
struct FVoxelLandM5PointCm;
struct FVoxelLandM5Segment;

// Conservative ideal-profile containment only. No meshed-field clearance,
// floor contact, junction continuity, step, entrance or publication proof.
namespace VoxelLand::M5::ProfileContainment
{
VOXELLANDALGO_API EVoxelLandM5ErrorCode Check(const FVoxelLandM5Segment& Segment,
	const FVoxelLandM5CapsuleProfile& Capsule, const FVoxelLandM5PointCm& StartFeet,
	const FVoxelLandM5PointCm& EndFeet);
VOXELLANDALGO_API EVoxelLandM5ErrorCode Propose(const FVoxelLandM5Segment& Segment,
	const FVoxelLandM5CapsuleProfile& Capsule, FVoxelLandM5FeetProposal& Out);
}
