#include "Generation/Caves/Field/VoxelLandCaveFieldChangeAudit.h"

#include "Data/Field/Chunks/VoxelLandChunkData.h"
#include "Data/Caves/Field/VoxelLandCaveEdgeSamples.h"
#include "Data/Caves/Field/VoxelLandCaveInfluenceRange.h"
#include "Data/Caves/Field/VoxelLandCaveSampleInfluenceRecord.h"
#include "Data/Caves/Planning/VoxelLandCaveWorldSnapshot.h"
#include "Generation/Field/VoxelLandGenerator.h"
#include "Generation/Caves/Field/VoxelLandCaveBoundBaseline.h"
#include "Generation/Caves/Infrastructure/VoxelLandCaveBudget.h"
#include "Generation/Caves/Infrastructure/VoxelLandCaveBudgetLease.h"
#include "Generation/Caves/Infrastructure/VoxelLandCaveCheckedMath.h"
#include "Generation/Caves/Planning/VoxelLandCaveEvaluateChunk.h"
#include "Generation/Caves/Field/VoxelLandCaveFieldInputs.h"
#include "Generation/Caves/Field/VoxelLandCaveInfluenceSet.h"
#include "Generation/Caves/Field/VoxelLandCaveSampleInfluence.h"
#include "Generation/Caves/Planning/VoxelLandCaveValidatedDefinition.h"

class FVoxelLandM5FieldChangeAuditInternals final
{
public:
	static constexpr int64 OwnerBytes = 4096;
	// Includes both inverse enumeration and verification of all edge samples.
	static constexpr int64 ClosureWorkPerChange = 32768;
	static constexpr int64 BaselineBytes = int64(FVoxelLandChunkData::SampleCount) * sizeof(int16) * 2 + 4096;
	static constexpr int32 OverlapSamples =
		3 * FVoxelLandChunkData::SamplesPerAxis * FVoxelLandChunkData::SamplesPerAxis;

	static FVoxelLandM5Diagnostic Error(EVoxelLandM5ErrorCode Code, const TCHAR* Message)
	{
		FVoxelLandM5Diagnostic Result;
		Result.Code = Code;
		Result.Stage = TEXT("M5.FieldChangeAudit");
		Result.Message = Message;
		return Result;
	}

	static bool Contains(const FVoxelLandM5Bounds& Bounds, const FVoxelLandM5PointCm& Point)
	{
		return Point.X >= Bounds.MinInclusive.X && Point.X < Bounds.MaxExclusive.X && Point.Y >= Bounds.MinInclusive.Y
			&& Point.Y < Bounds.MaxExclusive.Y && Point.Z >= Bounds.MinInclusive.Z && Point.Z < Bounds.MaxExclusive.Z;
	}

	static FVoxelLandM5Diagnostic CheckEdge(
		const FVoxelLandM5OwnedLatticeEdge& Edge, const FVoxelLandM5InfluenceSet& Influence, int64 VoxelSizeCm)
	{
		using E = EVoxelLandM5ErrorCode;
		if (!Influence.CoversChunk(Edge.Owner))
		{
			return Error(E::OutOfDomain, TEXT("Changed sample requires an uncovered edge owner"));
		}
		FVoxelLandM5EdgeSamples Dependencies;
		if (!VoxelLand::M5::SampleInfluence::TryBuildEdgeSamples(Edge, Dependencies))
		{
			return Error(E::ArithmeticOverflow, TEXT("Edge dependency overflow"));
		}
		for (const FVoxelLandM5LatticePoint& Sample : Dependencies.Samples)
		{
			FVoxelLandM5PointCm Position;
			if (!VoxelLand::M5::SampleInfluence::TryToCentimetres(Sample, VoxelSizeCm, Position))
			{
				return Error(E::ArithmeticOverflow, TEXT("Edge sample conversion overflow"));
			}
			if (!Contains(Influence.GetCoverage().ReadBoundsCm, Position))
			{
				return Error(E::OutOfDomain, TEXT("Changed edge dependency is outside admitted reads"));
			}
		}
		return {};
	}

