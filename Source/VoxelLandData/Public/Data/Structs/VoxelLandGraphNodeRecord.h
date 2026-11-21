#pragma once

#include "CoreMinimal.h"
#include "UObject/SoftObjectPath.h"
#include "Data/Structs/VoxelLandGraphPinRecord.h"
#include "VoxelLandGraphNodeRecord.generated.h"

USTRUCT()
struct VOXELLANDDATA_API FVoxelLandGraphNodeRecord
{
	GENERATED_BODY()

	UPROPERTY()
	FGuid Id;

	// Stable operator identifier, independent of editor presentation and registration order.
	UPROPERTY()
	FGuid OperatorId;

	UPROPERTY()
	int32 OperatorVersion = 1;

	// Salt is interpreted as unsigned bits when deriving a per-node seed.
	UPROPERTY()
	int32 SeedSalt = 0;

	// Operator-specific bytes, requiring canonical validation by a matching provider version.
	UPROPERTY()
	TArray<uint8> Configuration;

	// Empty on built-in and provider nodes. Function calls resolve this path on the game thread.
	UPROPERTY()
	FSoftObjectPath FunctionPath;

	UPROPERTY()
	FGuid ParameterId;

	// Input order is semantic, including for variadic operators.
	UPROPERTY()
	TArray<FVoxelLandGraphPinRecord> Pins;
};
