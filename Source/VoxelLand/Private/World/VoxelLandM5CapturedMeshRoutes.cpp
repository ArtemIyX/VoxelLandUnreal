#include "World/VoxelLandM5CapturedMeshRoutes.h"

#include "Data/Structs/VoxelLandM5WorldSnapshot.h"
#include "Generation/VoxelLandM5Budget.h"
#include "Generation/VoxelLandM5BudgetLease.h"
#include "Generation/VoxelLandM5CandidateResult.h"
#include "Generation/VoxelLandM5CheckedMath.h"
#include "Generation/VoxelLandM5FiniteRoutePlanner.h"
#include "Generation/VoxelLandM5MeshFeetProposal.h"
#include "Generation/VoxelLandM5ScratchChunks.h"
#include "Generation/VoxelLandM5ScratchMesh.h"
#include "Generation/VoxelLandM5SupportSelector.h"
#include "Generation/VoxelLandM5RetainedRouteInputs.h"
#include "Generation/VoxelLandM5ScratchRouteChecks.h"
#include "World/VoxelLandM5BoundBase.h"
#include "World/VoxelLandM5CapturedBaseline.h"
#include "World/VoxelLandM5CapturedFiniteCandidate.h"
#include "World/VoxelLandM5CapturedFiniteScratch.h"
#include "World/VoxelLandM5CapturedScratchChunks.h"

class FVoxelLandM5CapturedMeshRoutesInternals final
{
public:
	static FVoxelLandM5Diagnostic Error(EVoxelLandM5ErrorCode Code, const TCHAR* Message)
	{
		FVoxelLandM5Diagnostic Result;
		Result.Code = Code;
		Result.Stage = TEXT("M5.CapturedMeshRoutes");
		Result.Message = Message;
		return Result;
	}
};

FVoxelLandM5Diagnostic FVoxelLandM5CapturedMeshRoutes::Build(
	TSharedRef<const FVoxelLandM5CapturedScratchChunks, ESPMode::ThreadSafe> Source,
	TSharedRef<const FVoxelLandM5ScratchMesh, ESPMode::ThreadSafe> Mesh, int64 MaxSearchDepthCm,
	TFunctionRef<bool()> IsCancelled, TSharedPtr<const FVoxelLandM5CapturedMeshRoutes, ESPMode::ThreadSafe>& Out)
{
	using E = EVoxelLandM5ErrorCode;
	using I = FVoxelLandM5CapturedMeshRoutesInternals;
	using namespace VoxelLand::M5::CheckedMath;
	if (IsCancelled())
	{
		return I::Error(E::Cancelled, TEXT("Captured mesh routes cancelled"));
	}
	if (IsInGameThread() || MaxSearchDepthCm < 1 || MaxSearchDepthCm > 1048576)
	{
		return I::Error(E::InvalidInput, TEXT("Invalid mesh route thread or search depth"));
	}
	const FVoxelLandM5CapturedFiniteCandidate* Proposal = Source->GetCandidate();
	const FVoxelLandM5CapturedBaseline& Baseline = Source->GetSource();
	const FVoxelLandM5CapturedFiniteScratch& Captured = Baseline.GetCaptured();
	const FVoxelLandM5WorldSnapshot& Snapshot = Captured.GetCapture().GetSnapshot();
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget = Captured.GetBudget();
	if (!Proposal || &Proposal->GetSource() != &Baseline || &Mesh->GetSource() != &Source->GetChunks()
		|| !Source->GetChunks().IsOwnedBy(*Budget) || !Mesh->IsOwnedBy(*Budget)
		|| !Snapshot.Caves.bEnabled || Snapshot.Caves.PlanningMode != EVoxelLandM5PlanningMode::Finite)
	{
		return I::Error(E::InvalidInput, TEXT("Mesh routes require the exact captured network and budget"));
	}
	const FVoxelLandM5ScratchChunks& Chunks = Source->GetChunks();
	const int64 Spacing = Chunks.GetVoxelSizeCm();
	if (Spacing < 1 || Spacing > 134217 || Mesh->GetTriangles().Num() == 0)
	{
		return I::Error(E::Unproven, TEXT("Mesh route geometry is empty or unsupported"));
	}
	int64 Min[3] = {}, Max[3] = {};
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		int64 Extent = 0;
		if (!TryMultiply(Chunks.GetMinInclusive()[Axis], 32 * Spacing, Min[Axis])
			|| !TryMultiply(Chunks.GetMaxExclusive()[Axis], 32 * Spacing, Max[Axis])
			|| !TryMultiply(2, Spacing, Extent) || !TryAdd(Min[Axis], Extent, Min[Axis])
			|| !TrySubtract(Max[Axis], Extent, Max[Axis]) || Min[Axis] >= Max[Axis])
		{
			return I::Error(E::OutOfDomain, TEXT("Mesh route ownership bounds are invalid"));
		}
	}
	if (!Budget->TryChargeWork(64))
	{
		return I::Error(E::BudgetExceeded, TEXT("Mesh route setup work exhausted"));
	}
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Lease;
	if (!Budget->TryReserveBytes(4096, Lease))
	{
		return I::Error(E::BudgetExceeded, TEXT("Mesh route wrapper memory exhausted"));
	}
	const auto Resolve = [&Mesh, &Min, &Max, MaxSearchDepthCm](const FVoxelLandM5PointCm& Center,
		TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> SharedBudget, TFunctionRef<bool()> Cancel,
		FVoxelLandM5PointCm& Feet) {
		using namespace VoxelLand::M5::CheckedMath;
		if (Cancel())
		{
			return I::Error(E::Cancelled, TEXT("Mesh feet search cancelled"));
		}
		if (!SharedBudget->TryChargeWork(16))
		{
			return I::Error(E::BudgetExceeded, TEXT("Mesh feet search admission exhausted"));
		}
		int64 Lower = 0;
		if (!TrySubtract(Center.Z, MaxSearchDepthCm, Lower) || Lower < -1048576
			|| Center.X <= Min[0] || Center.X >= Max[0] || Center.Y <= Min[1] || Center.Y >= Max[1]
			|| Lower <= Min[2] || Center.Z >= Max[2])
		{
			return I::Error(E::OutOfDomain, TEXT("Mesh feet search exceeds owned rectangle"));
		}
		return VoxelLand::M5::MeshFeetProposal::Build(Center, Lower, Mesh->GetTriangles(), SharedBudget, Cancel, Feet);
	};
	const FVoxelLandM5RegionCoord Region;
	TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> Routed;
	FVoxelLandM5Diagnostic Result = VoxelLand::M5::FiniteRoutePlanner::Build(Snapshot.Caves, Snapshot.Base.Seed,
		Region, Proposal->GetCandidateShared(), Budget, Resolve, IsCancelled, Routed);
	if (Result.Code != E::None)
	{
		return Result;
	}
	if (!Routed)
	{
		return I::Error(E::Unproven, TEXT("Mesh route proposal missing"));
	}
	if (IsCancelled())
	{
		return I::Error(E::Cancelled, TEXT("Captured mesh routes cancelled"));
	}
	TSharedPtr<FVoxelLandM5CapturedMeshRoutes, ESPMode::ThreadSafe> Built =
		MakeShareable(new FVoxelLandM5CapturedMeshRoutes(MoveTemp(Lease), Source, Mesh));
	Built->Candidate = MoveTemp(Routed);
	if (IsCancelled())
	{
		return I::Error(E::Cancelled, TEXT("Captured mesh routes cancelled"));
	}
	Out = MoveTemp(Built);
	return {};
}

