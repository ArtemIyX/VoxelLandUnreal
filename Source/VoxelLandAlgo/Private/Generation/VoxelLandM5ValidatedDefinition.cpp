#include "Generation/VoxelLandM5ValidatedDefinition.h"

FVoxelLandM5ValidatedDefinition::FVoxelLandM5ValidatedDefinition(
	TVoxelLandM5ConstRef<FVoxelLandM5WorldSnapshot> InSnapshot,
	TVoxelLandM5ConstPtr<FVoxelLandM5ProtectionGrid> InProtection,
	TVoxelLandM5ConstRef<FVoxelLandOperatorRegistrySnapshot> InRegistry, FVoxelLandM5BudgetRef InBudget,
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe>&& InLease,
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe>&& InFreezeLease)
	: Lease(MoveTemp(InLease)), FreezeLease(MoveTemp(InFreezeLease)), Snapshot(MoveTemp(InSnapshot)), Protection(MoveTemp(InProtection)),
	  Registry(MoveTemp(InRegistry)), Budget(MoveTemp(InBudget))
{
}
