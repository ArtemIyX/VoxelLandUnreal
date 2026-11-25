#include "World/VoxelLandM5DefinitionValidator.h"

#include "Data/Enums/VoxelLandM4DomainMode.h"
#include "Data/Enums/VoxelLandM5WaterPolicy.h"
#include "Data/Structs/VoxelLandCanonicalWriter.h"
#include "Generation/VoxelLandM5DefinitionInputs.h"
#include "Data/Structs/VoxelLandM5WorldSnapshot.h"
#include "Generation/VoxelLandM5Budget.h"
#include "Generation/VoxelLandM5BudgetLease.h"
#include "Generation/VoxelLandM5CheckedMath.h"
#include "Generation/VoxelLandM5ProtectionGrid.h"
#include "Generation/VoxelLandM5ValidatedDefinition.h"
#include "Operators/VoxelLandOperator.h"
#include "World/VoxelLandM5MeteredIdentity.h"
#include "World/VoxelLandM5IdentityPreflight.h"
#include "World/VoxelLandM5IdentityPreflightLimits.h"
#include "World/VoxelLandM5IdentityPreflightStats.h"

namespace
{
void SanitizeTooltips(FVoxelLandM5WorldSnapshot& Snapshot)
{
	FVoxelLandM4ControlSnapshot* Channels[] = {&Snapshot.Base.Continental, &Snapshot.Base.Mountain, &Snapshot.Base.Peaks};
	for (auto* Channel : Channels)
	{
		for (auto& Parameter : Channel->Source.Parameters) Parameter.Tooltip = FText::GetEmpty();
		for (auto& Function : Channel->Source.Functions)
			for (auto& Parameter : Function.Inputs) Parameter.Tooltip = FText::GetEmpty();
	}
}
}

#if WITH_DEV_AUTOMATION_TESTS
void FVoxelLandM5DefinitionValidator::TestSanitizeTooltips(FVoxelLandM5WorldSnapshot& Snapshot)
{
	SanitizeTooltips(Snapshot);
}
#endif

