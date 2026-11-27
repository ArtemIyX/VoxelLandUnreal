#pragma once

#include "CoreMinimal.h"
#include "VoxelLandMaterialGraphInstruction.h"

struct FVoxelLandGraphSnapshot;
struct FVoxelLandMaterialGraphPoint;

class VOXELLANDALGO_API FVoxelLandMaterialGraphProgram final
{
public:
	static constexpr int32 MaxInstructions = 256;

	[[nodiscard]] static bool Compile(const FVoxelLandGraphSnapshot& Source, TConstArrayView<FGuid> Palette,
		TSharedPtr<const FVoxelLandMaterialGraphProgram, ESPMode::ThreadSafe>& Out, FString& Error);
	[[nodiscard]] static bool IsSupportedNode(EVoxelLandBuiltinNode Kind);
	[[nodiscard]] static bool ReadGuid(TConstArrayView<uint8> Bytes, FGuid& Out);
	static void WriteGuid(FGuid Id, TArray<uint8>& Out);

	[[nodiscard]] bool Evaluate(const FVoxelLandMaterialGraphPoint& Point, TArrayView<double> Weights) const;
	[[nodiscard]] bool EvaluatePreview(FGuid Node, const FVoxelLandMaterialGraphPoint& Point, double& Out) const;
	TConstArrayView<FVoxelLandMaterialGraphInstruction> GetInstructions() const { return Instructions; }
	bool UsesSlope() const { return bHasSlope; }

private:
	friend class FVoxelLandMaterialGraphCompile;
	friend class FVoxelLandMaskGraphProgram;
	[[nodiscard]] static bool CompileScalarOutput(const FVoxelLandGraphSnapshot& Source, FGuid Output,
		TSharedPtr<const FVoxelLandMaterialGraphProgram, ESPMode::ThreadSafe>& Out, FString& Error);
	TArray<FVoxelLandMaterialGraphInstruction> Instructions;
	uint8 SlotCount = 0;
	bool bHasSlope = false;
};
