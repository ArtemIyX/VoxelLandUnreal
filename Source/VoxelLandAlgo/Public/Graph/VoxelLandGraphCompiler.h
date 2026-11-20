#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandGraphSnapshot.h"
#include "Data/Structs/VoxelLandGraphDiagnostic.h"
#include "Data/Structs/VoxelLandProgramDraft.h"

class FVoxelLandOperatorRegistrySnapshot;

// Compiles a copied source snapshot; failure always clears OutDraft.
class VOXELLANDALGO_API FVoxelLandGraphCompiler
{
public:
    static bool Compile(const FVoxelLandGraphSnapshot& Snapshot, FVoxelLandProgramDraft& OutDraft, FString& OutError);
    static bool Compile(const FVoxelLandGraphSnapshot& Snapshot, const FVoxelLandOperatorRegistrySnapshot& Providers, FVoxelLandProgramDraft& OutDraft, FString& OutError);
    static bool Compile(const FVoxelLandGraphSnapshot& Snapshot, FVoxelLandProgramDraft& OutDraft, FVoxelLandGraphDiagnostic& OutDiagnostic);
    static bool Compile(const FVoxelLandGraphSnapshot& Snapshot, const FVoxelLandOperatorRegistrySnapshot& Providers, FVoxelLandProgramDraft& OutDraft, FVoxelLandGraphDiagnostic& OutDiagnostic);
};
