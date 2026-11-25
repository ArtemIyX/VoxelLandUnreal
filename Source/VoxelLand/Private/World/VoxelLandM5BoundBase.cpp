#include "World/VoxelLandM5BoundBase.h"

#include "Data/Assets/VoxelLandFiniteWorld.h"
#include "Data/Assets/VoxelLandM5ProtectionAsset.h"
#include "Data/Assets/VoxelLandM4WorldConfig.h"
#include "Data/Structs/VoxelLandFiniteWorldSnapshot.h"
#include "Generation/VoxelLandGenerator.h"
#include "Generation/VoxelLandM4GridLease.h"
#include "Generation/VoxelLandM5Budget.h"
#include "Generation/VoxelLandM5BudgetLease.h"
#include "Generation/VoxelLandM5ProtectionGrid.h"
#include "Generation/VoxelLandM5BoundBaseline.h"
#include "Generation/VoxelLandM5ValidatedDefinition.h"
#include "World/VoxelLandM5FiniteBaseScratch.h"
#include "Generation/VoxelLandM5DefinitionInputs.h"
#include "Operators/VoxelLandOperator.h"
#include "World/VoxelLandM5DefinitionValidator.h"
#include "Misc/ScopeLock.h"
#include "UObject/StrongObjectPtr.h"
#include "World/VoxelLandFiniteSnapshotAdapter.h"
#include "World/VoxelLandM4Preparation.h"

class FVoxelLandM5BoundBaseInternals final
{
public:
	// A logical per-capture allowance for snapshot and identity bookkeeping. It is
	// not a measurement or process-wide bound on M4 compiler/provider memory.
	static constexpr int64 M5BoundBaseMetadataAllowance = 48ll * 1024 * 1024;

	static bool NeverCancelled() { return false; }

	static FVoxelLandM5Diagnostic M5BoundBaseFailure(EVoxelLandM5ErrorCode Code, const FString& Message)
	{
		FVoxelLandM5Diagnostic Failure;
		Failure.Code = Code;
		Failure.Stage = TEXT("M5.BoundBase");
		Failure.Message = Message;
		return Failure;
	}
};

FVoxelLandM5BaseCapture::FVoxelLandM5BaseCapture(TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> InBudget,
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe>&& InMetadataLease,
	TSharedPtr<const FVoxelLandM4GridLease, ESPMode::ThreadSafe>&& InGridLease,
	TSharedPtr<const FVoxelLandM4GridLease, ESPMode::ThreadSafe>&& InValidationLease,
	FVoxelLandM5WorldSnapshot&& InSnapshot,
	TSharedPtr<const FVoxelLandM5ProtectionGrid, ESPMode::ThreadSafe>&& InProtectionGrid)
	: Budget(MoveTemp(InBudget))
	, MetadataLease(MoveTemp(InMetadataLease))
	, GridLease(MoveTemp(InGridLease))
	, ValidationLease(MoveTemp(InValidationLease))
	, Snapshot(MoveTemp(InSnapshot))
	, ProtectionGrid(MoveTemp(InProtectionGrid))
{
}

