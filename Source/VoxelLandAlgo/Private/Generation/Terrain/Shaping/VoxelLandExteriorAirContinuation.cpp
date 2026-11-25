#include "Generation/Terrain/Shaping/VoxelLandExteriorAirContinuation.h"
#include "Data/Caves/Planning/VoxelLandCaveWorldSnapshot.h"
#include "Generation/Field/VoxelLandGenerator.h"
#include "Generation/Terrain/Heightfield/VoxelLandHeightGrid.h"
#include "Generation/Caves/Field/VoxelLandCaveBoundBaseline.h"
#include "Generation/Caves/Infrastructure/VoxelLandCaveBudget.h"
#include "Generation/Caves/Infrastructure/VoxelLandCaveBudgetLease.h"
#include "Generation/Caves/Infrastructure/VoxelLandCaveCheckedMath.h"
#include "Generation/Caves/Field/VoxelLandCaveInfluenceSet.h"
#include "Generation/Caves/Planning/VoxelLandCaveValidatedDefinition.h"

namespace
{
FVoxelLandM5Diagnostic AirError(EVoxelLandM5ErrorCode Code, const TCHAR* Message)
{
	FVoxelLandM5Diagnostic Result;
	Result.Code = Code;
	Result.Stage = TEXT("M4.AirContinuation");
	Result.Message = Message;
	return Result;
}
}
FVoxelLandM4AirContinuation::FVoxelLandM4AirContinuation(TVoxelLandM5ConstRef<FVoxelLandM5BoundBaseline> InBaseline,
	TVoxelLandM5ConstRef<FVoxelLandM5InfluenceSet> InInfluence, FVoxelLandM5BudgetRef InSource,
	FVoxelLandM5BudgetRef InJob, TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe>&& InLease)
	: OwnedBaseline(MoveTemp(InBaseline))
	, OwnedInfluence(MoveTemp(InInfluence))
	, OwnedSource(MoveTemp(InSource))
	, OwnedJob(MoveTemp(InJob))
	, Lease(MoveTemp(InLease))
{
}
FVoxelLandM4AirContinuation::~FVoxelLandM4AirContinuation() = default;
bool FVoxelLandM4AirContinuation::Matches(const FVoxelLandM5BoundBaseline& InBaseline,
	const FVoxelLandM5InfluenceSet& InInfluence, const FVoxelLandM5Budget& InSource,
	const FVoxelLandM5Budget& InJob) const
{
	return &OwnedBaseline.Get() == &InBaseline && &OwnedInfluence.Get() == &InInfluence
		&& &OwnedSource.Get() == &InSource && &OwnedJob.Get() == &InJob && Lease && InJob.OwnsLease(*Lease);
}
FVoxelLandM5Diagnostic FVoxelLandM4AirContinuation::Build(TVoxelLandM5ConstRef<FVoxelLandM5BoundBaseline> InBaseline,
	TVoxelLandM5ConstRef<FVoxelLandM5InfluenceSet> InInfluence, FVoxelLandM5BudgetRef InSource,
	FVoxelLandWorkerContext& Worker, FIntPoint MinXY, FIntPoint MaxXYExclusive, FVoxelLandM5BudgetRef InJob,
	TFunctionRef<bool()> IsCancelled, TVoxelLandM5ConstPtr<FVoxelLandM4AirContinuation>& Out)
{
	using E = EVoxelLandM5ErrorCode;
	using namespace VoxelLand::M5::CheckedMath;
	if (IsCancelled())
	{
		return AirError(E::Cancelled, TEXT("Air continuation cancelled"));
	}
	if (IsInGameThread() || &InBaseline->GetDefinition() != &InInfluence->GetDefinition()
		|| !InBaseline->UsesBudget(*InSource) || !InInfluence->UsesBudget(*InSource) || &InSource.Get() == &InJob.Get()
		|| !InBaseline->GetGenerator().OwnsWorkerOnCurrentThread(Worker) || MinXY.X >= MaxXYExclusive.X
		|| MinXY.Y >= MaxXYExclusive.Y)
	{
		return AirError(E::InvalidInput, TEXT("Air continuation owner, worker or XY range invalid"));
	}
	const auto& Snapshot = InBaseline->GetDefinition().GetSnapshot();
	const auto& Base = Snapshot.Base;
	int64 JobBytes = 0, JobWork = 0;
	InJob->GetLimits(JobBytes, JobWork);
	if (JobBytes > Snapshot.Caves.Limits.MaxBytes || JobWork > Snapshot.Caves.Limits.MaxValidationWork)
	{
		return AirError(E::InvalidInput, TEXT("Air continuation job budget exceeds source limits"));
	}
	const int64 V = Base.VoxelSizeCm;
	if (V <= 0 || V > 65536)
	{
		return AirError(E::InvalidInput, TEXT("Invalid air continuation voxel size"));
	}
	// Native detail/overhang can fail at unvisited coordinates; their bounded
	// amplitudes alone do not prove provider success over the whole interval.
	if ((Base.HeightDetail.bEnabled && Base.HeightDetail.AmplitudeQ8 != 0)
		|| (Base.bOverhangEnabled && Base.OverhangAmplitudeQ8 != 0))
	{
		return AirError(E::Unproven, TEXT("Whole-domain native noise availability is not proven"));
	}
	const int64 SmoothHalo = Base.Smooth.bEnabled && Base.Smooth.Strength > 0.f ? 1 : 0;
	const int64 XYHalo = SmoothHalo + (Base.bNormalizeSlope ? 1 : 0);
	int64 GridMinX = 0, GridMinY = 0, GridMaxX = 0, GridMaxY = 0;
	InBaseline->GetGrid().GetBoundsCm(GridMinX, GridMinY, GridMaxX, GridMaxY);
	const int64 MinValues[] = {MinXY.X, MinXY.Y}, MaxValues[] = {MaxXYExclusive.X, MaxXYExclusive.Y};
	const int64 GridMin[] = {GridMinX, GridMinY}, GridMax[] = {GridMaxX, GridMaxY};
	for (int32 Axis = 0; Axis < 2; ++Axis)
	{
		int64 A = 0, B = 0;
		if (!TrySubtract(MinValues[Axis], XYHalo, A) || !TryAdd(MaxValues[Axis], XYHalo - 1, B) || !TryMultiply(A, V, A)
			|| !TryMultiply(B, V, B))
		{
			return AirError(E::ArithmeticOverflow, TEXT("Air continuation XY stencil overflow"));
		}
		if (A < GridMin[Axis] || B > GridMax[Axis] || A < -2000000 || B > 2000000)
		{
			return AirError(E::OutOfDomain, TEXT("Air continuation XY stencil is outside finite source"));
		}
	}
	int32 MinHeight = 0, MaxHeight = 0;
	InBaseline->GetGrid().GetHeightRangeQ8(MinHeight, MaxHeight);
	if (MinHeight < -512000000 || MaxHeight > 512000000 || MinHeight > MaxHeight)
	{
		return AirError(E::Unproven, TEXT("Finite height bound unavailable"));
	}
	if (Base.bNormalizeSlope)
	{
		// The slope operator rejects either Q12 component outside +/-1048576.
		// The global height span bounds every stencil difference; a ceiling
		// bounds its rounding even at ties without sampling the XY domain.
		int64 Span = 0, Numerator = 0, Den = 0;
		if (!TrySubtract(MaxHeight, MinHeight, Span) || !TryMultiplyNonNegative(Span, 4096, Numerator)
			|| !TryMultiplyNonNegative(V, 512, Den) || !TryAdd(Numerator, Den - 1, Numerator))
		{
			return AirError(E::ArithmeticOverflow, TEXT("Air continuation slope validity overflow"));
		}
		if (Numerator / Den > 1048576)
		{
			return AirError(E::Unproven, TEXT("Whole-domain slope validity is not proven"));
		}
	}
	// Noise is inactive above, so detail amplitude A and displacement band B
	// are both zero. R ensures quantization cannot round positive Q8 to zero.
	const int64 R = (V + 7) / 8;
	int64 D = R, Twice = 0, Product = 0;
	if (Base.bNormalizeSlope)
	{
		if (!TryMultiply(R, 2, Twice) || !TrySubtract(Twice, 1, Twice) || !TryMultiply(Twice, 1482917, Product)
			|| !TryAdd(Product, 8191, Product))
		{
			return AirError(E::ArithmeticOverflow, TEXT("Air continuation slope bound overflow"));
		}
		D = Product / 8192;
	}
	int64 Threshold = 0, Denominator = 0, Raw = 0, First = 0, Last = 0;
	if (!TryAdd(MaxHeight, D, Threshold) || !TryMultiply(256, V, Denominator)
		|| !TryFloorDivide(Threshold, Denominator, Raw))
	{
		return AirError(E::ArithmeticOverflow, TEXT("Air continuation height bound overflow"));
	}
	if (Threshold % Denominator != 0 && !TryAdd(Raw, 1, Raw))
	{
		return AirError(E::ArithmeticOverflow, TEXT("Air continuation ceiling overflow"));
	}
	if (!TryAdd(Raw, SmoothHalo, First) || !TryFloorDivide(2000000, V, Last) || !TrySubtract(Last, SmoothHalo, Last))
	{
		return AirError(E::ArithmeticOverflow, TEXT("Air continuation Z interval overflow"));
	}
	if (First > Last)
	{
		return AirError(E::Unproven, TEXT("No supported positive continuation interval"));
	}
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> OutputLease;
	if (!InJob->TryChargeWork(64) || !InJob->TryReserveBytes(sizeof(FVoxelLandM4AirContinuation) + 512, OutputLease))
	{
		return AirError(E::BudgetExceeded, TEXT("Air continuation admission exhausted"));
	}
	if (IsCancelled())
	{
		return AirError(E::Cancelled, TEXT("Air continuation cancelled"));
	}
	TSharedPtr<FVoxelLandM4AirContinuation, ESPMode::ThreadSafe> Built =
		MakeShareable(new FVoxelLandM4AirContinuation(InBaseline, InInfluence, InSource, InJob, MoveTemp(OutputLease)));
	Built->XYMin = MinXY;
	Built->XYMax = MaxXYExclusive;
	Built->FirstZ = First;
	Built->LastZ = Last;
	if (IsCancelled())
	{
		return AirError(E::Cancelled, TEXT("Air continuation cancelled"));
	}
	Out = MoveTemp(Built);
	return {};
}
