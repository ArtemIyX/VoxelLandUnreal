#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandGraphSnapshot.h"
#include "Data/Structs/VoxelLandProgramDraft.h"

// Compiles a copied source snapshot; failure always clears OutDraft.
class VOXELLANDALGO_API FVoxelLandGraphCompiler
{
public:
    static bool Compile(const FVoxelLandGraphSnapshot& Snapshot, FVoxelLandProgramDraft& OutDraft, FString& OutError);
};