FVoxelLandM5Diagnostic FVoxelLandM5BaseCapture::Capture(UVoxelLandM4WorldConfig& Config,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> InBudget,
	TSharedPtr<const FVoxelLandM5BaseCapture, ESPMode::ThreadSafe>& OutCapture)
{
	if (!IsInGameThread())
	{
		return FVoxelLandM5BoundBaseInternals::M5BoundBaseFailure(
			EVoxelLandM5ErrorCode::InvalidInput, TEXT("M5 base capture requires the game thread"));
	}
	// Graph capture may load assets and reenter game-thread code.
	TStrongObjectPtr<UVoxelLandM4WorldConfig> ConfigGuard(&Config);
	int64 MaxBytes = 0, MaxWork = 0;
	InBudget->GetLimits(MaxBytes, MaxWork);
	if (MaxBytes > Config.Caves.Limits.MaxBytes || MaxWork > Config.Caves.Limits.MaxValidationWork)
	{
		return FVoxelLandM5BoundBaseInternals::M5BoundBaseFailure(
			EVoxelLandM5ErrorCode::InvalidInput, TEXT("M5 budget exceeds the authored limits"));
	}
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Metadata;
	if (!InBudget->TryReserveBytes(FVoxelLandM5BoundBaseInternals::M5BoundBaseMetadataAllowance, Metadata))
	{
		return FVoxelLandM5BoundBaseInternals::M5BoundBaseFailure(
			EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("M5 base metadata admission failed"));
	}
	// Reserve real M4 admission before capture. Prepare claims it once and is
	// responsible for transferring or releasing it after the producer finishes.
	auto GridLease = VoxelLandM4Preparation::Reserve();
	if (!GridLease)
	{
		return FVoxelLandM5BoundBaseInternals::M5BoundBaseFailure(
			EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("M4 preparation reservation unavailable"));
	}
	auto ValidationLease = VoxelLandM4Preparation::ReserveValidation();
	if (!ValidationLease)
	{
		return FVoxelLandM5BoundBaseInternals::M5BoundBaseFailure(
			EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("M4 snapshot validation permit unavailable"));
	}
	FVoxelLandM5WorldSnapshot Snapshot;
	FString Error;
	if (!Config.CaptureM5Snapshot(Snapshot, Error))
	{
		return FVoxelLandM5BoundBaseInternals::M5BoundBaseFailure(EVoxelLandM5ErrorCode::InvalidInput, Error);
	}
	// Capture can reenter game-thread code. Compare the budget against the
	// settings that were actually captured, not only the earlier live values.
	if (MaxBytes > Snapshot.Caves.Limits.MaxBytes || MaxWork > Snapshot.Caves.Limits.MaxValidationWork)
	{
		return FVoxelLandM5BoundBaseInternals::M5BoundBaseFailure(
			EVoxelLandM5ErrorCode::InvalidInput, TEXT("M5 budget exceeds captured limits"));
	}
	TSharedPtr<const FVoxelLandM5BaseCapture, ESPMode::ThreadSafe> Result = MakeShareable(new FVoxelLandM5BaseCapture(
		InBudget, MoveTemp(Metadata), MoveTemp(GridLease), MoveTemp(ValidationLease), MoveTemp(Snapshot), {}));
	OutCapture = MoveTemp(Result);
	return {};
}

