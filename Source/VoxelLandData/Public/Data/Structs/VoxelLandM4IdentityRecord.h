#pragma once

#include "CoreMinimal.h"
#include "VoxelLandM4IdentityRecord.generated.h"

// Persistable identity evidence, not a self-contained world save. The matching
// graph assets and authored settings must also be retained to regenerate.
USTRUCT()
struct VOXELLANDDATA_API FVoxelLandM4IdentityRecord
{
	GENERATED_BODY()
	FVoxelLandM4IdentityRecord();
	// Current descriptor encoding. Other versions are rejected.
	UPROPERTY()
	int32 FormatVersion = 1;
	UPROPERTY()
	TArray<uint8> CanonicalBytes;
	UPROPERTY()
	int64 FingerprintBits = 0;
};
