#include "Generation/VoxelLandM5BoundBaseline.h"
#include "Generation/VoxelLandGenerator.h"
#include "Generation/VoxelLandM4HeightGrid.h"
#include "Generation/VoxelLandM5Budget.h"
#include "Generation/VoxelLandM5BudgetLease.h"
#include "Generation/VoxelLandM5ValidatedDefinition.h"

FVoxelLandM5BoundBaseline::FVoxelLandM5BoundBaseline(TVoxelLandM5ConstRef<FVoxelLandM5ValidatedDefinition> InDefinition,
	TVoxelLandM5ConstRef<FVoxelLandGenerator> InGenerator, TVoxelLandM5ConstRef<FVoxelLandM4HeightGrid> InGrid,
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe>&& InScratchLease,
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe>&& InLease)
	: Lease(MoveTemp(InLease)), ScratchLease(MoveTemp(InScratchLease)), Definition(MoveTemp(InDefinition)),
	  Generator(MoveTemp(InGenerator)), Grid(MoveTemp(InGrid))
{
}
FVoxelLandM5BoundBaseline::~FVoxelLandM5BoundBaseline() = default;
bool FVoxelLandM5BoundBaseline::UsesBudget(const FVoxelLandM5Budget& Budget) const
{
	return Definition->UsesBudget(Budget) && Lease && ScratchLease && Budget.OwnsLease(*Lease) && Budget.OwnsLease(*ScratchLease);
}