FVoxelLandM5Diagnostic FVoxelLandM5BaseCapture::Capture(UVoxelLandFiniteWorld& Definition,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> InBudget,
	TSharedPtr<const FVoxelLandM5BaseCapture, ESPMode::ThreadSafe>& OutCapture)
{
	if (!IsInGameThread() || !IsValid(&Definition))
	{
		return FVoxelLandM5BoundBaseInternals::M5BoundBaseFailure(
			EVoxelLandM5ErrorCode::InvalidInput, TEXT("M5 finite capture requires a valid asset and the game thread"));
	}
	TStrongObjectPtr<UVoxelLandFiniteWorld> DefinitionGuard(&Definition);
	UVoxelLandM5ProtectionAsset* const AuthoredMask = Definition.ProtectionMask.Get();
	if (AuthoredMask && !IsValid(AuthoredMask))
	{
		return FVoxelLandM5BoundBaseInternals::M5BoundBaseFailure(
			EVoxelLandM5ErrorCode::InvalidInput, TEXT("Invalid finite protection asset"));
	}
	TStrongObjectPtr<UVoxelLandM5ProtectionAsset> MaskGuard(AuthoredMask);
	int64 MaxBytes = 0;
	int64 MaxWork = 0;
	InBudget->GetLimits(MaxBytes, MaxWork);
	if (MaxBytes > Definition.Common.Caves.Limits.MaxBytes
		|| MaxWork > Definition.Common.Caves.Limits.MaxValidationWork)
	{
		return FVoxelLandM5BoundBaseInternals::M5BoundBaseFailure(
			EVoxelLandM5ErrorCode::InvalidInput, TEXT("M5 budget exceeds the authored limits"));
	}
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Metadata;
	if (!InBudget->TryReserveBytes(FVoxelLandM5BoundBaseInternals::M5BoundBaseMetadataAllowance, Metadata))
	{
		return FVoxelLandM5BoundBaseInternals::M5BoundBaseFailure(
			EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("M5 base metadata admission failed"));
	}
	TSharedPtr<const FVoxelLandM4GridLease, ESPMode::ThreadSafe> GridLease = VoxelLandM4Preparation::Reserve();
	if (!GridLease)
	{
		return FVoxelLandM5BoundBaseInternals::M5BoundBaseFailure(
			EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("M4 preparation reservation unavailable"));
	}
	TSharedPtr<const FVoxelLandM4GridLease, ESPMode::ThreadSafe> ValidationLease =
		VoxelLandM4Preparation::ReserveValidation();
	if (!ValidationLease)
	{
		return FVoxelLandM5BoundBaseInternals::M5BoundBaseFailure(
			EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("M4 snapshot validation permit unavailable"));
	}
	FVoxelLandFiniteWorldSnapshot Finite;
	FString Error;
	if (!Definition.CaptureSnapshot(Finite, Error))
	{
		return FVoxelLandM5BoundBaseInternals::M5BoundBaseFailure(EVoxelLandM5ErrorCode::InvalidInput, Error);
	}
	// Graph capture can reenter game-thread code. Do not combine a snapshot
	// with a different mask assignment from the one present at entry.
	if (Definition.ProtectionMask.Get() != AuthoredMask || (AuthoredMask && !IsValid(AuthoredMask)))
	{
		return FVoxelLandM5BoundBaseInternals::M5BoundBaseFailure(
			EVoxelLandM5ErrorCode::InvalidInput, TEXT("Finite protection asset changed during capture"));
	}
	if (!AuthoredMask && Finite.Common.Caves.Protection.bRequireMask)
	{
		return FVoxelLandM5BoundBaseInternals::M5BoundBaseFailure(
			EVoxelLandM5ErrorCode::InvalidInput, TEXT("Required finite protection asset is missing"));
	}
	if (!Finite.Common.Caves.bEnabled && !Finite.Common.Caves.Pockets.bEnabled)
	{
		return FVoxelLandM5BoundBaseInternals::M5BoundBaseFailure(
			EVoxelLandM5ErrorCode::InvalidInput, TEXT("M5 capture requires an enabled cave network or pocket stage"));
	}
	if (MaxBytes > Finite.Common.Caves.Limits.MaxBytes || MaxWork > Finite.Common.Caves.Limits.MaxValidationWork)
	{
		return FVoxelLandM5BoundBaseInternals::M5BoundBaseFailure(
			EVoxelLandM5ErrorCode::InvalidInput, TEXT("M5 budget exceeds captured limits"));
	}
	TSharedPtr<const FVoxelLandM5ProtectionGrid, ESPMode::ThreadSafe> ProtectionGrid;
	if (AuthoredMask)
	{
		// Native copy is budgeted and the source remains on this game thread.
		// This call does not pump events; its cancellation callback cannot reenter.
		const FVoxelLandM5Diagnostic MaskResult = FVoxelLandM5ProtectionGrid::Capture(AuthoredMask->CellCounts,
			AuthoredMask->OriginCm, AuthoredMask->SpacingCm, AuthoredMask->Cells, InBudget,
			FVoxelLandM5BoundBaseInternals::NeverCancelled, ProtectionGrid);
		if (MaskResult.Code != EVoxelLandM5ErrorCode::None)
		{
			return MaskResult;
		}
	}
	FVoxelLandM5WorldSnapshot Snapshot;
	if (!FVoxelLandFiniteSnapshotAdapter::TryConvert(Finite, Snapshot.Base, Error))
	{
		return FVoxelLandM5BoundBaseInternals::M5BoundBaseFailure(EVoxelLandM5ErrorCode::InvalidInput, Error);
	}
	Snapshot.FormatVersion = 1;
	Snapshot.Manifest = FVoxelLandM5StageManifest{};
	Snapshot.Caves = MoveTemp(Finite.Common.Caves);
	// Assigned masks must never be silently ignored by current identity code.
	// This is the captured effective requirement, not an authoring mutation.
	if (ProtectionGrid)
	{
		Snapshot.Caves.Protection.bRequireMask = true;
	}
	if (Snapshot.Caves.Water.Policy == EVoxelLandM5WaterPolicy::DryWorld)
	{
		Snapshot.Manifest.Descriptor = ProtectionGrid ? 3 : 2;
	}
	TSharedPtr<const FVoxelLandM5BaseCapture, ESPMode::ThreadSafe> Result = MakeShareable(new FVoxelLandM5BaseCapture(
		InBudget, MoveTemp(Metadata), MoveTemp(GridLease), MoveTemp(ValidationLease), MoveTemp(Snapshot),
		MoveTemp(ProtectionGrid)));
	OutCapture = MoveTemp(Result);
	return {};
}

