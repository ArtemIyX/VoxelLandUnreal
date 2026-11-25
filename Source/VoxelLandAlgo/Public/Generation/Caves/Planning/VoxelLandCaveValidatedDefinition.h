#pragma once

#include "CoreMinimal.h"
#include "Data/Caves/Infrastructure/VoxelLandCaveHandles.h"
#include "Data/Caves/Geometry/VoxelLandCaveBounds.h"

struct FVoxelLandM5WorldSnapshot;
class FVoxelLandM5BudgetLease;
class FVoxelLandM5ProtectionGrid;
class FVoxelLandOperatorRegistrySnapshot;
class FVoxelLandM5DefinitionValidator;
class FVoxelLandM5BoundBase;
class FVoxelLandM5FiniteNativeSource;

// Validated native inputs and complete canonical bytes, not a final-field,
// water observation, source freshness, renderer, route or publication proof.
class VOXELLANDALGO_API FVoxelLandM5ValidatedDefinition final
{
public:
	FVoxelLandM5ValidatedDefinition(const FVoxelLandM5ValidatedDefinition&) = delete;
	FVoxelLandM5ValidatedDefinition& operator=(const FVoxelLandM5ValidatedDefinition&) = delete;
	const FVoxelLandM5WorldSnapshot& GetSnapshot() const { return Snapshot.Get(); }
	const FVoxelLandM5Bounds& GetEffectiveBounds() const { return EffectiveBounds; }
	bool HasExplicitBounds() const { return bExplicitBounds; }
	TConstArrayView<uint8> GetCanonicalBytes() const { return CanonicalBytes; }
	// Exact descriptor bytes inside the versioned definition envelope.
	bool MatchesDescriptor(TConstArrayView<uint8> Descriptor) const;
	const FVoxelLandM5ProtectionGrid* GetProtection() const { return Protection.Get(); }
	const FVoxelLandOperatorRegistrySnapshot& GetRegistry() const { return Registry.Get(); }
	bool UsesBudget(const FVoxelLandM5Budget& Other) const { return &Budget.Get() == &Other; }

private:
	friend class FVoxelLandM5DefinitionValidator;
	friend class FVoxelLandM5BoundBase;
	FVoxelLandM5ValidatedDefinition(TVoxelLandM5ConstRef<FVoxelLandM5WorldSnapshot> InSnapshot,
		TVoxelLandM5ConstPtr<FVoxelLandM5ProtectionGrid> InProtection,
		TVoxelLandM5ConstRef<FVoxelLandOperatorRegistrySnapshot> InRegistry, FVoxelLandM5BudgetRef InBudget,
		TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe>&& InLease,
		TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe>&& InFreezeLease);
	// Optional pre-capture native admission, assigned only by its private producer.
	TSharedPtr<const FVoxelLandM5FiniteNativeSource, ESPMode::ThreadSafe> NativeSource;
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Lease;
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> FreezeLease;
	TVoxelLandM5ConstRef<FVoxelLandM5WorldSnapshot> Snapshot;
	TVoxelLandM5ConstPtr<FVoxelLandM5ProtectionGrid> Protection;
	TVoxelLandM5ConstRef<FVoxelLandOperatorRegistrySnapshot> Registry;
	FVoxelLandM5BudgetRef Budget;
	FVoxelLandM5Bounds EffectiveBounds;
	bool bExplicitBounds = false;
	TArray<uint8> CanonicalBytes;
};
