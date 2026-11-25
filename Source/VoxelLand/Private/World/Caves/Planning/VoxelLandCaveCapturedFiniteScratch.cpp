#include "World/Caves/Planning/VoxelLandCaveCapturedFiniteScratch.h"

#include "Data/Caves/Planning/VoxelLandCaveWorldSnapshot.h"
#include "Generation/Field/VoxelLandGenerator.h"
#include "Generation/Terrain/Worlds/VoxelLandTerrainGridLease.h"
#include "Generation/Caves/Infrastructure/VoxelLandCaveBudget.h"
#include "Generation/Caves/Infrastructure/VoxelLandCaveBudgetLease.h"
#include "Generation/Caves/Field/VoxelLandCaveProtectionGrid.h"
#include "Generation/Caves/Field/VoxelLandCaveBoundBaseline.h"
#include "Generation/Caves/Planning/VoxelLandCaveValidatedDefinition.h"
#include "World/Caves/Field/VoxelLandCaveBoundBase.h"
#include "World/Caves/Planning/VoxelLandCaveFiniteBaseScratch.h"
#include "World/Caves/Persistence/VoxelLandCaveMeteredIdentity.h"

class FVoxelLandM5CapturedFiniteScratchInternals final
{
public:
	static FVoxelLandM5Diagnostic Error(EVoxelLandM5ErrorCode Code, const TCHAR* Message)
	{
		FVoxelLandM5Diagnostic Result;
		Result.Code = Code;
		Result.Stage = TEXT("M5.CapturedFiniteScratch");
		Result.Message = Message;
		return Result;
	}
};

TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> FVoxelLandM5CapturedFiniteScratch::GetBudget() const
{
	return Capture->Budget;
}

FVoxelLandM5Diagnostic FVoxelLandM5CapturedFiniteScratch::Build(
	TSharedRef<const FVoxelLandM5BaseCapture, ESPMode::ThreadSafe> Capture,
	TFunctionRef<FVoxelLandM5Diagnostic(const FVoxelLandGenerator&, TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe>,
		TFunctionRef<bool()>, TUniquePtr<FVoxelLandWorkerContext>&)>
		CreateWorker,
	TFunctionRef<bool()> IsCancelled, TSharedPtr<const FVoxelLandM5CapturedFiniteScratch, ESPMode::ThreadSafe>& Out)
{
	using E = EVoxelLandM5ErrorCode;
	if (IsInGameThread())
	{
		return FVoxelLandM5CapturedFiniteScratchInternals::Error(
			E::InvalidInput, TEXT("Captured finite scratch requires a worker thread"));
	}
	TSharedPtr<const FVoxelLandM4GridLease, ESPMode::ThreadSafe> GridLease = Capture->ClaimGridLease();
	if (!GridLease)
	{
		return FVoxelLandM5CapturedFiniteScratchInternals::Error(
			E::InvalidInput, TEXT("Capture reservation was already consumed"));
	}
	if (IsCancelled())
	{
		return FVoxelLandM5CapturedFiniteScratchInternals::Error(
			E::Cancelled, TEXT("Captured finite scratch cancelled"));
	}
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget = Capture->Budget;
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Lease;
	if (!Budget->TryReserveBytes(4096, Lease))
	{
		return FVoxelLandM5CapturedFiniteScratchInternals::Error(
			E::BudgetExceeded, TEXT("Captured scratch memory exhausted"));
	}
	TSharedPtr<const FVoxelLandM5MeteredIdentity, ESPMode::ThreadSafe> Identity;
	const TSharedPtr<const FVoxelLandM5ProtectionGrid, ESPMode::ThreadSafe> Mask = Capture->GetProtectionGrid();
	FVoxelLandM5Diagnostic Result =
		Mask ? FVoxelLandM5MeteredIdentity::Capture(Capture->GetSnapshot(), *Mask, Budget, IsCancelled, Identity)
			 : FVoxelLandM5MeteredIdentity::Capture(Capture->GetSnapshot(), Budget, IsCancelled, Identity);
	if (Result.Code != E::None)
	{
		return Result;
	}
	if (!Identity)
	{
		return FVoxelLandM5CapturedFiniteScratchInternals::Error(E::Unproven, TEXT("Captured identity missing"));
	}
	TSharedPtr<const FVoxelLandM5FiniteBaseScratch, ESPMode::ThreadSafe> Scratch;
	Result = FVoxelLandM5FiniteBaseScratch::Build(
		Capture->GetSnapshot().Base, Budget, MoveTemp(GridLease), CreateWorker, IsCancelled, Scratch);
	if (Result.Code != E::None)
	{
		return Result;
	}
	if (!Scratch)
	{
		return FVoxelLandM5CapturedFiniteScratchInternals::Error(E::Unproven, TEXT("Finite base scratch missing"));
	}
	if (IsCancelled())
	{
		return FVoxelLandM5CapturedFiniteScratchInternals::Error(
			E::Cancelled, TEXT("Captured finite scratch cancelled"));
	}
	TSharedPtr<FVoxelLandM5CapturedFiniteScratch, ESPMode::ThreadSafe> Prepared =
		MakeShareable(new FVoxelLandM5CapturedFiniteScratch(Capture, MoveTemp(Lease)));
	Prepared->Identity = MoveTemp(Identity);
	Prepared->Scratch = MoveTemp(Scratch);
	if (IsCancelled())
	{
		return FVoxelLandM5CapturedFiniteScratchInternals::Error(
			E::Cancelled, TEXT("Captured finite scratch cancelled"));
	}
	Out = MoveTemp(Prepared);
	return {};
}