FVoxelLandM5Diagnostic FVoxelLandM5BaseCapture::CheckFiniteProtectionCurrent(
	const UVoxelLandFiniteWorld& Definition, bool& bOutCurrent) const
{
	if (!IsInGameThread() || !IsValid(&Definition))
	{
		return FVoxelLandM5BoundBaseInternals::M5BoundBaseFailure(
			EVoxelLandM5ErrorCode::InvalidInput, TEXT("Protection freshness requires a valid finite world and the game thread"));
	}
	const UVoxelLandM5ProtectionAsset* Mask = Definition.ProtectionMask.Get();
	if (Mask && !IsValid(Mask))
	{
		return FVoxelLandM5BoundBaseInternals::M5BoundBaseFailure(
			EVoxelLandM5ErrorCode::InvalidInput, TEXT("Invalid finite protection asset"));
	}
	if (!Budget->TryChargeWork(1))
	{
		return FVoxelLandM5BoundBaseInternals::M5BoundBaseFailure(
			EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("Protection freshness work budget exhausted"));
	}
	const bool bCurrentRequired = Definition.Common.Caves.Protection.bRequireMask || Mask != nullptr;
	if (Definition.Common.Caves.Protection.bProtectAll != Snapshot.Caves.Protection.bProtectAll
		|| bCurrentRequired != Snapshot.Caves.Protection.bRequireMask || (Mask != nullptr) != ProtectionGrid.IsValid())
	{
		bOutCurrent = false;
		return {};
	}
	if (!Mask)
	{
		// A required mask without a source is never current.
		bOutCurrent = !bCurrentRequired;
		return {};
	}
	const FVoxelLandM5PointCm& Origin = ProtectionGrid->GetOriginCm();
	const TConstArrayView<uint8> Cells = ProtectionGrid->GetCells();
	if (Mask->CellCounts != ProtectionGrid->GetSize() || Mask->OriginCm.X != Origin.X
		|| Mask->OriginCm.Y != Origin.Y || Mask->OriginCm.Z != Origin.Z
		|| Mask->SpacingCm != ProtectionGrid->GetSpacingCm() || Mask->Cells.Num() != Cells.Num())
	{
		bOutCurrent = false;
		return {};
	}
	if (!Budget->TryChargeWork(Cells.Num()))
	{
		return FVoxelLandM5BoundBaseInternals::M5BoundBaseFailure(
			EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("Protection freshness work budget exhausted"));
	}
	// No callbacks or event pumping while the game-thread asset is borrowed.
	bOutCurrent = FMemory::Memcmp(Mask->Cells.GetData(), Cells.GetData(), Cells.Num()) == 0;
	return {};
}

TSharedPtr<const FVoxelLandM4GridLease, ESPMode::ThreadSafe> FVoxelLandM5BaseCapture::ClaimGridLease() const
{
	FScopeLock Lock(&GridLeaseMutex);
	return MoveTemp(GridLease);
}

FVoxelLandM5FiniteNativeSource::FVoxelLandM5FiniteNativeSource(
	TSharedRef<const FVoxelLandM5BaseCapture, ESPMode::ThreadSafe> InCapture, uint32 InGeometryVersion,
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe>&& InLease)
	: Capture(MoveTemp(InCapture)), GeometryVersion(InGeometryVersion), Lease(MoveTemp(InLease)) {}
FVoxelLandM5FiniteNativeSource::~FVoxelLandM5FiniteNativeSource() = default;

FVoxelLandM5Diagnostic FVoxelLandM5BoundBase::CaptureFiniteNative(UVoxelLandFiniteWorld& Asset,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, uint32 GeometryVersion,
	TSharedPtr<const FVoxelLandM5FiniteNativeSource, ESPMode::ThreadSafe>& OutSource)
{
	using E = EVoxelLandM5ErrorCode; using I = FVoxelLandM5BoundBaseInternals;
	if (!IsInGameThread()) return I::M5BoundBaseFailure(E::InvalidInput, TEXT("Native finite capture requires the game thread"));
	if (GeometryVersion != 1 && GeometryVersion != 2) return I::M5BoundBaseFailure(E::InvalidInput, TEXT("Unsupported native geometry version"));
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Lease;
	if (!Budget->TryReserveBytes(4096, Lease)) return I::M5BoundBaseFailure(E::BudgetExceeded, TEXT("Native finite source memory exhausted"));
	TSharedPtr<const FVoxelLandM5BaseCapture, ESPMode::ThreadSafe> Capture;
	const auto Captured = FVoxelLandM5BaseCapture::Capture(Asset, Budget, Capture);
	if (Captured.Code != E::None) return Captured;
	if (!Capture) return I::M5BoundBaseFailure(E::Unproven, TEXT("Native finite capture missing"));
	// The capture is private to this call. Freeze the explicitly selected geometry
	// version before any owner or identity can observe the native snapshot.
	ConstCastSharedPtr<FVoxelLandM5BaseCapture>(Capture)->Snapshot.Manifest.Geometry = GeometryVersion;
	TSharedPtr<const FVoxelLandM5FiniteNativeSource, ESPMode::ThreadSafe> Result = MakeShareable(
		new FVoxelLandM5FiniteNativeSource(Capture.ToSharedRef(), GeometryVersion, MoveTemp(Lease)));
	OutSource = MoveTemp(Result);
	return {};
}

