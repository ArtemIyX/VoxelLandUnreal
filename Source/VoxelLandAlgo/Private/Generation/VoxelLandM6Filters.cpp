#include "Generation/VoxelLandGenerator.h"

#include "Generation/VoxelLandM6FilterPlan.h"
#include "Generation/VoxelLandM6Redistance.h"
#include "Generation/VoxelLandM6StampMath.h"
#include "Generation/VoxelLandQuantizedDensity.h"

class FVoxelLandM6FilterExecution final
{
public:
	static FVoxelLandError Apply(const FVoxelLandM6FilterStage& Stage, const FInt64Vector& FirstCm,
		int64 VoxelSizeCm, int32 OutputAxis, const TArray<int16>& Input, int32 InputAxis,
		TArray<int16>& Output, TFunctionRef<bool()> IsCancelled);

private:
	static int32 Index(int32 X, int32 Y, int32 Z, int32 Axis)
	{
		return X + Axis * (Y + Axis * Z);
	}
	static bool Contains(const FVoxelLandM6FilterStage& Stage, const FInt64Vector& Point);
	static int16 Flatten(TConstArrayView<int16> Input, int32 Axis, int32 X, int32 Y, int32 Z, int32 Strength);
	static int16 MakeSpace(int16 Original, const FVoxelLandM6FilterStage& Stage, const FInt64Vector& Point, int64 VoxelSizeCm);
	static bool FindRisingCrossing(int32 Below, int32 Center, int32 Above, int32& OutCrossing);
};

bool FVoxelLandM6FilterExecution::Contains(const FVoxelLandM6FilterStage& Stage, const FInt64Vector& Point)
{
	return Point.X >= Stage.MinCm.X && Point.X <= Stage.MaxCm.X && Point.Y >= Stage.MinCm.Y && Point.Y <= Stage.MaxCm.Y
		&& Point.Z >= Stage.MinCm.Z && Point.Z <= Stage.MaxCm.Z;
}

bool FVoxelLandM6FilterExecution::FindRisingCrossing(int32 Below, int32 Center, int32 Above, int32& OutCrossing)
{
	constexpr int32 UnitsPerVoxel = 1024;
	if (Below < 0 && Center >= 0)
	{
		const int32 Denominator = Center - Below;
		const int64 Numerator = -int64(UnitsPerVoxel) * Denominator - int64(Below) * UnitsPerVoxel;
		OutCrossing = FVoxelLandQuantizedDensity::RoundRatioAwayFromZero(Numerator, Denominator);
		return true;
	}
	if (Center < 0 && Above >= 0)
	{
		OutCrossing = FVoxelLandQuantizedDensity::RoundRatioAwayFromZero(-int64(Center) * UnitsPerVoxel, Above - Center);
		return true;
	}
	return false;
}

int16 FVoxelLandM6FilterExecution::Flatten(
	TConstArrayView<int16> Input, int32 Axis, int32 X, int32 Y, int32 Z, int32 Strength)
{
	int32 CrossingCount = 0;
	int32 CrossingSum = 0;
	const int32 Slice = Axis * Axis;
	for (int32 DY = -1; DY <= 1; ++DY)
	{
		for (int32 DX = -1; DX <= 1; ++DX)
		{
			const int32 CenterIndex = Index(X + DX, Y + DY, Z, Axis);
			int32 Crossing = 0;
			if (FindRisingCrossing(Input[CenterIndex - Slice], Input[CenterIndex], Input[CenterIndex + Slice], Crossing))
			{
				++CrossingCount;
				CrossingSum += Crossing;
			}
		}
	}
	const int16 Original = Input[Index(X, Y, Z, Axis)];
	if (CrossingCount == 0)
	{
		return Original;
	}
	const int32 PlaneDensity = -FVoxelLandQuantizedDensity::RoundRatioAwayFromZero(CrossingSum, CrossingCount);
	return FVoxelLandQuantizedDensity::Blend(Original, PlaneDensity, Strength);
}

int16 FVoxelLandM6FilterExecution::MakeSpace(int16 Original, const FVoxelLandM6FilterStage& Stage,
	const FInt64Vector& Point, int64 VoxelSizeCm)
{
	// The mask is also the clearing box. The maximum signed distance to its
	// six faces is its inside SDF, including odd-width centimetre boxes.
	const FInt64Vector Gap = (Stage.MinCm - Point).ComponentMax(Point - Stage.MaxCm);
	const int64 DistanceCm = FMath::Max3(Gap.X, Gap.Y, Gap.Z);
	constexpr int64 CentimetreScaleQ8 = 256;
	return FMath::Max(Original, FVoxelLandM6StampMath::QuantizeDistanceQ8(DistanceCm * CentimetreScaleQ8, VoxelSizeCm, true));
}