	static FVoxelLandM5Diagnostic CheckChange(const FVoxelLandM5LatticePoint& Sample,
		const FVoxelLandM5InfluenceSet& Influence, int64 VoxelSizeCm, FVoxelLandM5Budget& Budget,
		TFunctionRef<bool()> IsCancelled)
	{
		using E = EVoxelLandM5ErrorCode;
		if (IsCancelled())
		{
			return Error(E::Cancelled, TEXT("Field change audit cancelled"));
		}
		if (!Budget.TryChargeWork(ClosureWorkPerChange))
		{
			return Error(E::BudgetExceeded, TEXT("Field change closure work exhausted"));
		}
		FVoxelLandM5SampleInfluence Dependency;
		if (!VoxelLand::M5::SampleInfluence::TryBuild(Sample, Dependency))
		{
			return Error(E::ArithmeticOverflow, TEXT("Changed sample dependency overflow"));
		}
		for (const FVoxelLandM5OwnedLatticeEdge& Edge : Dependency.Edges)
		{
			if (IsCancelled())
			{
				return Error(E::Cancelled, TEXT("Field change audit cancelled"));
			}
			const FVoxelLandM5Diagnostic Result = CheckEdge(Edge, Influence, VoxelSizeCm);
			if (Result.Code != E::None)
			{
				return Result;
			}
		}
		return {};
	}

	static FVoxelLandM5Diagnostic CheckNeighbor(const FVoxelLandM5FieldInputs& Inputs, const FVoxelLandChunkKey& Key,
		int32 Axis, TConstArrayView<int16> Baseline, TConstArrayView<int16> Final, FVoxelLandWorkerContext& Worker,
		FVoxelLandM5BudgetRef Budget, TFunctionRef<bool()> IsCancelled)
	{
		using E = EVoxelLandM5ErrorCode;
		FVoxelLandChunkKey Neighbor = Key;
		++Neighbor.Coordinate[Axis];
		TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe> NeighborFinal;
		FVoxelLandM5Diagnostic Result =
			VoxelLand::M5::FinalField::EvaluateChunk(Inputs, Neighbor, Worker, Budget, IsCancelled, NeighborFinal);
		if (Result.Code != E::None)
		{
			return Result;
		}
		if (!NeighborFinal || !(NeighborFinal->Key == Neighbor)
			|| NeighborFinal->Density.Num() != FVoxelLandChunkData::SampleCount)
		{
			return Error(E::Unproven, TEXT("Neighbor field is incomplete"));
		}
		TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> NeighborLease;
		if (!Budget->TryReserveBytes(BaselineBytes, NeighborLease))
		{
			return Error(E::BudgetExceeded, TEXT("Neighbor comparison storage exhausted"));
		}
		FVoxelLandM5PointCm Origin;
		Origin.X = int64(Neighbor.Coordinate.X) * FVoxelLandChunkData::Cells - 1;
		Origin.Y = int64(Neighbor.Coordinate.Y) * FVoxelLandChunkData::Cells - 1;
		Origin.Z = int64(Neighbor.Coordinate.Z) * FVoxelLandChunkData::Cells - 1;
		TArray<int16> NeighborBaseline;
		Result = Inputs.GetBaseline().GetGenerator().GenerateFiniteM4Box(Origin,
			FIntVector(FVoxelLandChunkData::SamplesPerAxis, FVoxelLandChunkData::SamplesPerAxis,
				FVoxelLandChunkData::SamplesPerAxis),
			Worker, Budget, IsCancelled, NeighborBaseline);
		if (Result.Code != E::None)
		{
			return Result;
		}
		if (NeighborBaseline.Num() != FVoxelLandChunkData::SampleCount)
		{
			return Error(E::Unproven, TEXT("Neighbor baseline is incomplete"));
		}
		if (!Budget->TryChargeWork(2 * OverlapSamples))
		{
			return Error(E::BudgetExceeded, TEXT("Overlap comparison work exhausted"));
		}
		for (int32 Index = 0; Index < OverlapSamples; ++Index)
		{
			if ((Index & 255) == 0 && IsCancelled())
			{
				return Error(E::Cancelled, TEXT("Overlap comparison cancelled"));
			}
			const int32 Plane = Index % 3;
			const int32 U = (Index / 3) % FVoxelLandChunkData::SamplesPerAxis;
			const int32 V = Index / (3 * FVoxelLandChunkData::SamplesPerAxis);
			int32 A[3] = {0, 0, 0};
			int32 B[3] = {0, 0, 0};
			A[Axis] = FVoxelLandChunkData::Cells + Plane;
			B[Axis] = Plane;
			A[(Axis + 1) % 3] = U;
			B[(Axis + 1) % 3] = U;
			A[(Axis + 2) % 3] = V;
			B[(Axis + 2) % 3] = V;
			const int32 Left = A[0] + 35 * (A[1] + 35 * A[2]);
			const int32 Right = B[0] + 35 * (B[1] + 35 * B[2]);
			if (Baseline[Left] != NeighborBaseline[Right] || Final[Left] != NeighborFinal->Density[Right])
			{
				return Error(E::Unproven, TEXT("Neighbor stored overlap differs"));
			}
		}
		return {};
	}