FVoxelLandM5Diagnostic FVoxelLandM5CapturedFiniteScratch::BuildBound(
	TSharedRef<const FVoxelLandM5BaseCapture, ESPMode::ThreadSafe> Capture,
	TSharedRef<const FVoxelLandM5ValidatedDefinition, ESPMode::ThreadSafe> Definition,
	TFunctionRef<FVoxelLandM5Diagnostic(const FVoxelLandGenerator&, TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe>,
		TFunctionRef<bool()>, TUniquePtr<FVoxelLandWorkerContext>&)>
		CreateWorker,
	TFunctionRef<bool()> IsCancelled, TSharedPtr<const FVoxelLandM5BoundBaseline, ESPMode::ThreadSafe>& Out)
{
	using E = EVoxelLandM5ErrorCode;
	using I = FVoxelLandM5CapturedFiniteScratchInternals;
	if (IsCancelled())
	{
		return I::Error(E::Cancelled, TEXT("Bound finite baseline cancelled"));
	}
	if (IsInGameThread())
	{
		return I::Error(E::InvalidInput, TEXT("Bound finite baseline requires a worker thread"));
	}
	const auto Budget = Capture->Budget;
	const auto Mask = Capture->GetProtectionGrid();
	if (!Definition->UsesBudget(*Budget) || Definition->GetProtection() != Mask.Get())
	{
		return I::Error(E::InvalidInput, TEXT("Definition budget or protection owner differs from capture"));
	}
	TSharedPtr<const FVoxelLandM5MeteredIdentity, ESPMode::ThreadSafe> Identity;
	const auto IdentityResult =
		Mask ? FVoxelLandM5MeteredIdentity::Capture(Capture->GetSnapshot(), *Mask, Budget, IsCancelled, Identity)
			 : FVoxelLandM5MeteredIdentity::Capture(Capture->GetSnapshot(), Budget, IsCancelled, Identity);
	if (IdentityResult.Code != E::None)
	{
		return IdentityResult;
	}
	if (!Identity)
	{
		return I::Error(E::Unproven, TEXT("Captured descriptor missing"));
	}
	if (!Budget->TryChargeWork(int64(Identity->GetIdentity().CanonicalBytes.Num()) + 64))
	{
		return I::Error(E::BudgetExceeded, TEXT("Captured descriptor comparison exhausted"));
	}
	if (IsCancelled())
	{
		return I::Error(E::Cancelled, TEXT("Bound finite baseline cancelled"));
	}
	if (!Definition->MatchesDescriptor(Identity->GetIdentity().CanonicalBytes))
	{
		return I::Error(E::InvalidInput, TEXT("Captured descriptor differs from validated definition"));
	}
	if (IsCancelled())
	{
		return I::Error(E::Cancelled, TEXT("Bound finite baseline cancelled"));
	}
	TSharedPtr<const FVoxelLandM4GridLease, ESPMode::ThreadSafe> GridLease = Capture->ClaimGridLease();
	if (!GridLease)
	{
		return I::Error(E::InvalidInput, TEXT("Capture reservation was already consumed"));
	}
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Lease;
	if (!Budget->TryReserveBytes(4096, Lease))
	{
		return I::Error(E::BudgetExceeded, TEXT("Bound baseline memory exhausted"));
	}
	TSharedPtr<const FVoxelLandM5FiniteBaseScratch, ESPMode::ThreadSafe> Scratch;
	const auto Result = FVoxelLandM5FiniteBaseScratch::Build(
		Definition->GetSnapshot().Base, Budget, MoveTemp(GridLease), CreateWorker, IsCancelled, Scratch);
	if (Result.Code != E::None)
	{
		return Result;
	}
	if (!Scratch)
	{
		return I::Error(E::Unproven, TEXT("Finite base scratch missing"));
	}
	const auto Generator = Scratch->GetGeneratorShared();
	const auto Grid = Generator->GetM4HeightGrid();
	auto ScratchLease = Scratch->GetLifetimeLease();
	if (!Grid || !ScratchLease || !Budget->OwnsLease(*ScratchLease) || &Scratch->GetGrid() != Grid.Get())
	{
		return I::Error(E::Unproven, TEXT("Finite baseline generator or grid provenance missing"));
	}
	if (IsCancelled())
	{
		return I::Error(E::Cancelled, TEXT("Bound finite baseline cancelled"));
	}
	TSharedPtr<FVoxelLandM5BoundBaseline, ESPMode::ThreadSafe> Built = MakeShareable(new FVoxelLandM5BoundBaseline(
		Definition, Generator, Grid.ToSharedRef(), MoveTemp(ScratchLease), MoveTemp(Lease)));
	if (IsCancelled())
	{
		return I::Error(E::Cancelled, TEXT("Bound finite baseline cancelled"));
	}
	Out = MoveTemp(Built);
	return {};
}
