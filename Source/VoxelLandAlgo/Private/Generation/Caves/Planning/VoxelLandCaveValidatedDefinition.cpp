#include "Generation/Caves/Planning/VoxelLandCaveValidatedDefinition.h"

bool FVoxelLandM5ValidatedDefinition::MatchesDescriptor(TConstArrayView<uint8> Descriptor) const
{
	if (CanonicalBytes.Num() < 12 || CanonicalBytes[0] != 'V' || CanonicalBytes[1] != 'L' || CanonicalBytes[2] != 'D'
		|| CanonicalBytes[3] != '5')
	{
		return false;
	}
	const uint32 Version = uint32(CanonicalBytes[4]) | (uint32(CanonicalBytes[5]) << 8)
						 | (uint32(CanonicalBytes[6]) << 16) | (uint32(CanonicalBytes[7]) << 24);
	const uint32 Size = uint32(CanonicalBytes[8]) | (uint32(CanonicalBytes[9]) << 8)
					  | (uint32(CanonicalBytes[10]) << 16) | (uint32(CanonicalBytes[11]) << 24);
	return Version == 1 && Size == uint32(Descriptor.Num()) && int64(Size) + 62 == CanonicalBytes.Num()
		&& FMemory::Memcmp(CanonicalBytes.GetData() + 12, Descriptor.GetData(), Size) == 0;
}

FVoxelLandM5ValidatedDefinition::FVoxelLandM5ValidatedDefinition(
	TVoxelLandM5ConstRef<FVoxelLandM5WorldSnapshot> InSnapshot,
	TVoxelLandM5ConstPtr<FVoxelLandM5ProtectionGrid> InProtection,
	TVoxelLandM5ConstRef<FVoxelLandOperatorRegistrySnapshot> InRegistry, FVoxelLandM5BudgetRef InBudget,
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe>&& InLease,
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe>&& InFreezeLease)
	: Lease(MoveTemp(InLease))
	, FreezeLease(MoveTemp(InFreezeLease))
	, Snapshot(MoveTemp(InSnapshot))
	, Protection(MoveTemp(InProtection))
	, Registry(MoveTemp(InRegistry))
	, Budget(MoveTemp(InBudget))
{
}