FVoxelLandM5Diagnostic FVoxelLandM5CapturedMeshRoutes::CheckSupport(
	int64 ContactSkinCm, int32 MaxTotalFaces, TFunctionRef<bool()> IsCancelled) const
{
	using E = EVoxelLandM5ErrorCode;
	using I = FVoxelLandM5CapturedMeshRoutesInternals;
	if (IsCancelled()) return I::Error(E::Cancelled, TEXT("Captured mesh routes cancelled"));
	const FVoxelLandM5CapturedFiniteCandidate* Proposal = Source->GetCandidate();
	const FVoxelLandM5CapturedBaseline& Baseline = Source->GetSource();
	const FVoxelLandM5CapturedFiniteScratch& Captured = Baseline.GetCaptured();
	const auto& Capture = Captured.GetCapture();
	const auto& Snapshot = Capture.GetSnapshot();
	const auto Budget = Captured.GetBudget();
	if (!Proposal || !Candidate || &Proposal->GetSource() != &Baseline || &Mesh->GetSource() != &Source->GetChunks())
		return I::Error(E::InvalidInput, TEXT("Mesh routes require the exact captured network"));
	TSharedPtr<const FVoxelLandM5SupportSelector, ESPMode::ThreadSafe> Selector;
	const auto Result = FVoxelLandM5SupportSelector::Build(Snapshot.Caves, Snapshot.Base.Seed,
		Proposal->GetCandidateShared(), Candidate.ToSharedRef(), Mesh, ContactSkinCm, MaxTotalFaces, Budget,
		IsCancelled, Selector);
	if (Result.Code != E::None) return Result;
	if (!Selector || !Selector->MatchesSources(*Mesh, *Candidate))
		return I::Error(E::Unproven, TEXT("Support selector source mismatch"));
	const FVoxelLandM5RetainedRouteInputs Inputs{Snapshot.Caves, Snapshot.Base.Seed, Snapshot.Manifest,
		Baseline.GetWindow(), Proposal->GetCandidateShared(), Candidate.ToSharedRef(), Capture.GetProtectionGrid(),
		*Mesh, ContactSkinCm, Selector->GetSelections()};
	return VoxelLand::M5::ScratchRouteChecks::CheckAll(Inputs, Budget, IsCancelled);
}