FVoxelLandM5Diagnostic FVoxelLandM5DefinitionValidator::Validate(const FVoxelLandM5DefinitionInputs& Inputs,
	FVoxelLandM5BudgetRef Budget, TFunctionRef<bool()> IsCancelled,
	TVoxelLandM5ConstPtr<FVoxelLandM5ValidatedDefinition>& Out)
{
	using E = EVoxelLandM5ErrorCode;
	using namespace VoxelLand::M5::CheckedMath;
	const auto Fail = [](E Code, const TCHAR* Message) {
		FVoxelLandM5Diagnostic Result; Result.Code = Code; Result.Stage = TEXT("M5.Definition"); Result.Message = Message;
		return Result;
	};
	if (IsCancelled()) return Fail(E::Cancelled, TEXT("Definition validation cancelled"));
	const auto& Snapshot = Inputs.Snapshot.Get();
	if (IsInGameThread() || Snapshot.Base.Domain.Mode != EVoxelLandM4DomainMode::Finite
		|| Snapshot.Caves.PlanningMode != EVoxelLandM5PlanningMode::Finite
		|| !Inputs.bAuthoritativeDryWorld || Snapshot.Caves.Water.Policy != EVoxelLandM5WaterPolicy::DryWorld
		|| (Snapshot.Manifest.Descriptor != 2 && Snapshot.Manifest.Descriptor != 3))
		return Fail(E::InvalidInput, TEXT("Finite definition requires an authoritative dry-world declaration"));
	if (!Inputs.bExplicitBounds)
		return Fail(E::Unproven, TEXT("Full usable finite domain is not yet established"));
	const auto& Bounds = Inputs.EffectiveBounds;
	const int64 Min[] = {Bounds.MinInclusive.X, Bounds.MinInclusive.Y, Bounds.MinInclusive.Z};
	const int64 Max[] = {Bounds.MaxExclusive.X, Bounds.MaxExclusive.Y, Bounds.MaxExclusive.Z};
	const int64 AuthoredMin[] = {Snapshot.Caves.Bounds.MinInclusive.X, Snapshot.Caves.Bounds.MinInclusive.Y, Snapshot.Caves.Bounds.MinInclusive.Z};
	const int64 AuthoredMax[] = {Snapshot.Caves.Bounds.MaxExclusive.X, Snapshot.Caves.Bounds.MaxExclusive.Y, Snapshot.Caves.Bounds.MaxExclusive.Z};
	for (int32 Axis = 0; Axis < 3; ++Axis)
		if (Min[Axis] < -1048576 || Max[Axis] > 1048577 || Min[Axis] >= Max[Axis]
			|| Min[Axis] < AuthoredMin[Axis] || Max[Axis] > AuthoredMax[Axis])
			return Fail(E::OutOfDomain, TEXT("Effective bounds exceed the declared finite geometry domain"));
	const auto& Domain = Snapshot.Base.Domain;
	int64 Width = 0, Height = 0, EndX = 0, EndY = 0;
	if (Domain.CellsX < 1 || Domain.CellsY < 1 || Domain.GridSpacingCm < 1
		|| !TryMultiply(Domain.CellsX, Domain.GridSpacingCm, Width) || !TryMultiply(Domain.CellsY, Domain.GridSpacingCm, Height)
		|| !TryAdd(Domain.OriginXCm, Width, EndX) || !TryAdd(Domain.OriginYCm, Height, EndY)
		|| Min[0] < Domain.OriginXCm || Min[1] < Domain.OriginYCm || Max[0] - 1 > EndX || Max[1] - 1 > EndY)
		return Fail(E::OutOfDomain, TEXT("Effective bounds exceed the finite M4 grid"));
	if (Inputs.Protection)
	{
		const auto& Grid = *Inputs.Protection;
		const int64 Spacing = Snapshot.Base.VoxelSizeCm;
		const auto& Origin = Grid.GetOriginCm();
		if (!Grid.IsOwnedBy(*Budget) || Spacing <= 0 || Grid.GetSpacingCm() != Spacing
			|| Origin.X % Spacing != 0 || Origin.Y % Spacing != 0 || Origin.Z % Spacing != 0)
			return Fail(E::InvalidInput, TEXT("Protection grid has a different budget or lattice"));
	}
	// Inventory every nested dynamic array/string before copying. Opaque soft
	// paths have no admitted deep-copy bound here. Editor-only tooltip histories
	// are stripped from the private copy immediately after its shallow FText copy.
	FVoxelLandM5IdentityPreflightLimits Limits;
	Limits.MaxElements = 100000; Limits.MaxStringCodeUnits = 1000000; Limits.MaxConfigurationBytes = 4000000;
	FVoxelLandM5IdentityPreflightStats Stats;
	const auto Inventory = VoxelLand::M5::IdentityPreflight::Inspect(Snapshot, Limits, Budget, IsCancelled, Stats);
	if (Inventory.Code != E::None) return Inventory;
	if (Stats.bHasOpaqueSoftPaths || !Snapshot.Base.MaterialSetupPath.IsNull())
		return Fail(E::Unproven, TEXT("Opaque soft paths cannot yet be frozen under admission"));
	int64 FreezeBytes = 0, Part = 0, FreezeWork = 0;
	// Every inventoried record is bounded well below 512 bytes. Fourfold
	// allowance covers array growth and nested allocations; strings include
	// terminators via the per-record allowance. Allocator bookkeeping is external.
	if (!TryMultiplyNonNegative(Stats.Elements, 2048, FreezeBytes)
		|| !TryMultiplyNonNegative(Stats.StringCodeUnits, 8, Part) || !TryAdd(FreezeBytes, Part, FreezeBytes)
		|| !TryMultiplyNonNegative(Stats.ConfigurationBytes, 4, Part) || !TryAdd(FreezeBytes, Part, FreezeBytes)
		|| !TryMultiplyNonNegative(Inputs.Registry->Num(), 256, Part) || !TryAdd(FreezeBytes, Part, FreezeBytes)
		|| !TryAdd(FreezeBytes, int64(sizeof(FVoxelLandM5WorldSnapshot)) + 8192, FreezeBytes)
		|| !TryAdd(Stats.Elements, Stats.StringCodeUnits, FreezeWork)
		|| !TryAdd(FreezeWork, Stats.ConfigurationBytes, FreezeWork) || !TryAdd(FreezeWork, Inputs.Registry->Num() + 1, FreezeWork))
		return Fail(E::ArithmeticOverflow, TEXT("Definition freeze admission overflow"));
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> FreezeLease;
	if (!Budget->TryChargeWork(FreezeWork) || !Budget->TryReserveBytes(FreezeBytes, FreezeLease))
		return Fail(E::BudgetExceeded, TEXT("Definition freeze admission exhausted"));
	if (IsCancelled()) return Fail(E::Cancelled, TEXT("Definition validation cancelled"));
	TSharedRef<FVoxelLandM5WorldSnapshot, ESPMode::ThreadSafe> MutableSnapshot = MakeShared<FVoxelLandM5WorldSnapshot, ESPMode::ThreadSafe>(Snapshot);
	SanitizeTooltips(MutableSnapshot.Get());
	TVoxelLandM5ConstRef<FVoxelLandM5WorldSnapshot> FrozenSnapshot = MutableSnapshot;
	TVoxelLandM5ConstRef<FVoxelLandOperatorRegistrySnapshot> FrozenRegistry =
		MakeShared<FVoxelLandOperatorRegistrySnapshot, ESPMode::ThreadSafe>(Inputs.Registry.Get());
	if (IsCancelled()) return Fail(E::Cancelled, TEXT("Definition validation cancelled"));
	TVoxelLandM5ConstPtr<FVoxelLandM5MeteredIdentity> Identity;
	const auto Result = Inputs.Protection
		? FVoxelLandM5MeteredIdentity::Capture(FrozenSnapshot.Get(), *Inputs.Protection, Budget, IsCancelled, Identity)
		: FVoxelLandM5MeteredIdentity::Capture(FrozenSnapshot.Get(), Budget, IsCancelled, Identity);
	if (Result.Code != E::None) return Result;
	if (!Identity) return Fail(E::Unproven, TEXT("Canonical descriptor missing"));
	const auto& Descriptor = Identity->GetIdentity().CanonicalBytes;
	int64 Capacity = 0, Bytes = 0, Work = 0;
	if (!TryAdd(Descriptor.Num(), 128, Capacity) || !TryMultiplyNonNegative(Capacity, 4, Bytes)
		|| !TryAdd(Bytes, 4096, Bytes) || !TryMultiplyNonNegative(Capacity, 2, Work))
		return Fail(E::ArithmeticOverflow, TEXT("Definition identity admission overflow"));
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Lease;
	if (!Budget->TryChargeWork(Work) || !Budget->TryReserveBytes(Bytes, Lease))
		return Fail(E::BudgetExceeded, TEXT("Definition identity admission exhausted"));
	if (IsCancelled()) return Fail(E::Cancelled, TEXT("Definition validation cancelled"));
	FCanonicalWriter Writer;
	Writer.Bytes.Reserve(int32(Capacity));
	Writer.U32(0x35444c56); // VLD5, little endian
	Writer.U32(1); Writer.U32(uint32(Descriptor.Num())); Writer.Bytes.Append(Descriptor);
	Writer.U8(1); // authoritative native dry declaration
	Writer.U8(1); // explicit bounds declaration
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		const uint64 Values[] = {uint64(Min[Axis]), uint64(Max[Axis])};
		for (uint64 Value : Values) { Writer.U32(uint32(Value)); Writer.U32(uint32(Value >> 32)); }
	}
	if (IsCancelled()) return Fail(E::Cancelled, TEXT("Definition validation cancelled"));
	TSharedPtr<FVoxelLandM5ValidatedDefinition, ESPMode::ThreadSafe> Built = MakeShareable(
		new FVoxelLandM5ValidatedDefinition(FrozenSnapshot, Inputs.Protection, FrozenRegistry, Budget, MoveTemp(Lease), MoveTemp(FreezeLease)));
	Built->EffectiveBounds = Bounds;
	Built->CanonicalBytes = MoveTemp(Writer.Bytes);
	if (IsCancelled()) return Fail(E::Cancelled, TEXT("Definition validation cancelled"));
	Out = MoveTemp(Built);
	return {};
}
