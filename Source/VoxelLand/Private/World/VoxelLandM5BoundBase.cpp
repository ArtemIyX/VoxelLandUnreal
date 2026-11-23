#include "World/VoxelLandM5BoundBase.h"

#include "Data/Assets/VoxelLandFiniteWorld.h"
#include "Data/Assets/VoxelLandM4WorldConfig.h"
#include "Data/Structs/VoxelLandFiniteWorldSnapshot.h"
#include "Generation/VoxelLandGenerator.h"
#include "Generation/VoxelLandM4GridLease.h"
#include "Generation/VoxelLandM5Budget.h"
#include "Generation/VoxelLandM5BudgetLease.h"
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
	FVoxelLandM5WorldSnapshot&& InSnapshot)
	: Budget(MoveTemp(InBudget))
	, MetadataLease(MoveTemp(InMetadataLease))
	, GridLease(MoveTemp(InGridLease))
	, ValidationLease(MoveTemp(InValidationLease))
	, Snapshot(MoveTemp(InSnapshot))
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
		InBudget, MoveTemp(Metadata), MoveTemp(GridLease), MoveTemp(ValidationLease), MoveTemp(Snapshot)));
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
	FVoxelLandM5WorldSnapshot Snapshot;
	if (!FVoxelLandFiniteSnapshotAdapter::TryConvert(Finite, Snapshot.Base, Error))
	{
		return FVoxelLandM5BoundBaseInternals::M5BoundBaseFailure(EVoxelLandM5ErrorCode::InvalidInput, Error);
	}
	Snapshot.FormatVersion = 1;
	Snapshot.Manifest = FVoxelLandM5StageManifest{};
	Snapshot.Caves = MoveTemp(Finite.Common.Caves);
	TSharedPtr<const FVoxelLandM5BaseCapture, ESPMode::ThreadSafe> Result = MakeShareable(new FVoxelLandM5BaseCapture(
		InBudget, MoveTemp(Metadata), MoveTemp(GridLease), MoveTemp(ValidationLease), MoveTemp(Snapshot)));
	OutCapture = MoveTemp(Result);
	return {};
}

TSharedPtr<const FVoxelLandM4GridLease, ESPMode::ThreadSafe> FVoxelLandM5BaseCapture::ClaimGridLease() const
{
	FScopeLock Lock(&GridLeaseMutex);
	return MoveTemp(GridLease);
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
