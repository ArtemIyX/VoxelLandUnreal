#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandGraphSnapshot.h"
#include "Data/Structs/VoxelLandSerializedProgram.h"

class FVoxelLandProgram;

// All methods operate only on copied data and do not access UObjects.
class VOXELLANDALGO_API FVoxelLandProgramCodec
{
public:
    static uint64 SourceFingerprint(const FVoxelLandGraphSnapshot& Snapshot);
    static bool Pack(const FVoxelLandGraphSnapshot& Source, const FVoxelLandProgram& Program,
        FVoxelLandSerializedProgram& OutPayload, FString& OutError);
    // Resolves and validates an immutable program. Must run on a worker thread.
    static bool Load(const FVoxelLandGraphSnapshot& Source, const FVoxelLandSerializedProgram& Payload,
        TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe>& OutProgram, FString& OutError);
};
