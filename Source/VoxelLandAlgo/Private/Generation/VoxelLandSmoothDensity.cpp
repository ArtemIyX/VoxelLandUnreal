#include "Generation/VoxelLandGenerator.h"
#include "Data/Structs/VoxelLandWorkerContextImpl.h"
#include "HAL/PlatformTLS.h"
#include <cfloat>

namespace
{
constexpr int32 ExpandedAxis = 37;
constexpr int32 ExpandedCount = ExpandedAxis * ExpandedAxis * ExpandedAxis;
constexpr int32 SmoothBatchSize = 256;

FVoxelLandError SmoothError(EVoxelLandErrorCode Code, const TCHAR* Message)
{
	FVoxelLandError Error;
	Error.Code = Code;
	Error.Message = Message;
	return Error;
}

int32 RoundRatioAwayFromZero(int64 Numerator, int32 Denominator)
{
	const int64 Magnitude = Numerator < 0 ? -Numerator : Numerator;
	const int32 Rounded = int32((Magnitude + Denominator / 2) / Denominator);
	return Numerator < 0 ? -Rounded : Rounded;
}

FVoxelLandError FillExpandedPositions(const FVoxelLandChunkKey& Key, float VoxelSize, int32 Start,
	TArrayView<FVector3f> Positions)
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
		if (FMath::Abs(PX) > double(FLT_MAX) || FMath::Abs(PY) > double(FLT_MAX)
			|| FMath::Abs(PZ) > double(FLT_MAX))
		{
			return SmoothError(EVoxelLandErrorCode::NonFiniteCoordinate, TEXT("Expanded chunk coordinate cannot be represented"));
		}
		Positions[Sample] = FVector3f(float(PX), float(PY), float(PZ));
		if (!FMath::IsFinite(Positions[Sample].X) || !FMath::IsFinite(Positions[Sample].Y)
			|| !FMath::IsFinite(Positions[Sample].Z))
		{
			return SmoothError(EVoxelLandErrorCode::NonFiniteCoordinate, TEXT("Expanded chunk coordinate cannot be represented"));
		}
	}
	return {};
}

int16 SmoothSample(const TArray<int16>& Expanded, int32 X, int32 Y, int32 Z, int32 Strength)
{
	const int32 Center = (X + 1) + ExpandedAxis * ((Y + 1) + ExpandedAxis * (Z + 1));
	int32 Sum = 0;
	for (int32 DZ = -1; DZ <= 1; ++DZ)
	{
		for (int32 DY = -1; DY <= 1; ++DY)
		{
			for (int32 DX = -1; DX <= 1; ++DX)
			{
				Sum += Expanded[Center + DX + ExpandedAxis * (DY + ExpandedAxis * DZ)];
			}
		}
	}
	const int32 Average = RoundRatioAwayFromZero(Sum, 27);
	const int32 Original = Expanded[Center];
	const int64 Blend = int64(Original) * (65535 - Strength) + int64(Average) * Strength;
	return int16(FMath::Clamp(RoundRatioAwayFromZero(Blend, 65535), -32768, 32767));
}
}

FVoxelLandError FVoxelLandGenerator::GenerateSmoothedChunkCancellable(const FVoxelLandChunkKey& Key,
	FVoxelLandWorkerContext& Worker, bool bEnabled, float Strength,
	TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe>& OutChunk, TFunctionRef<bool()> IsCancelled) const
{
	OutChunk.Reset();
	if (!FMath::IsFinite(Strength) || Strength < 0.f || Strength > 1.f)
	{
		return SmoothError(EVoxelLandErrorCode::InvalidParameter, TEXT("Smooth strength must be finite and in [0,1]"));
	}
	if (!bEnabled || Strength == 0.f)
	{
		return GenerateChunkCancellable(Key, Worker, OutChunk, IsCancelled);
	}
	if (IsInGameThread())
	{
		return SmoothError(EVoxelLandErrorCode::GameThreadNotAllowed, TEXT("Chunk generation requires a worker thread"));
	}
	if (!Worker.Impl || Worker.Impl->Owner.Get() != this || Worker.Impl->Thread != FPlatformTLS::GetCurrentThreadId())
	{
		return SmoothError(EVoxelLandErrorCode::InvalidContext, TEXT("Worker belongs to a different generator or thread"));
	}
	if (Key.LOD != 0)
	{
		return SmoothError(EVoxelLandErrorCode::UnsupportedLod, TEXT("Smoothing supports only LOD 0"));
	}
	TArray<int16>& Expanded = Worker.SmoothScratch;
	Expanded.Reset();
	Expanded.Reserve(ExpandedCount);
	FVector3f Positions[SmoothBatchSize];
	TArray<int16> Batch;
	for (int32 Start = 0; Start < ExpandedCount; Start += SmoothBatchSize)
	{
		if (IsCancelled())
		{
			return SmoothError(EVoxelLandErrorCode::Cancelled, TEXT("Chunk generation cancelled"));
		}
		const int32 Count = FMath::Min(SmoothBatchSize, ExpandedCount - Start);
		if (FVoxelLandError Error = FillExpandedPositions(Key, Profile.VoxelSizeCm, Start, MakeArrayView(Positions, Count)))
		{
			return Error;
		}
		if (FVoxelLandError Error = EvaluateSamples(MakeArrayView(Positions, Count), Worker, Batch))
		{
			return Error;
		}
		Expanded.Append(Batch);
	}
	const int32 FixedStrength = int32(double(Strength) * 65535.0 + 0.5);
	TSharedPtr<FVoxelLandChunkData, ESPMode::ThreadSafe> Chunk = MakeShared<FVoxelLandChunkData, ESPMode::ThreadSafe>();
	Chunk->Key = Key;
	Chunk->Density.Reserve(FVoxelLandChunkData::SampleCount);
	for (int32 Z = 0; Z < FVoxelLandChunkData::SamplesPerAxis; ++Z)
	{
		if (IsCancelled())
		{
			return SmoothError(EVoxelLandErrorCode::Cancelled, TEXT("Chunk generation cancelled"));
		}
		for (int32 Y = 0; Y < FVoxelLandChunkData::SamplesPerAxis; ++Y)
		{
			for (int32 X = 0; X < FVoxelLandChunkData::SamplesPerAxis; ++X)
			{
				Chunk->Density.Add(SmoothSample(Expanded, X, Y, Z, FixedStrength));
			}
		}
	}
	if (IsCancelled())
	{
		return SmoothError(EVoxelLandErrorCode::Cancelled, TEXT("Chunk generation cancelled"));
	}
	Chunk->Hash = Worker.Impl->HashChunkDensity(Chunk->Density);
	if (IsCancelled())
	{
		return SmoothError(EVoxelLandErrorCode::Cancelled, TEXT("Chunk generation cancelled"));
	}
	OutChunk = Chunk;
	return {};
}