FVoxelLandError FVoxelLandM6FilterExecution::Apply(const FVoxelLandM6FilterStage& Stage, const FInt64Vector& FirstCm,
	int64 VoxelSizeCm, int32 OutputAxis, const TArray<int16>& Input, int32 InputAxis,
	TArray<int16>& Output, TFunctionRef<bool()> IsCancelled)
{
	const int32 Inset = (InputAxis - OutputAxis) / 2;
	const FIntVector InputSize(InputAxis);
	check(Input.Num() == InputAxis * InputAxis * InputAxis);
	check(Inset == (Stage.Kind == EVoxelLandM6FilterKind::MakeSpace ? 0 : 1));
	Output.SetNumUninitialized(OutputAxis * OutputAxis * OutputAxis);
	int32 Destination = 0;
	for (int32 Z = 0; Z < OutputAxis; ++Z)
	{
		for (int32 Y = 0; Y < OutputAxis; ++Y)
		{
			if (IsCancelled())
			{
				return {EVoxelLandErrorCode::Cancelled, TEXT("M6 region filter cancelled")};
			}
			for (int32 X = 0; X < OutputAxis; ++X)
			{
				const FInt64Vector Point = FirstCm + FInt64Vector(X, Y, Z) * VoxelSizeCm;
				int16 Value = Input[Index(X + Inset, Y + Inset, Z + Inset, InputAxis)];
				if (Contains(Stage, Point))
				{
					switch (Stage.Kind)
					{
					case EVoxelLandM6FilterKind::Smooth:
						Value = FVoxelLandQuantizedDensity::SmoothSample(Input, InputSize, X, Y, Z, Stage.Strength);
						break;
					case EVoxelLandM6FilterKind::Flatten:
						Value = Flatten(Input, InputAxis, X + Inset, Y + Inset, Z + Inset, Stage.Strength);
						break;
					case EVoxelLandM6FilterKind::MakeSpace:
						Value = MakeSpace(Value, Stage, Point, VoxelSizeCm);
						break;
					}
				}
				Output[Destination++] = Value;
			}
		}
	}
	return {};
}

FVoxelLandError FVoxelLandGenerator::GenerateM6ProcessedChunk(const FVoxelLandChunkKey& Key, const FVoxelLandM6FilterPlan& Plan,
	FVoxelLandWorkerContext& Worker, TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe>& OutChunk,
	TFunctionRef<bool()> IsCancelled) const
{
	if (IsInGameThread())
	{
		return {EVoxelLandErrorCode::GameThreadNotAllowed, TEXT("M6 pipeline requires a worker thread")};
	}
	if (!OwnsWorkerOnCurrentThread(Worker))
	{
		return {EVoxelLandErrorCode::InvalidContext, TEXT("M6 pipeline worker belongs to a different generator or thread")};
	}
	if (IsCancelled())
	{
		return {EVoxelLandErrorCode::Cancelled, TEXT("M6 pipeline cancelled")};
	}
	if (const FVoxelLandError Error = ValidateM6FilterDemand(Key, Plan))
	{
		return Error;
	}

	const int64 VoxelSizeCm = int64(Profile.VoxelSizeCm);
	const FInt64Vector StoredFirst = FVoxelLandM6StampMath::FirstSampleCm(Key, VoxelSizeCm);
	TArray<int16> Current;
	const FVoxelLandError Baseline = M4Unlimited ? GenerateM6LocalBaselineBox(Key, Plan.Halo, Current, IsCancelled)
		: GenerateM6BaselineBox(Key, Plan.Halo, Worker, Current, IsCancelled);
	if (Baseline)
	{
		return Baseline;
	}

	constexpr int32 StoredAxis = FVoxelLandChunkData::SamplesPerAxis;
	int32 InputAxis = StoredAxis + 2 * Plan.Halo;
	if (!M6Stamps.IsEmpty())
	{
		const FInt64Vector InputFirst = StoredFirst - FInt64Vector(Plan.Halo * VoxelSizeCm);
		if (const FVoxelLandError Error = ApplyM6Stamps(InputFirst, InputAxis, Current, IsCancelled))
		{
			return Error;
		}
	}

	TArray<int16> Next;
	for (int32 Index = 0; Index < M6Filters.Num(); ++Index)
	{
		if (!Plan.HasStage(Index))
		{
			continue;
		}
		const int32 OutputHalo = Plan.OutputHalo[Index];
		const int32 OutputAxis = StoredAxis + 2 * OutputHalo;
		const FInt64Vector First = StoredFirst - FInt64Vector(OutputHalo * VoxelSizeCm);
		if (const FVoxelLandError Error =
				FVoxelLandM6FilterExecution::Apply(M6Filters[Index], First, VoxelSizeCm, OutputAxis, Current, InputAxis,
					Next, IsCancelled))
		{
			return Error;
		}
		Swap(Current, Next);
		InputAxis = OutputAxis;
	}

	if (Plan.RedistanceBand > 0)
	{
		if (const FVoxelLandError Error =
				FVoxelLandM6Redistance::Apply(Current, InputAxis, Plan.RedistanceBand, Next, IsCancelled))
		{
			return Error;
		}
		Swap(Current, Next);
		InputAxis = StoredAxis;
	}
	check(InputAxis == StoredAxis);
	const TSharedRef<FVoxelLandChunkData, ESPMode::ThreadSafe> Chunk = MakeShared<FVoxelLandChunkData, ESPMode::ThreadSafe>();
	Chunk->Key = Key;
	Chunk->Density = MoveTemp(Current);
	return FinalizeDensityChunk(Chunk, Worker, OutChunk, IsCancelled, false);
}