	static FVoxelLandM5Diagnostic CompareChunk(const FVoxelLandM5FieldInputs& Inputs, const FVoxelLandChunkKey& Key,
		FVoxelLandWorkerContext& Worker, FVoxelLandM5BudgetRef Budget, TFunctionRef<bool()> IsCancelled, int64& Changed)
	{
		using E = EVoxelLandM5ErrorCode;
		using namespace VoxelLand::M5::CheckedMath;
		TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe> Final;
		FVoxelLandM5Diagnostic Result =
			VoxelLand::M5::FinalField::EvaluateChunk(Inputs, Key, Worker, Budget, IsCancelled, Final);
		if (Result.Code != E::None)
		{
			return Result;
		}
		if (!Final || !(Final->Key == Key) || Final->Density.Num() != FVoxelLandChunkData::SampleCount)
		{
			return Error(E::Unproven, TEXT("Final chunk is missing or incomplete"));
		}
		TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> BaselineLease;
		if (!Budget->TryReserveBytes(BaselineBytes, BaselineLease))
		{
			return Error(E::BudgetExceeded, TEXT("Baseline comparison storage exhausted"));
		}
		FVoxelLandM5PointCm Origin;
		Origin.X = int64(Key.Coordinate.X) * FVoxelLandChunkData::Cells - 1;
		Origin.Y = int64(Key.Coordinate.Y) * FVoxelLandChunkData::Cells - 1;
		Origin.Z = int64(Key.Coordinate.Z) * FVoxelLandChunkData::Cells - 1;
		TArray<int16> Baseline;
		Result = Inputs.GetBaseline().GetGenerator().GenerateFiniteM4Box(Origin,
			FIntVector(FVoxelLandChunkData::SamplesPerAxis, FVoxelLandChunkData::SamplesPerAxis,
				FVoxelLandChunkData::SamplesPerAxis),
			Worker, Budget, IsCancelled, Baseline);
		if (Result.Code != E::None)
		{
			return Result;
		}
		if (Baseline.Num() != FVoxelLandChunkData::SampleCount)
		{
			return Error(E::Unproven, TEXT("Baseline chunk is incomplete"));
		}
		if (!Budget->TryChargeWork(FVoxelLandChunkData::SampleCount))
		{
			return Error(E::BudgetExceeded, TEXT("Field comparison work exhausted"));
		}
		const int64 VoxelSizeCm = Inputs.GetBaseline().GetDefinition().GetSnapshot().Base.VoxelSizeCm;
		for (int32 Index = 0; Index < FVoxelLandChunkData::SampleCount; ++Index)
		{
			if ((Index & 255) == 0 && IsCancelled())
			{
				return Error(E::Cancelled, TEXT("Field comparison cancelled"));
			}
			if (Baseline[Index] == Final->Density[Index])
			{
				continue;
			}
			FVoxelLandM5LatticePoint Sample;
			Sample.X = Origin.X + Index % FVoxelLandChunkData::SamplesPerAxis;
			Sample.Y = Origin.Y + (Index / FVoxelLandChunkData::SamplesPerAxis) % FVoxelLandChunkData::SamplesPerAxis;
			Sample.Z = Origin.Z + Index / (FVoxelLandChunkData::SamplesPerAxis * FVoxelLandChunkData::SamplesPerAxis);
			Result = CheckChange(Sample, Inputs.GetInfluence(), VoxelSizeCm, *Budget, IsCancelled);
			if (Result.Code != E::None)
			{
				return Result;
			}
			if (!TryAdd(Changed, 1, Changed))
			{
				return Error(E::ArithmeticOverflow, TEXT("Changed sample count overflow"));
			}
		}
		const FVoxelLandM5InfluenceRange& Coverage = Inputs.GetInfluence().GetCoverage();
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			if (int64(Key.Coordinate[Axis]) + 1 >= int64(Coverage.MaxChunkExclusive[Axis]))
			{
				continue;
			}
			Result = CheckNeighbor(Inputs, Key, Axis, Baseline, Final->Density, Worker, Budget, IsCancelled);
			if (Result.Code != E::None)
			{
				return Result;
			}
		}
		return {};
	}
};

