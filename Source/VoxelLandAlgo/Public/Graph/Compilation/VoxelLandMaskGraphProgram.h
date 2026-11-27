#pragma once

#include "CoreMinimal.h"
#include "Data/Graph/Schema/VoxelLandGraphValueType.h"
#include "Graph/Compilation/VoxelLandMaskGraphOutput.h"

enum class EVoxelLandBuiltinNode : uint32;
class FVoxelLandMaterialGraphProgram;
struct FVoxelLandGraphSnapshot;
struct FVoxelLandMaterialGraphPoint;

class VOXELLANDALGO_API FVoxelLandMaskGraphProgram final
{
public:
	[[nodiscard]] static bool Compile(const FVoxelLandGraphSnapshot& Source,
		TSharedPtr<const FVoxelLandMaskGraphProgram, ESPMode::ThreadSafe>& Out, FString& Error);
	[[nodiscard]] static bool IsSupportedNode(EVoxelLandBuiltinNode Kind);
	[[nodiscard]] bool Evaluate(FGuid OutputId, const FVoxelLandMaterialGraphPoint& Point, double& Out) const;
	TConstArrayView<FVoxelLandMaskGraphOutput> GetOutputs() const { return Outputs; }
	const TMap<FGuid, EVoxelLandGraphValueType>& GetTerrainSignals() const { return TerrainSignals; }

private:
	static constexpr int32 MaxOutputs = 32;
	TArray<FVoxelLandMaskGraphOutput> Outputs;
	TMap<FGuid, EVoxelLandGraphValueType> TerrainSignals;
	TArray<TSharedPtr<const FVoxelLandMaterialGraphProgram, ESPMode::ThreadSafe>> Programs;
};
