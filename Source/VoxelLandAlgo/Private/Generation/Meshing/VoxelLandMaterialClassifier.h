#pragma once

#include "CoreMinimal.h"
#include "VoxelLandMaterialCandidates.h"

struct FVoxelLandMaterialAppearance;
struct FVoxelLandMeshVertex;
struct FVoxelLandResolvedMaterialRule;

class FVoxelLandMaterialClassifier final
{
public:
	[[nodiscard]] static bool IsValid(const FVoxelLandMaterialAppearance& Appearance);
	[[nodiscard]] static bool HasSlopeRules(const FVoxelLandMaterialAppearance& Appearance);
	[[nodiscard]] static FVoxelLandMaterialCandidates Sample(const FVoxelLandMeshVertex& Vertex,
		const FVoxelLandMaterialAppearance& Appearance, double OriginZ, bool bHasSlopeRules);

private:
	static double GetSlope(const FVector3f& Normal);
	static double GetActivation(const FVoxelLandResolvedMaterialRule& Rule, double Value);
	static FVoxelLandMaterialCandidates Select(const double* Weights, uint8 SlotCount);
};
