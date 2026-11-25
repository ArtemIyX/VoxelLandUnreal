#include "Generation/Field/VoxelLandGenerator.h"
#include "Generation/Terrain/Heightfield/VoxelLandHeightGrid.h"
#include "Generation/Caves/Infrastructure/VoxelLandCaveBudget.h"
#include "Generation/Caves/Infrastructure/VoxelLandCaveBudgetLease.h"
#include "Generation/Caves/Infrastructure/VoxelLandCaveCheckedMath.h"
#include "VoxelLandQuantizedDensity.h"
#include "Data/Caves/Geometry/VoxelLandCavePointCm.h"
#include "Data/Field/Execution/VoxelLandWorkerContextImpl.h"
#include "Operators/VoxelLandOperator.h"
#include "HAL/PlatformTLS.h"
#include <cfloat>

class FVoxelLandSmoothDensityInternals final
{
public:
	static constexpr int32 ExpandedAxis = 37;
	static constexpr int32 ExpandedCount = ExpandedAxis * ExpandedAxis * ExpandedAxis;
	static constexpr int32 SmoothBatchSize = 256;
	static FVoxelLandError SmoothError(EVoxelLandErrorCode Code, const TCHAR* Message);
	static FVoxelLandError FillExpandedPositions(
		const FVoxelLandChunkKey& Key, float VoxelSize, int32 Start, TArrayView<FVector3f> Positions);
};

FVoxelLandError FVoxelLandSmoothDensityInternals::SmoothError(EVoxelLandErrorCode Code, const TCHAR* Message)
{
	FVoxelLandError Error;
	Error.Code = Code;
	Error.Message = Message;
	return Error;
}

int32 FVoxelLandQuantizedDensity::RoundRatioAwayFromZero(int64 Numerator, int32 Denominator)
{
	const int64 Magnitude = Numerator < 0 ? -Numerator : Numerator;
	const int32 Rounded = int32((Magnitude + Denominator / 2) / Denominator);
	return Numerator < 0 ? -Rounded : Rounded;
}

FVoxelLandError FVoxelLandSmoothDensityInternals::FillExpandedPositions(
	const FVoxelLandChunkKey& Key, float VoxelSize, int32 Start, TArrayView<FVector3f> Positions)
{
	for (int32 Sample = 0; Sample < Positions.Num(); ++Sample)
	{
		const int32 Index = Start + Sample;
		const int32 X = Index % ExpandedAxis;
		const int32 Y = (Index / ExpandedAxis) % ExpandedAxis;
		const int32 Z = Index / (ExpandedAxis * ExpandedAxis);
		const double PX = double(int64(Key.Coordinate.X) * 32 + X - 2) * double(VoxelSize);
		const double PY = double(int64(Key.Coordinate.Y) * 32 + Y - 2) * double(VoxelSize);
		const double PZ = double(int64(Key.Coordinate.Z) * 32 + Z - 2) * double(VoxelSize);
		if (FMath::Abs(PX) > double(FLT_MAX) || FMath::Abs(PY) > double(FLT_MAX) || FMath::Abs(PZ) > double(FLT_MAX))
		{
			return SmoothError(
				EVoxelLandErrorCode::NonFiniteCoordinate, TEXT("Expanded chunk coordinate cannot be represented"));
		}
		Positions[Sample] = FVector3f(float(PX), float(PY), float(PZ));
		if (!FMath::IsFinite(Positions[Sample].X) || !FMath::IsFinite(Positions[Sample].Y)
			|| !FMath::IsFinite(Positions[Sample].Z))
		{
			return SmoothError(
				EVoxelLandErrorCode::NonFiniteCoordinate, TEXT("Expanded chunk coordinate cannot be represented"));
		}
	}
	return {};
}

