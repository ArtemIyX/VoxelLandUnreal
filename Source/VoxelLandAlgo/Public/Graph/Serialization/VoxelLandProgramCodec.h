#pragma once

#include "CoreMinimal.h"
#include "Data/Graph/Sources/VoxelLandGraphSnapshot.h"
#include "Data/Program/Serialization/VoxelLandSerializedProgram.h"

class FVoxelLandProgram;
class FVoxelLandOperatorRegistrySnapshot;

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
	static bool Load(const FVoxelLandGraphSnapshot& Source, const FVoxelLandSerializedProgram& Payload,
		const FVoxelLandOperatorRegistrySnapshot& Providers,
		TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe>& OutProgram, FString& OutError);
};
