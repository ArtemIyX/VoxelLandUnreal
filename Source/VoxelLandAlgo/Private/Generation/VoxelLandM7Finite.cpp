#include "Generation/VoxelLandGenerator.h"

#include "Data/Structs/VoxelLandMeshData.h"
#include "Generation/VoxelLandM4HeightGrid.h"

class FVoxelLandM7FiniteFootprint final
{
public:
	static constexpr uint8 Level = 1;
	static constexpr int32 Stride = 1 << Level;
	static constexpr int32 Samples = FVoxelLandChunkData::SamplesPerAxis;
	static constexpr int32 BaseAxis = (Samples - 1) * Stride + 1;

	static FInt64Vector BaseMin(const FVoxelLandChunkKey& Key)
	{
		return (FInt64Vector(Key.Coordinate) * int64(FVoxelLandChunkData::Cells) - FInt64Vector(1)) * int64(Stride);
	}
};

FVoxelLandError FVoxelLandGenerator::ValidateM7FiniteDemand(const FVoxelLandChunkKey& Key) const
{
	using FFootprint = FVoxelLandM7FiniteFootprint;
	if (Key.LOD != FFootprint::Level || !M4HeightGrid || M4Unlimited || M5Lease.IsValid()
		|| !M6Stamps.IsEmpty() || !M6Filters.IsEmpty() || M6RedistanceBand > 0)
	{
		return {EVoxelLandErrorCode::UnsupportedLod, TEXT("LOD 1 requires ordinary finite M4 with M6 disabled")};
	}
	float LevelSpacingCm = 0.f;
	if (!VoxelLandMeshCoordinates::TryGetLevelSpacing(Profile.VoxelSizeCm, Key.LOD, LevelSpacingCm)
		|| !VoxelLandMeshCoordinates::IsKeyInPrecisionRange(Key.Coordinate, LevelSpacingCm, false))
	{
		return {EVoxelLandErrorCode::NonFiniteCoordinate, TEXT("LOD 1 exceeds the finite mesh precision range")};
	}

	// Finite M4 setup admits integral base spacing up to 2,000,000 cm.
	// The finite mesh key cap and this fixed level keep every lattice
	// index and its centimetre conversion in signed int64.
	const int64 VoxelSizeCm = int64(Profile.VoxelSizeCm);
	const int32 CompatibilityHalo = GetM6CompatibilityHalo();
	const int32 HeightMargin = CompatibilityHalo + int32(bM4NormalizeSlope);
	const FInt64Vector Margin(HeightMargin, HeightMargin, CompatibilityHalo);
	const FInt64Vector First = FFootprint::BaseMin(Key);
	const FInt64Vector Min = (First - Margin) * VoxelSizeCm;
	const FInt64Vector Max = (First + FInt64Vector(FFootprint::BaseAxis - 1) + Margin) * VoxelSizeCm;
	static_assert(FFootprint::BaseAxis == 69);

	constexpr int64 MaxBaselineZCm = 2000000;
	if (Min.Z < -MaxBaselineZCm || Max.Z > MaxBaselineZCm)
	{
		return {EVoxelLandErrorCode::NonFiniteCoordinate, TEXT("LOD 1 input including compatibility smoothing exceeds the M4 Z range")};
	}
	const bool bNative = !bM4FixedLatticeShaping
		&& ((M4Overhang.Displacement.bEnabled && M4Overhang.Displacement.AmplitudeQ8 != 0)
			|| (M4HeightDetail.bEnabled && M4HeightDetail.AmplitudeQ8 != 0));
	constexpr int64 MaxExactFloatCm = 16777216;
	if (bNative && (Min.X < -MaxExactFloatCm || Max.X > MaxExactFloatCm
		|| Min.Y < -MaxExactFloatCm || Max.Y > MaxExactFloatCm))
	{
		return {EVoxelLandErrorCode::NonFiniteCoordinate, TEXT("LOD 1 input exceeds the native exact XY lattice")};
	}
	int32 HeightQ8 = 0;
	if (!M4HeightGrid->Sample(Min.X, Min.Y, HeightQ8) || !M4HeightGrid->Sample(Max.X, Max.Y, HeightQ8))
	{
		return {EVoxelLandErrorCode::InvalidParameter, TEXT("LOD 1 input including smoothing and slope reads is outside the M4 height grid")};
	}
	return {};
}

FVoxelLandError FVoxelLandGenerator::GenerateM7FiniteChunk(const FVoxelLandChunkKey& Key, FVoxelLandWorkerContext& Worker,
	TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe>& OutChunk, TFunctionRef<bool()> IsCancelled) const
{
	using FFootprint = FVoxelLandM7FiniteFootprint;
	if (IsInGameThread())
	{
		return {EVoxelLandErrorCode::GameThreadNotAllowed, TEXT("LOD 1 generation requires a worker thread")};
	}
	if (!OwnsWorkerOnCurrentThread(Worker))
	{
		return {EVoxelLandErrorCode::InvalidContext, TEXT("LOD 1 worker belongs to a different generator or thread")};
	}
	if (const FVoxelLandError Error = ValidateM7FiniteDemand(Key))
	{
		return Error;
	}
	if (IsCancelled())
	{
		return {EVoxelLandErrorCode::Cancelled, TEXT("LOD 1 generation cancelled")};
	}

	TArray<int16> Baseline;
	if (const FVoxelLandError Error = GenerateM4TiledBaselineBox(
			FFootprint::BaseMin(Key), FFootprint::BaseAxis, Worker, Baseline, IsCancelled))
	{
		return Error;
	}
	const TSharedRef<FVoxelLandChunkData, ESPMode::ThreadSafe> Chunk = MakeShared<FVoxelLandChunkData, ESPMode::ThreadSafe>();
	Chunk->Key = Key;
	Chunk->Density.SetNumUninitialized(FVoxelLandChunkData::SampleCount);
	int32 Destination = 0;
	for (int32 Z = 0; Z < FFootprint::Samples; ++Z)
	{
		if (IsCancelled())
		{
			return {EVoxelLandErrorCode::Cancelled, TEXT("LOD 1 sample selection cancelled")};
		}
		for (int32 Y = 0; Y < FFootprint::Samples; ++Y)
		{
			const int32 Row = FFootprint::BaseAxis * FFootprint::Stride * (Y + FFootprint::BaseAxis * Z);
			for (int32 X = 0; X < FFootprint::Samples; ++X)
			{
				Chunk->Density[Destination++] = Baseline[Row + FFootprint::Stride * X];
			}
		}
	}
	return FinalizeDensityChunk(Chunk, Worker, OutChunk, IsCancelled, false);
}
