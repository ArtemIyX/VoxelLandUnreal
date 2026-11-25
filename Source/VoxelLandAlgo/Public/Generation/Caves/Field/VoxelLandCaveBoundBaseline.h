#pragma once

#include "CoreMinimal.h"
#include "Data/Caves/Infrastructure/VoxelLandCaveHandles.h"

class FVoxelLandGenerator;
class FVoxelLandM4HeightGrid;
class FVoxelLandM5Budget;
class FVoxelLandM5BudgetLease;
class FVoxelLandM5ValidatedDefinition;
class FVoxelLandM5CapturedFiniteScratch;
class FVoxelLandM5BoundBase;

// Exact finite generator/grid derived from one validated native definition.
// This is not a final field, dry observation, source freshness or publication proof.
class VOXELLANDALGO_API FVoxelLandM5BoundBaseline final
{
public:
	~FVoxelLandM5BoundBaseline();
	FVoxelLandM5BoundBaseline(const FVoxelLandM5BoundBaseline&) = delete;
	FVoxelLandM5BoundBaseline& operator=(const FVoxelLandM5BoundBaseline&) = delete;
	const FVoxelLandM5ValidatedDefinition& GetDefinition() const { return Definition.Get(); }
	const FVoxelLandGenerator& GetGenerator() const { return Generator.Get(); }
	TVoxelLandM5ConstRef<FVoxelLandGenerator> GetGeneratorShared() const { return Generator; }
	const FVoxelLandM4HeightGrid& GetGrid() const { return Grid.Get(); }
	bool UsesBudget(const FVoxelLandM5Budget& Budget) const;

private:
	friend class FVoxelLandM5CapturedFiniteScratch;
	friend class FVoxelLandM5BoundBase;
	FVoxelLandM5BoundBaseline(TVoxelLandM5ConstRef<FVoxelLandM5ValidatedDefinition> InDefinition,
		TVoxelLandM5ConstRef<FVoxelLandGenerator> InGenerator, TVoxelLandM5ConstRef<FVoxelLandM4HeightGrid> InGrid,
		TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe>&& InScratchLease,
		TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe>&& InLease);
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Lease;
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> ScratchLease;
	TVoxelLandM5ConstRef<FVoxelLandM5ValidatedDefinition> Definition;
	TVoxelLandM5ConstRef<FVoxelLandGenerator> Generator;
	TVoxelLandM5ConstRef<FVoxelLandM4HeightGrid> Grid;
};