FVoxelLandM5FieldChangeAudit::FVoxelLandM5FieldChangeAudit(TVoxelLandM5ConstRef<FVoxelLandM5FieldInputs> InInputs,
	FVoxelLandM5BudgetRef InBudget, TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe>&& InLease)
	: Lease(MoveTemp(InLease))
	, Inputs(MoveTemp(InInputs))
	, Budget(MoveTemp(InBudget))
{
}

FVoxelLandM5FieldChangeAudit::~FVoxelLandM5FieldChangeAudit() = default;

#if WITH_DEV_AUTOMATION_TESTS
FVoxelLandM5Diagnostic FVoxelLandM5FieldChangeAudit::TestCheckSample(const FVoxelLandM5LatticePoint& Sample,
	const FVoxelLandM5InfluenceSet& Influence, int64 VoxelSizeCm, FVoxelLandM5BudgetRef Budget,
	TFunctionRef<bool()> IsCancelled)
{
	return FVoxelLandM5FieldChangeAuditInternals::CheckChange(Sample, Influence, VoxelSizeCm, *Budget, IsCancelled);
}
#endif

FVoxelLandM5Diagnostic FVoxelLandM5FieldChangeAudit::Build(TVoxelLandM5ConstRef<FVoxelLandM5FieldInputs> Inputs,
	FVoxelLandWorkerContext& Worker, FVoxelLandM5BudgetRef JobBudget, TFunctionRef<bool()> IsCancelled,
	TVoxelLandM5ConstPtr<FVoxelLandM5FieldChangeAudit>& Out)
{
	using I = FVoxelLandM5FieldChangeAuditInternals;
	using E = EVoxelLandM5ErrorCode;
	using namespace VoxelLand::M5::CheckedMath;
	if (IsCancelled())
	{
		return I::Error(E::Cancelled, TEXT("Field change audit cancelled"));
	}
	const FVoxelLandM5BoundBaseline& Baseline = Inputs->GetBaseline();
	const FVoxelLandM5InfluenceSet& Influence = Inputs->GetInfluence();
	if (IsInGameThread() || &Baseline.GetDefinition() != &Influence.GetDefinition()
		|| !Baseline.UsesBudget(*Inputs->GetSourceBudget()) || !Influence.UsesBudget(*Inputs->GetSourceBudget())
		|| &JobBudget.Get() == &Inputs->GetSourceBudget().Get()
		|| !Baseline.GetGenerator().OwnsWorkerOnCurrentThread(Worker))
	{
		return I::Error(E::InvalidInput, TEXT("Field audit source or worker mismatch"));
	}
	int64 MaxBytes = 0;
	int64 MaxWork = 0;
	JobBudget->GetLimits(MaxBytes, MaxWork);
	const FVoxelLandM5WorldSnapshot& Snapshot = Baseline.GetDefinition().GetSnapshot();
	if (MaxBytes > Snapshot.Caves.Limits.MaxBytes || MaxWork > Snapshot.Caves.Limits.MaxValidationWork)
	{
		return I::Error(E::InvalidInput, TEXT("Field audit budget exceeds source limits"));
	}
	const FVoxelLandM5InfluenceRange& Coverage = Influence.GetCoverage();
	int64 ChunkCount = 1;
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		const int64 Extent = int64(Coverage.MaxChunkExclusive[Axis]) - int64(Coverage.MinChunk[Axis]);
		if (Extent < 1 || !TryMultiplyNonNegative(ChunkCount, Extent, ChunkCount))
		{
			return I::Error(E::ArithmeticOverflow, TEXT("Field audit coverage overflow"));
		}
	}
	int64 Compared = 0;
	if (!TryMultiplyNonNegative(ChunkCount, FVoxelLandChunkData::SampleCount, Compared))
	{
		return I::Error(E::ArithmeticOverflow, TEXT("Field audit sample count overflow"));
	}
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Lease;
	if (!JobBudget->TryChargeWork(ChunkCount) || !JobBudget->TryReserveBytes(I::OwnerBytes, Lease))
	{
		return I::Error(E::BudgetExceeded, TEXT("Field audit admission exhausted"));
	}
	int64 Changed = 0;
	const int64 SizeX = int64(Coverage.MaxChunkExclusive.X) - Coverage.MinChunk.X;
	const int64 SizeY = int64(Coverage.MaxChunkExclusive.Y) - Coverage.MinChunk.Y;
	for (int64 Index = 0; Index < ChunkCount; ++Index)
	{
		if (IsCancelled())
		{
			return I::Error(E::Cancelled, TEXT("Field change audit cancelled"));
		}
		const int64 X = int64(Coverage.MinChunk.X) + Index % SizeX;
		const int64 Y = int64(Coverage.MinChunk.Y) + (Index / SizeX) % SizeY;
		const int64 Z = int64(Coverage.MinChunk.Z) + (Index / SizeX) / SizeY;
		FVoxelLandChunkKey Key;
		Key.Coordinate = FIntVector(int32(X), int32(Y), int32(Z));
		Key.LOD = 0;
		const FVoxelLandM5Diagnostic Result = I::CompareChunk(*Inputs, Key, Worker, JobBudget, IsCancelled, Changed);
		if (Result.Code != E::None)
		{
			return Result;
		}
	}
	int64 ExaminedEdges = 0;
	if (!TryMultiplyNonNegative(Changed, FVoxelLandM5SampleInfluence::EdgeCount, ExaminedEdges))
	{
		return I::Error(E::ArithmeticOverflow, TEXT("Examined edge count overflow"));
	}
	if (IsCancelled())
	{
		return I::Error(E::Cancelled, TEXT("Field change audit cancelled"));
	}
	TSharedPtr<FVoxelLandM5FieldChangeAudit, ESPMode::ThreadSafe> Built =
		MakeShareable(new FVoxelLandM5FieldChangeAudit(Inputs, JobBudget, MoveTemp(Lease)));
	Built->ComparedSamples = Compared;
	Built->ChangedSamples = Changed;
	Built->ExaminedEdges = ExaminedEdges;
	if (IsCancelled())
	{
		return I::Error(E::Cancelled, TEXT("Field change audit cancelled"));
	}
	Out = MoveTemp(Built);
	return {};
}
