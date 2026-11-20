#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandSerializedInstruction.h"
#include "Data/Structs/VoxelLandSerializedParameter.h"
#include "Data/Structs/VoxelLandSerializedSeed.h"
#include "Data/Structs/VoxelLandSerializedNoise.h"
#include "Data/Structs/VoxelLandExternalOperator.h"
#include "Data/Structs/VoxelLandSerializedSmoothFilter.h"
#include "VoxelLandSerializedProgram.generated.h"

// This record has an independent envelope version; bytecode retains its own version.
USTRUCT()
struct VOXELLANDDATA_API FVoxelLandSerializedProgram
{
    GENERATED_BODY()

    UPROPERTY()
    int32 EnvelopeVersion = 1;
    UPROPERTY()
    int32 ProgramVersion = 0;
    UPROPERTY()
    int32 CompilerSemanticVersion = 0;
    UPROPERTY()
    int32 ExternalFilterIndex = INDEX_NONE;
    UPROPERTY()
    int32 RegisterCount = 0;
    UPROPERTY()
    int64 SourceFingerprint = 0;
    UPROPERTY()
    int64 ProgramFingerprint = 0;
    UPROPERTY()
    TArray<FVoxelLandSerializedInstruction> Instructions;
    UPROPERTY()
    TArray<float> Constants;
    UPROPERTY()
    TArray<FVoxelLandSerializedParameter> Parameters;
    UPROPERTY()
    TArray<FVoxelLandSerializedSeed> Seeds;
    UPROPERTY()
    TArray<FVoxelLandSerializedNoise> Noise;
    UPROPERTY()
    TArray<FVoxelLandExternalOperator> ExternalOperators;
    UPROPERTY()
    FVoxelLandSerializedSmoothFilter SmoothFilter;
};
