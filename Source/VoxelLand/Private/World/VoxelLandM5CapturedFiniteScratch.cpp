#include "World/VoxelLandM5CapturedFiniteScratch.h"

#include "Data/Structs/VoxelLandM5WorldSnapshot.h"
#include "Generation/VoxelLandGenerator.h"
#include "Generation/VoxelLandM4GridLease.h"
#include "Generation/VoxelLandM5Budget.h"
#include "Generation/VoxelLandM5BudgetLease.h"
#include "Generation/VoxelLandM5ProtectionGrid.h"
#include "World/VoxelLandM5BoundBase.h"
#include "World/VoxelLandM5FiniteBaseScratch.h"
#include "World/VoxelLandM5MeteredIdentity.h"

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
		TFunctionRef<bool()>, TUniquePtr<FVoxelLandWorkerContext>&)> CreateWorker,
	TFunctionRef<bool()> IsCancelled, TSharedPtr<const FVoxelLandM5CapturedFiniteScratch, ESPMode::ThreadSafe>& Out)
{
	using E = EVoxelLandM5ErrorCode;
	if (IsInGameThread())
	{
		return FVoxelLandM5CapturedFiniteScratchInternals::Error(E::InvalidInput, TEXT("Captured finite scratch requires a worker thread"));
	}
	TSharedPtr<const FVoxelLandM4GridLease, ESPMode::ThreadSafe> GridLease = Capture->ClaimGridLease();
	if (!GridLease)
	{
		return FVoxelLandM5CapturedFiniteScratchInternals::Error(E::InvalidInput, TEXT("Capture reservation was already consumed"));
	}
	if (IsCancelled())
	{
		return FVoxelLandM5CapturedFiniteScratchInternals::Error(E::Cancelled, TEXT("Captured finite scratch cancelled"));
	}
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget = Capture->Budget;
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Lease;
	if (!Budget->TryReserveBytes(4096, Lease))
	{
		return FVoxelLandM5CapturedFiniteScratchInternals::Error(E::BudgetExceeded, TEXT("Captured scratch memory exhausted"));
	}
	TSharedPtr<const FVoxelLandM5MeteredIdentity, ESPMode::ThreadSafe> Identity;
	const TSharedPtr<const FVoxelLandM5ProtectionGrid, ESPMode::ThreadSafe> Mask = Capture->GetProtectionGrid();
	FVoxelLandM5Diagnostic Result = Mask
		? FVoxelLandM5MeteredIdentity::Capture(Capture->GetSnapshot(), *Mask, Budget, IsCancelled, Identity)
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
	Result = FVoxelLandM5FiniteBaseScratch::Build(Capture->GetSnapshot().Base, Budget, MoveTemp(GridLease),
		CreateWorker, IsCancelled, Scratch);
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
		return FVoxelLandM5CapturedFiniteScratchInternals::Error(E::Cancelled, TEXT("Captured finite scratch cancelled"));
	}
	TSharedPtr<FVoxelLandM5CapturedFiniteScratch, ESPMode::ThreadSafe> Prepared =
		MakeShareable(new FVoxelLandM5CapturedFiniteScratch(Capture, MoveTemp(Lease)));
	Prepared->Identity = MoveTemp(Identity);
	Prepared->Scratch = MoveTemp(Scratch);
	if (IsCancelled())
	{
		return FVoxelLandM5CapturedFiniteScratchInternals::Error(E::Cancelled, TEXT("Captured finite scratch cancelled"));
	}
	Out = MoveTemp(Prepared);
	return {};
}
