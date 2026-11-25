#include "Generation/Caves/Field/VoxelLandCaveBoundBaseline.h"
#include "Generation/Field/VoxelLandGenerator.h"
#include "Generation/Terrain/Heightfield/VoxelLandHeightGrid.h"
#include "Generation/Caves/Infrastructure/VoxelLandCaveBudget.h"
#include "Generation/Caves/Infrastructure/VoxelLandCaveBudgetLease.h"
#include "Generation/Caves/Planning/VoxelLandCaveValidatedDefinition.h"

FVoxelLandM5BoundBaseline::FVoxelLandM5BoundBaseline(TVoxelLandM5ConstRef<FVoxelLandM5ValidatedDefinition> InDefinition,
	TVoxelLandM5ConstRef<FVoxelLandGenerator> InGenerator, TVoxelLandM5ConstRef<FVoxelLandM4HeightGrid> InGrid,
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe>&& InScratchLease,
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe>&& InLease)
	: Lease(MoveTemp(InLease))
	, ScratchLease(MoveTemp(InScratchLease))
	, Definition(MoveTemp(InDefinition))
	, Generator(MoveTemp(InGenerator))
	, Grid(MoveTemp(InGrid))
{
}
FVoxelLandM5BoundBaseline::~FVoxelLandM5BoundBaseline() = default;
bool FVoxelLandM5BoundBaseline::UsesBudget(const FVoxelLandM5Budget& Budget) const
{
	return Definition->UsesBudget(Budget) && Lease && ScratchLease && Budget.OwnsLease(*Lease)
		&& Budget.OwnsLease(*ScratchLease);
}
