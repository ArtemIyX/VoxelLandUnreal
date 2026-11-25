#pragma once
#include "CoreMinimal.h"
#include "Data/Graph/Schema/VoxelLandGraphValueType.h"

// Stack-owned output capability. It never exposes raw output storage to providers.
// Each lane must be written exactly once with the declared type.
class VOXELLANDALGO_API FVoxelLandOperatorOutput
{
public:
	FVoxelLandOperatorOutput(EVoxelLandGraphValueType InType, void* InData, int32 InCount);
	explicit FVoxelLandOperatorOutput(TArrayView<int16> Density);
	FVoxelLandOperatorOutput(const FVoxelLandOperatorOutput&) = delete;
	FVoxelLandOperatorOutput& operator=(const FVoxelLandOperatorOutput&) = delete;
	FVoxelLandOperatorOutput(FVoxelLandOperatorOutput&&) = delete;
	FVoxelLandOperatorOutput& operator=(FVoxelLandOperatorOutput&&) = delete;
	int32 Num() const { return Count; }
	bool WriteFloat(int32 Index, float Value);
	bool WriteInt(int32 Index, int32 Value);
	bool WriteBool(int32 Index, int32 Value);
	bool WriteDensity(int32 Index, int16 Value);
	// Checked four-lane writes accept provider-owned SIMD scratch, never VM storage.
	bool WriteFloat4(int32 Index, const float* Values);
	bool WriteInt4(int32 Index, const int32* Values);
	bool WriteBool4(int32 Index, const int32* Values);
	bool IsComplete(FString& Error) const;

private:
	bool Claim(int32 Index, bool bCorrectType);
	void* Data = nullptr;
	int32 Count = 0;
	EVoxelLandGraphValueType Type = EVoxelLandGraphValueType::Float;
	bool bDensity = false;
	bool bInvalid = false;
	uint64 Written[4] = {};
};
