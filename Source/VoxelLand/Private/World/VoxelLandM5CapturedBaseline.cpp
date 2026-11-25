#include "World/VoxelLandM5CapturedBaseline.h"

#include "Generation/VoxelLandM5BaselineWindow.h"
#include "Generation/VoxelLandGenerator.h"
#include "Generation/VoxelLandM5Budget.h"
#include "Generation/VoxelLandM5BudgetLease.h"
#include "World/VoxelLandM5CapturedFiniteScratch.h"
#include "World/VoxelLandM5FiniteBaseScratch.h"

class FVoxelLandM5CapturedBaselineInternals final
{
public:
	static FVoxelLandM5Diagnostic Error(EVoxelLandM5ErrorCode Code, const TCHAR* Message)
	{
		FVoxelLandM5Diagnostic Result;
		Result.Code = Code;
		Result.Stage = TEXT("M5.CapturedBaseline");
		Result.Message = Message;
		return Result;
	}
};

FVoxelLandM5Diagnostic FVoxelLandM5CapturedBaseline::Build(
	TSharedRef<const FVoxelLandM5CapturedFiniteScratch, ESPMode::ThreadSafe> Captured,
	const FIntVector& MinChunk, const FIntVector& MaxChunk, int32 MaxChunks, TFunctionRef<bool()> IsCancelled,
	TSharedPtr<const FVoxelLandM5CapturedBaseline, ESPMode::ThreadSafe>& Out)
{
	using E = EVoxelLandM5ErrorCode;
	if (IsCancelled())
	{
		return FVoxelLandM5CapturedBaselineInternals::Error(E::Cancelled, TEXT("Captured baseline cancelled"));
	}
	if (IsInGameThread())
	{
		return FVoxelLandM5CapturedBaselineInternals::Error(E::InvalidInput, TEXT("Captured baseline requires a worker thread"));
	}
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget = Captured->GetBudget();
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Lease;
	if (!Budget->TryReserveBytes(4096, Lease))
	{
		return FVoxelLandM5CapturedBaselineInternals::Error(E::BudgetExceeded, TEXT("Captured baseline memory exhausted"));
	}
	TSharedPtr<const FVoxelLandM5BaselineWindow, ESPMode::ThreadSafe> Window;
	FVoxelLandM5Diagnostic Result = FVoxelLandM5BaselineWindow::Build(
		Captured->GetScratch().GetGeneratorShared(), MinChunk, MaxChunk, MaxChunks, Budget, IsCancelled, Window);
	if (Result.Code != E::None)
	{
		return Result;
	}
	if (!Window)
	{
		return FVoxelLandM5CapturedBaselineInternals::Error(E::Unproven, TEXT("Baseline window missing"));
	}
	if (IsCancelled())
	{
		return FVoxelLandM5CapturedBaselineInternals::Error(E::Cancelled, TEXT("Captured baseline cancelled"));
	}
	TSharedPtr<FVoxelLandM5CapturedBaseline, ESPMode::ThreadSafe> Built =
		MakeShareable(new FVoxelLandM5CapturedBaseline(Captured, MoveTemp(Lease)));
	Built->Window = MoveTemp(Window);
	if (IsCancelled())
	{
		return FVoxelLandM5CapturedBaselineInternals::Error(E::Cancelled, TEXT("Captured baseline cancelled"));
	}
	Out = MoveTemp(Built);
	return {};
}

FVoxelLandM5Diagnostic FVoxelLandM5CapturedBaseline::BuildBox(
	TSharedRef<const FVoxelLandM5CapturedFiniteScratch, ESPMode::ThreadSafe> Captured,
	FVoxelLandWorkerContext& Worker, const FVoxelLandM5PointCm& MinLattice, const FIntVector& Size,
	TFunctionRef<bool()> IsCancelled, TSharedPtr<const FVoxelLandM5CapturedBaseline, ESPMode::ThreadSafe>& Out)
{
	using E = EVoxelLandM5ErrorCode;
	using I = FVoxelLandM5CapturedBaselineInternals;
	if (IsCancelled()) return I::Error(E::Cancelled, TEXT("Captured baseline cancelled"));
	const auto Generator = Captured->GetScratch().GetGeneratorShared();
	if (!Generator->OwnsWorkerOnCurrentThread(Worker))
		return I::Error(E::InvalidInput, TEXT("Captured baseline requires its exact worker and thread"));
	const auto Budget = Captured->GetBudget();
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Lease;
	if (!Budget->TryReserveBytes(4096, Lease)) return I::Error(E::BudgetExceeded, TEXT("Captured baseline memory exhausted"));
	TSharedPtr<const FVoxelLandM5BaselineWindow, ESPMode::ThreadSafe> Window;
	const auto Result = FVoxelLandM5BaselineWindow::BuildBox(Generator, Worker, MinLattice, Size, Budget, IsCancelled, Window);
	if (Result.Code != E::None) return Result;
	if (!Window) return I::Error(E::Unproven, TEXT("Baseline window missing"));
	if (IsCancelled()) return I::Error(E::Cancelled, TEXT("Captured baseline cancelled"));
	TSharedPtr<FVoxelLandM5CapturedBaseline, ESPMode::ThreadSafe> Built = MakeShareable(new FVoxelLandM5CapturedBaseline(Captured, MoveTemp(Lease)));
	Built->Window = MoveTemp(Window);
	if (IsCancelled()) return I::Error(E::Cancelled, TEXT("Captured baseline cancelled"));
	Out = MoveTemp(Built);
	return {};
}