int16 FVoxelLandQuantizedDensity::SmoothSample(
	TConstArrayView<int16> Expanded, const FIntVector& ExpandedSize, int32 X, int32 Y, int32 Z, int32 Strength)
{
	const int32 Center = (X + 1) + ExpandedSize.X * ((Y + 1) + ExpandedSize.Y * (Z + 1));
	int32 Sum = 0;
	for (int32 DZ = -1; DZ <= 1; ++DZ)
	{
		for (int32 DY = -1; DY <= 1; ++DY)
		{
			for (int32 DX = -1; DX <= 1; ++DX)
			{
				Sum += Expanded[Center + DX + ExpandedSize.X * (DY + ExpandedSize.Y * DZ)];
			}
		}
	}
	return Blend(Expanded[Center], RoundRatioAwayFromZero(Sum, 27), Strength);
}

int16 FVoxelLandQuantizedDensity::Blend(int16 Original, int32 Target, int32 Strength)
{
	const int64 Weighted = int64(Original) * (MAX_uint16 - Strength) + int64(Target) * Strength;
	return int16(FMath::Clamp(RoundRatioAwayFromZero(Weighted, MAX_uint16), int32(MIN_int16), int32(MAX_int16)));
}

FVoxelLandM5Diagnostic FVoxelLandGenerator::GenerateFiniteM4Box(const FVoxelLandM5PointCm& MinLattice,
	const FIntVector& Size, FVoxelLandWorkerContext& Worker, TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget,
	TFunctionRef<bool()> IsCancelled, TArray<int16>& OutDensity) const
{
	using E = EVoxelLandM5ErrorCode;
	using namespace VoxelLand::M5::CheckedMath;
	const auto Fail = [](E Code, const TCHAR* Message) {
		FVoxelLandM5Diagnostic Result;
		Result.Code = Code;
		Result.Stage = TEXT("M5.M4Box");
		Result.Message = Message;
		return Result;
	};
	if (IsCancelled())
	{
		return Fail(E::Cancelled, TEXT("M4 box cancelled"));
	}
	if (!M4HeightGrid || M4Unlimited || Program->GetData().ExternalFilterIndex != INDEX_NONE
		|| !OwnsWorkerOnCurrentThread(Worker) || Size.X < 1 || Size.Y < 1 || Size.Z < 1 || Size.X > 4096
		|| Size.Y > 4096 || Size.Z > 4096 || !FMath::IsFinite(Profile.VoxelSizeCm) || Profile.VoxelSizeCm < 1.f
		|| Profile.VoxelSizeCm > 65536.f || Profile.VoxelSizeCm != float(int32(Profile.VoxelSizeCm)))
	{
		return Fail(E::InvalidInput, TEXT("Invalid finite M4 box, generator or worker"));
	}
	const bool bSmooth = bSmoothEnabled && SmoothStrength > 0.f;
	const int64 Halo = bSmooth ? 1 : 0, Slope = bM4NormalizeSlope ? 1 : 0;
	const int64 Spacing = int64(Profile.VoxelSizeCm);
	const int64 Min[] = {MinLattice.X, MinLattice.Y, MinLattice.Z};
	int64 Low[3] = {}, High[3] = {}, Count = 1;
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		int64 Last = 0, LowIndex = 0, HighIndex = 0;
		const int64 Extra = Halo + (Axis < 2 ? Slope : 0);
		if (!TryMultiplyNonNegative(Count, Size[Axis], Count) || Count > MAX_int32
			|| !TryAdd(Min[Axis], int64(Size[Axis]) - 1, Last) || !TrySubtract(Min[Axis], Extra, LowIndex)
			|| !TryAdd(Last, Extra, HighIndex) || !TryMultiply(LowIndex, Spacing, Low[Axis])
			|| !TryMultiply(HighIndex, Spacing, High[Axis]))
		{
			return Fail(E::OutOfDomain, TEXT("M4 box footprint overflow"));
		}
	}
	if (Low[2] < -2000000 || High[2] > 2000000)
	{
		return Fail(E::OutOfDomain, TEXT("M4 box Z outside fixed range"));
	}
	const bool bNative = !bM4FixedLatticeShaping
					  && ((M4Overhang.Displacement.bEnabled && M4Overhang.Displacement.AmplitudeQ8 != 0)
						  || (M4HeightDetail.bEnabled && M4HeightDetail.AmplitudeQ8 != 0));
	if (bNative && (Low[0] < -16777216 || High[0] > 16777216 || Low[1] < -16777216 || High[1] > 16777216))
	{
		return Fail(E::OutOfDomain, TEXT("M4 box native coordinates outside exact float lattice"));
	}
	int32 Sample = 0;
	if (!M4HeightGrid->Sample(Low[0], Low[1], Sample) || !M4HeightGrid->Sample(High[0], High[1], Sample))
	{
		return Fail(E::OutOfDomain, TEXT("M4 box outside height grid"));
	}
	int64 Work = 0, OutputBytes = 0;
	// The sum of the raw tile volumes is the product of the per-axis
	// extents, including each tile's own smoothing halo. This counts overlap
	// again where adjacent tiles recompute it. The kernel's 8-unit raw-sample
	// bound is unchanged. Detailed-height slope stencils additionally evaluate
	// a two-dimensional expanded grid once per Z tile, including thin tiles.
	int64 RawSamples = 1, AxisExtent[3] = {}, TileCount[3] = {};
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		TileCount[Axis] = (int64(Size[Axis]) + 34) / 35;
		int64 TileHalo = 0;
		if (!TryMultiplyNonNegative(TileCount[Axis], 2 * Halo, TileHalo)
			|| !TryAdd(Size[Axis], TileHalo, AxisExtent[Axis])
			|| !TryMultiplyNonNegative(RawSamples, AxisExtent[Axis], RawSamples))
		{
			return Fail(E::ArithmeticOverflow, TEXT("M4 box tile count overflow"));
		}
	}
	int64 DetailWork = 0, DetailX = 0, DetailY = 0, ExpandedSlopeX = 0, ExpandedSlopeY = 0;
	if (M4HeightDetail.bEnabled && M4HeightDetail.AmplitudeQ8 != 0
		&& (!TryMultiplyNonNegative(TileCount[0], 2 * Slope, ExpandedSlopeX)
			|| !TryMultiplyNonNegative(TileCount[1], 2 * Slope, ExpandedSlopeY)
			|| !TryAdd(AxisExtent[0], ExpandedSlopeX, DetailX) || !TryAdd(AxisExtent[1], ExpandedSlopeY, DetailY)
			|| !TryMultiplyNonNegative(DetailX, DetailY, DetailWork)
			|| !TryMultiplyNonNegative(DetailWork, TileCount[2], DetailWork)
			|| !TryMultiplyNonNegative(DetailWork, 8, DetailWork)))
	{
		return Fail(E::ArithmeticOverflow, TEXT("M4 box detail work overflow"));
	}
	if (!TryMultiplyNonNegative(RawSamples, 8, Work) || !TryAdd(Work, DetailWork, Work)
		|| !TryAdd(Work, Count * (bSmooth ? 30 : 2), Work)
		|| !TryMultiplyNonNegative(Count, 2 * int64(sizeof(int16)), OutputBytes)
		|| !TryAdd(OutputBytes, 4 * 1024 * 1024, OutputBytes))
	{
		return Fail(E::ArithmeticOverflow, TEXT("M4 box admission overflow"));
	}
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Lease;
	if (!Budget->TryChargeWork(Work) || !Budget->TryReserveBytes(OutputBytes, Lease))
	{
		return Fail(E::BudgetExceeded, TEXT("M4 box admission exhausted"));
	}
	TArray<int16> Result;
	Result.SetNumUninitialized(int32(Count));
	const int32 Strength = int32(double(SmoothStrength) * 65535.0 + 0.5);
	for (int32 Z = 0; Z < Size.Z; Z += 35)
	{
		for (int32 Y = 0; Y < Size.Y; Y += 35)
		{
			for (int32 X = 0; X < Size.X; X += 35)
			{
				if (IsCancelled())
				{
					return Fail(E::Cancelled, TEXT("M4 box cancelled"));
				}
				const FIntVector Tile(
					FMath::Min(35, Size.X - X), FMath::Min(35, Size.Y - Y), FMath::Min(35, Size.Z - Z));
				const FIntVector RawSize(Tile.X + int32(2 * Halo), Tile.Y + int32(2 * Halo), Tile.Z + int32(2 * Halo));
				FVoxelLandM5PointCm RawMin;
				RawMin.X = MinLattice.X + X - Halo;
				RawMin.Y = MinLattice.Y + Y - Halo;
				RawMin.Z = MinLattice.Z + Z - Halo;
				TArray<int16> Raw;
				const auto Error = GenerateM4DensityBox(RawMin, RawSize, Worker, Raw, IsCancelled);
				if (Error)
				{
					FVoxelLandM5Diagnostic Diagnostic =
						Fail(Error.Code == EVoxelLandErrorCode::Cancelled ? E::Cancelled : E::Unproven,
							TEXT("M4 box sampling failed"));
					Diagnostic.Message = Error.Message;
					return Diagnostic;
				}
				for (int32 K = 0; K < Tile.Z; ++K)
				{
					if (IsCancelled())
					{
						return Fail(E::Cancelled, TEXT("M4 box cancelled"));
					}
					for (int32 J = 0; J < Tile.Y; ++J)
					{
						for (int32 I = 0; I < Tile.X; ++I)
						{
							Result[(X + I) + Size.X * ((Y + J) + Size.Y * (Z + K))] =
								bSmooth ? FVoxelLandQuantizedDensity::SmoothSample(Raw, RawSize, I, J, K, Strength)
										: Raw[I + RawSize.X * (J + RawSize.Y * K)];
						}
					}
				}
			}
		}
	}
	if (IsCancelled())
	{
		return Fail(E::Cancelled, TEXT("M4 box cancelled"));
	}
	OutDensity = MoveTemp(Result);
	return {};
}