FVoxelLandM5Diagnostic FVoxelLandM5BoundBase::ValidateFiniteNative(
	TSharedRef<const FVoxelLandM5FiniteNativeSource, ESPMode::ThreadSafe> Source,
	TSharedRef<const FVoxelLandOperatorRegistrySnapshot, ESPMode::ThreadSafe> Registry,
	const FVoxelLandM5Bounds& EffectiveBounds, bool bExplicitBounds, bool bAuthoritativeDryWorld,
	TFunctionRef<bool()> IsCancelled, TSharedPtr<const FVoxelLandM5ValidatedDefinition, ESPMode::ThreadSafe>& OutDefinition)
{
	using E = EVoxelLandM5ErrorCode; using I = FVoxelLandM5BoundBaseInternals;
	if (IsInGameThread()) return I::M5BoundBaseFailure(E::InvalidInput, TEXT("Native finite validation requires a worker thread"));
	if (IsCancelled()) return I::M5BoundBaseFailure(E::Cancelled, TEXT("Native finite validation cancelled"));
	const auto Budget = Source->Capture->Budget;
	// Alias the admitted immutable capture without copying; the deleter retains
	// Source for the whole synchronous validation. Validator makes its own
	// metered private snapshot before publishing the definition.
	TSharedRef<const FVoxelLandM5WorldSnapshot, ESPMode::ThreadSafe> Snapshot = MakeShareable(
		&Source->Capture->GetSnapshot(), [Source](const FVoxelLandM5WorldSnapshot*) { (void)Source; });
	FVoxelLandM5DefinitionInputs Inputs{Snapshot, Source->Capture->GetProtectionGrid(), Registry,
		EffectiveBounds, bExplicitBounds, bAuthoritativeDryWorld};
	TSharedPtr<const FVoxelLandM5ValidatedDefinition, ESPMode::ThreadSafe> Definition;
	const auto Result = FVoxelLandM5DefinitionValidator::Validate(Inputs, Budget, IsCancelled, Definition);
	if (Result.Code != E::None) return Result;
	if (!Definition) return I::M5BoundBaseFailure(E::Unproven, TEXT("Native finite definition missing"));
	if (IsCancelled()) return I::M5BoundBaseFailure(E::Cancelled, TEXT("Native finite validation cancelled"));
	auto Mutable = ConstCastSharedPtr<FVoxelLandM5ValidatedDefinition>(Definition);
	Mutable->NativeSource = Source;
	OutDefinition = MoveTemp(Definition);
	return {};
}

