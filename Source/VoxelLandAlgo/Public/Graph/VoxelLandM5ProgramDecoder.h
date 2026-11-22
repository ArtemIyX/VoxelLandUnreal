#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandM5Diagnostic.h"
#include "Templates/Function.h"

class FVoxelLandM5Budget;
class FVoxelLandM5DecodedDraft;
struct FVoxelLandGraphSnapshot;
struct FVoxelLandSerializedProgram;

// Bounded decoder for the version-1/2 built-in-only program envelope. Function
// sources, non-ASCII names and external operators are rejected as Unproven.
// Successful decoding is NOT program validation or authorization to execute:
// register/type/dependency checks and program fingerprint must still be checked
// by a separately metered validator. OutDraft is unchanged on every failure.
// Reservations cover decoded payload estimates only; caller owns input leases,
// allocator/control-block overhead and any later validator/provider work.
class VOXELLANDALGO_API FVoxelLandM5ProgramDecoder final
{
public:
	static FVoxelLandM5Diagnostic Decode(const FVoxelLandGraphSnapshot& Source,
		const FVoxelLandSerializedProgram& Payload, TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget,
		TFunctionRef<bool()> IsCancelled, TSharedPtr<const FVoxelLandM5DecodedDraft, ESPMode::ThreadSafe>& OutDraft);
};
