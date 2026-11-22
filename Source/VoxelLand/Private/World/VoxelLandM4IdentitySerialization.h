#pragma once

#include "CoreMinimal.h"

struct FVoxelLandM4IdentityRecord;
struct FVoxelLandM4WorldSnapshot;
class FVoxelLandProgram;

// Internal byte encoder shared by callers which have already validated the
// complete snapshot, requested version and all three matching programs. This
// function alone is not identity validation or an admission/budget boundary.
namespace VoxelLandM4WorldIdentityInternal
{
bool SerializeValidated(const FVoxelLandM4WorldSnapshot& Snapshot, int32 Version, const FVoxelLandProgram& Continental,
	const FVoxelLandProgram& Mountain, const FVoxelLandProgram& Peaks, FVoxelLandM4IdentityRecord& Out, FString& Error);

// As above, with canonical prepass bytes already built and checked by caller.
// The caller must ensure they came from these exact settings and programs.
bool SerializeFromPrepass(const FVoxelLandM4WorldSnapshot& Snapshot, int32 Version,
	const FVoxelLandProgram& Continental, const FVoxelLandProgram& Mountain, const FVoxelLandProgram& Peaks,
	const TArray<uint8>& Candidate, FVoxelLandM4IdentityRecord& Out, FString& Error);
}