FVoxelLandM5Diagnostic FVoxelLandM5BoundBase::BuildFinite(
	TSharedRef<const FVoxelLandM5FiniteNativeSource, ESPMode::ThreadSafe> Source,
	TSharedRef<const FVoxelLandM5ValidatedDefinition, ESPMode::ThreadSafe> Definition,
	TFunctionRef<FVoxelLandM5Diagnostic(const FVoxelLandGenerator&, TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe>,
		TFunctionRef<bool()>, TUniquePtr<FVoxelLandWorkerContext>&)> CreateWorker,
	TFunctionRef<bool()> IsCancelled, TSharedPtr<const FVoxelLandM5BoundBaseline, ESPMode::ThreadSafe>& OutBaseline)
{
	using E = EVoxelLandM5ErrorCode; using I = FVoxelLandM5BoundBaseInternals;
	if (IsInGameThread()) return I::M5BoundBaseFailure(E::InvalidInput, TEXT("Native finite baseline requires a worker thread"));
	if (IsCancelled()) return I::M5BoundBaseFailure(E::Cancelled, TEXT("Native finite baseline cancelled"));
	const auto Budget = Source->Capture->Budget;
	if (Definition->NativeSource.Get() != &Source.Get() || !Definition->UsesBudget(*Budget))
		return I::M5BoundBaseFailure(E::InvalidInput, TEXT("Native finite definition or budget differs from source"));
	auto Process = Source->Capture->ClaimGridLease();
	if (!Process) return I::M5BoundBaseFailure(E::InvalidInput, TEXT("Native finite admission already consumed"));
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Lease;
	if (!Budget->TryReserveBytes(4096, Lease)) return I::M5BoundBaseFailure(E::BudgetExceeded, TEXT("Native finite baseline memory exhausted"));
	TSharedPtr<const FVoxelLandM5FiniteBaseScratch, ESPMode::ThreadSafe> Scratch;
	const auto Result = FVoxelLandM5FiniteBaseScratch::Build(Definition->GetSnapshot().Base, Budget,
		MoveTemp(Process), CreateWorker, IsCancelled, Scratch);
	if (Result.Code != E::None) return Result;
	if (!Scratch) return I::M5BoundBaseFailure(E::Unproven, TEXT("Finite base scratch missing"));
	const auto Generator = Scratch->GetGeneratorShared();
	const auto Grid = Generator->GetM4HeightGrid();
	auto ScratchLease = Scratch->GetLifetimeLease();
	if (!Grid || !ScratchLease || !Budget->OwnsLease(*ScratchLease) || &Scratch->GetGrid() != Grid.Get())
		return I::M5BoundBaseFailure(E::Unproven, TEXT("Finite baseline generator or grid provenance missing"));
	if (IsCancelled()) return I::M5BoundBaseFailure(E::Cancelled, TEXT("Native finite baseline cancelled"));
	TSharedPtr<FVoxelLandM5BoundBaseline, ESPMode::ThreadSafe> Built = MakeShareable(
		new FVoxelLandM5BoundBaseline(Definition, Generator, Grid.ToSharedRef(), MoveTemp(ScratchLease), MoveTemp(Lease)));
	if (IsCancelled()) return I::M5BoundBaseFailure(E::Cancelled, TEXT("Native finite baseline cancelled"));
	OutBaseline = MoveTemp(Built);
	return {};
}


FVoxelLandM5BoundBase::FVoxelLandM5BoundBase(TSharedRef<const FVoxelLandM5BaseCapture, ESPMode::ThreadSafe> InCapture,
	TSharedRef<const FVoxelLandGenerator, ESPMode::ThreadSafe> InGenerator, FVoxelLandM5IdentityRecord&& InIdentity)
	: Capture(MoveTemp(InCapture))
	, Generator(MoveTemp(InGenerator))
	, Identity(MoveTemp(InIdentity))
{
}

FVoxelLandM5Diagnostic FVoxelLandM5BoundBase::Prepare(
	TSharedRef<const FVoxelLandM5BaseCapture, ESPMode::ThreadSafe> InCapture, TFunctionRef<bool()> IsCancelled,
	TSharedPtr<const FVoxelLandM5BoundBase, ESPMode::ThreadSafe>& OutBase)
{
	(void)OutBase; // No output may be published until shared accounting exists.
	if (IsInGameThread())
	{
		return FVoxelLandM5BoundBaseInternals::M5BoundBaseFailure(
			EVoxelLandM5ErrorCode::InvalidInput, TEXT("M5 base preparation requires a worker thread"));
	}
	// A capture has one reservation and may start only one producer. A finite
	// success path must transfer it to its grid; a local path must retain it
	// through actual producer completion and then release it. Neither path is
	// enabled until the M4 identity/prepare work can debit the shared budget.
	const auto ProducerLease = InCapture->ClaimGridLease();
	if (!ProducerLease)
	{
		return FVoxelLandM5BoundBaseInternals::M5BoundBaseFailure(
			EVoxelLandM5ErrorCode::InvalidInput, TEXT("M5 base capture was already consumed"));
	}
	if (IsCancelled())
	{
		return FVoxelLandM5BoundBaseInternals::M5BoundBaseFailure(
			EVoxelLandM5ErrorCode::Cancelled, TEXT("M5 base preparation cancelled"));
	}
	if (!InCapture->Budget->TryChargeWork(1))
	{
		return FVoxelLandM5BoundBaseInternals::M5BoundBaseFailure(
			EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("M5 base preparation work budget exhausted"));
	}
	return FVoxelLandM5BoundBaseInternals::M5BoundBaseFailure(EVoxelLandM5ErrorCode::Unproven,
		TEXT("M4 identity and preparation are not yet metered against the shared M5 budget"));
}
