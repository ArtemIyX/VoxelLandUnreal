#pragma once

#include "CoreMinimal.h"
#include "VoxelLandM5IdentityRecord.generated.h"

// Full canonical bytes are retained and compared; fingerprint alone is not identity.
USTRUCT()
struct VOXELLANDDATA_API FVoxelLandM5IdentityRecord
{
	GENERATED_BODY()
	UPROPERTY()
	int32 FormatVersion = 1;
	UPROPERTY()
	TArray<uint8> CanonicalBytes;
	UPROPERTY()
	int64 FingerprintBits = 0;
};
