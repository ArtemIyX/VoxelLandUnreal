#pragma once

#include "CoreMinimal.h"
#include "Data/Caves/Infrastructure/VoxelLandCaveHandles.h"
#include "Data/Caves/Infrastructure/VoxelLandCaveDiagnostic.h"
#include "Data/Caves/Field/VoxelLandCaveInfluenceRange.h"
#include "Templates/Function.h"

struct FVoxelLandChunkKey;
class FVoxelLandM5BudgetLease;
class FVoxelLandM5ValidatedDefinition;

// Declared finite gate and read footprint only. This is not evidence of empty
// influence, final geometry, openings, coverage certification or publication.
class VOXELLANDALGO_API FVoxelLandM5InfluenceSet final
{
public:
	~FVoxelLandM5InfluenceSet();
	FVoxelLandM5InfluenceSet(const FVoxelLandM5InfluenceSet&) = delete;
	FVoxelLandM5InfluenceSet& operator=(const FVoxelLandM5InfluenceSet&) = delete;
	static FVoxelLandM5Diagnostic BuildFinite(TVoxelLandM5ConstRef<FVoxelLandM5ValidatedDefinition> Definition,
		FVoxelLandM5BudgetRef Budget, TFunctionRef<bool()> IsCancelled,
		TVoxelLandM5ConstPtr<FVoxelLandM5InfluenceSet>& Out);
	const FVoxelLandM5ValidatedDefinition& GetDefinition() const { return Definition.Get(); }
	const FVoxelLandM5Bounds& GetDeclaredBounds() const { return DeclaredBounds; }
	const FVoxelLandM5InfluenceRange& GetCoverage() const { return Coverage; }
	TConstArrayView<uint8> GetCanonicalBytes() const { return CanonicalBytes; }
	bool CoversChunk(const FVoxelLandChunkKey& Key) const;
	bool UsesBudget(const FVoxelLandM5Budget& Budget) const;

private:
	FVoxelLandM5InfluenceSet(TVoxelLandM5ConstRef<FVoxelLandM5ValidatedDefinition> InDefinition,
		TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe>&& InLease);
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Lease;
	TVoxelLandM5ConstRef<FVoxelLandM5ValidatedDefinition> Definition;
	FVoxelLandM5Bounds DeclaredBounds;
	FVoxelLandM5InfluenceRange Coverage;
	TArray<uint8> CanonicalBytes;
};