FVoxelLandError FVoxelLandGenerator::GenerateSmoothedChunkCancellable(const FVoxelLandChunkKey& Key,
	FVoxelLandWorkerContext& Worker, bool bEnabled, float Strength,
	TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe>& OutChunk, TFunctionRef<bool()> IsCancelled) const
{
	if (M4Unlimited)
	{
		return FVoxelLandSmoothDensityInternals::SmoothError(EVoxelLandErrorCode::InvalidContext,
			TEXT("M4 local terrain uses its configured filter through GenerateChunkCancellable"));
	}
	if (!M4HeightGrid)
	{
		OutChunk.Reset();
	}
	if (!FMath::IsFinite(Strength) || Strength < 0.f || Strength > 1.f)
	{
		return FVoxelLandSmoothDensityInternals::SmoothError(
			EVoxelLandErrorCode::InvalidParameter, TEXT("Smooth strength must be finite and in [0,1]"));
	}
	if (!bEnabled || Strength == 0.f)
	{
		return GeneratePureChunkCancellable(Key, Worker, OutChunk, IsCancelled);
	}
	if (IsInGameThread())
	{
		return FVoxelLandSmoothDensityInternals::SmoothError(
			EVoxelLandErrorCode::GameThreadNotAllowed, TEXT("Chunk generation requires a worker thread"));
	}
	if (!Worker.Impl || Worker.Impl->Owner.Get() != this || Worker.Impl->Thread != FPlatformTLS::GetCurrentThreadId())
	{
		return FVoxelLandSmoothDensityInternals::SmoothError(
			EVoxelLandErrorCode::InvalidContext, TEXT("Worker belongs to a different generator or thread"));
	}
	if (Key.LOD != 0)
	{
		return FVoxelLandSmoothDensityInternals::SmoothError(
			EVoxelLandErrorCode::UnsupportedLod, TEXT("Smoothing supports only LOD 0"));
	}
	if (FVoxelLandError Error = GenerateExpandedDensity(Key, Worker, IsCancelled))
	{
		return Error;
	}
	const TArray<int16>& Expanded = Worker.SmoothScratch;
	const int32 FixedStrength = int32(double(Strength) * 65535.0 + 0.5);
	const FIntVector ExpandedSize(FVoxelLandSmoothDensityInternals::ExpandedAxis,
		FVoxelLandSmoothDensityInternals::ExpandedAxis, FVoxelLandSmoothDensityInternals::ExpandedAxis);
	TSharedPtr<FVoxelLandChunkData, ESPMode::ThreadSafe> Chunk = MakeShared<FVoxelLandChunkData, ESPMode::ThreadSafe>();
	Chunk->Key = Key;
	Chunk->Density.Reserve(FVoxelLandChunkData::SampleCount);
	for (int32 Z = 0; Z < FVoxelLandChunkData::SamplesPerAxis; ++Z)
	{
		if (IsCancelled())
		{
			return FVoxelLandSmoothDensityInternals::SmoothError(
				EVoxelLandErrorCode::Cancelled, TEXT("Chunk generation cancelled"));
		}
		for (int32 Y = 0; Y < FVoxelLandChunkData::SamplesPerAxis; ++Y)
		{
			for (int32 X = 0; X < FVoxelLandChunkData::SamplesPerAxis; ++X)
			{
				Chunk->Density.Add(
					FVoxelLandQuantizedDensity::SmoothSample(Expanded, ExpandedSize, X, Y, Z, FixedStrength));
			}
		}
	}
	return FinalizeDensityChunk(Chunk.ToSharedRef(), Worker, OutChunk, IsCancelled);
}

