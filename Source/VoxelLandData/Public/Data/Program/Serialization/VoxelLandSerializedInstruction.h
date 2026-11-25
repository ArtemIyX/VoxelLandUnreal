#pragma once

#include "CoreMinimal.h"
#include "VoxelLandSerializedInstruction.generated.h"

USTRUCT()
struct VOXELLANDDATA_API FVoxelLandSerializedInstruction
{
	GENERATED_BODY()

	UPROPERTY()
	int32 Opcode = 0;

	UPROPERTY()
	int32 Dst = 0;

	UPROPERTY()
	int32 A = 0;

	UPROPERTY()
	int32 B = 0;

	UPROPERTY()
	int32 C = 0;

	UPROPERTY()
	int32 D = 0;

	UPROPERTY()
	int32 E = 0;

	UPROPERTY()
	int32 F = 0;

	UPROPERTY()
	int32 Immediate = 0;
};