FVoxelLandError FVoxelLandGenerator::GenerateExternalFilteredChunkCancellable(const FVoxelLandChunkKey& Key,
	FVoxelLandWorkerContext& Worker, TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe>& OutChunk,
	TFunctionRef<bool()> IsCancelled) const
{
	OutChunk.Reset();
	if (IsInGameThread())
	{
		return FVoxelLandSmoothDensityInternals::SmoothError(
			EVoxelLandErrorCode::GameThreadNotAllowed, TEXT("Filter generation requires a worker thread"));
	}
	if (!Worker.Impl || Worker.Impl->Owner.Get() != this || Worker.Impl->Thread != FPlatformTLS::GetCurrentThreadId())
	{
		return FVoxelLandSmoothDensityInternals::SmoothError(
			EVoxelLandErrorCode::InvalidContext, TEXT("Worker belongs to a different generator or thread"));
	}
	if (Key.LOD != 0)
	{
		return FVoxelLandSmoothDensityInternals::SmoothError(
			EVoxelLandErrorCode::UnsupportedLod, TEXT("Filter supports only LOD 0"));
	}
	const int32 OperatorIndex = Program->GetData().ExternalFilterIndex;
	if (!Worker.Impl->OperatorWorkers.IsValidIndex(OperatorIndex) || !Worker.Impl->OperatorWorkers[OperatorIndex])
	{
		return FVoxelLandSmoothDensityInternals::SmoothError(
			EVoxelLandErrorCode::InvalidProgram, TEXT("Filter worker unavailable"));
	}
	if (FVoxelLandError Error = GenerateExpandedDensity(Key, Worker, IsCancelled))
	{
		return Error;
	}
	const TArray<int16>& Expanded = Worker.SmoothScratch;
	TSharedPtr<FVoxelLandChunkData, ESPMode::ThreadSafe> Chunk = MakeShared<FVoxelLandChunkData, ESPMode::ThreadSafe>();
	Chunk->Key = Key;
	Chunk->Density.SetNumUninitialized(FVoxelLandChunkData::SampleCount);
	for (int32 Start = 0; Start < FVoxelLandChunkData::SampleCount;
		Start += FVoxelLandSmoothDensityInternals::SmoothBatchSize)
	{
		if (IsCancelled())
		{
			return FVoxelLandSmoothDensityInternals::SmoothError(
				EVoxelLandErrorCode::Cancelled, TEXT("Filter generation cancelled"));
		}
		const int32 Count =
			FMath::Min(FVoxelLandSmoothDensityInternals::SmoothBatchSize, FVoxelLandChunkData::SampleCount - Start);
		FString ProviderError;
		FVoxelLandOperatorOutput Writer(MakeArrayView(Chunk->Density.GetData() + Start, Count));
		if (!Worker.Impl->OperatorWorkers[OperatorIndex]->EvaluateFilter(
				Expanded, Start, Writer, IsCancelled, ProviderError)
			|| !Writer.IsComplete(ProviderError))
		{
			if (IsCancelled())
			{
				return FVoxelLandSmoothDensityInternals::SmoothError(
					EVoxelLandErrorCode::Cancelled, TEXT("Filter generation cancelled"));
			}
			FVoxelLandError Error = FVoxelLandSmoothDensityInternals::SmoothError(
				EVoxelLandErrorCode::InvalidProgram, TEXT("External filter failed"));
			Error.Parameter = ProviderError;
			return Error;
		}
	}
	if (IsCancelled())
	{
		return FVoxelLandSmoothDensityInternals::SmoothError(
			EVoxelLandErrorCode::Cancelled, TEXT("Filter generation cancelled"));
	}
	Chunk->Hash = Worker.Impl->HashChunkDensity(Chunk->Density);
	if (IsCancelled())
	{
		return FVoxelLandSmoothDensityInternals::SmoothError(
			EVoxelLandErrorCode::Cancelled, TEXT("Filter generation cancelled"));
	}
	OutChunk = MoveTemp(Chunk);
	return {};
}

FVoxelLandError FVoxelLandGenerator::GenerateExpandedDensity(
	const FVoxelLandChunkKey& Key, FVoxelLandWorkerContext& Worker, TFunctionRef<bool()> IsCancelled) const
{
	if (M4HeightGrid)
	{
		return GenerateM4Density(
			Key, FVoxelLandSmoothDensityInternals::ExpandedAxis, -2, Worker, Worker.SmoothScratch, IsCancelled);
	}
	TArray<int16>& Expanded = Worker.SmoothScratch;
	Expanded.Reset();
	Expanded.Reserve(FVoxelLandSmoothDensityInternals::ExpandedCount);
	FVector3f Positions[FVoxelLandSmoothDensityInternals::SmoothBatchSize];
	TArray<int16> Batch;
	for (int32 Start = 0; Start < FVoxelLandSmoothDensityInternals::ExpandedCount;
		Start += FVoxelLandSmoothDensityInternals::SmoothBatchSize)
	{
		if (IsCancelled())
		{
			return FVoxelLandSmoothDensityInternals::SmoothError(
				EVoxelLandErrorCode::Cancelled, TEXT("Filter generation cancelled"));
		}
		const int32 Count = FMath::Min(
			FVoxelLandSmoothDensityInternals::SmoothBatchSize, FVoxelLandSmoothDensityInternals::ExpandedCount - Start);
		if (FVoxelLandError Error = FVoxelLandSmoothDensityInternals::FillExpandedPositions(
				Key, Profile.VoxelSizeCm, Start, MakeArrayView(Positions, Count)))
		{
			return Error;
		}
		if (FVoxelLandError Error = EvaluateSamples(MakeArrayView(Positions, Count), Worker, Batch))
		{
			return Error;
		}
		Expanded.Append(Batch);
	}
	return {};
}