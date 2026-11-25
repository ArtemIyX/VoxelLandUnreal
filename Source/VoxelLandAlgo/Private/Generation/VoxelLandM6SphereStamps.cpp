#include "Generation/VoxelLandGenerator.h"

#include "Data/Structs/VoxelLandCanonicalWriter.h"
#include "Data/Structs/VoxelLandWorkerContextImpl.h"

class FVoxelLandM6SphereMath final
{
public:
	static constexpr int64 CoordinateLimitCm = 2000000;
	static constexpr int64 CentimetreScaleQ8 = 256;
	static constexpr int64 DensityUnitsPerVoxel = 1024;
	static constexpr int64 ToDensityScale = DensityUnitsPerVoxel / CentimetreScaleQ8;
	static constexpr uint32 IdentityTag = 0x5053364d; // M6SP in little endian.

	static constexpr bool FitsSphereAxis(int64 Center, int64 Radius)
	{
		return Center - Radius >= -CoordinateLimitCm && Center + Radius <= CoordinateLimitCm;
	}

	static FInt64Vector FirstSampleCm(const FVoxelLandChunkKey& Key, int64 VoxelSizeCm);
	static uint64 FloorSqrt(uint64 Squared);
	static int16 Evaluate(const FVoxelLandM6SphereStamp& Stamp, const FInt64Vector& PositionCm, int64 VoxelSizeCm);
};

FInt64Vector FVoxelLandM6SphereMath::FirstSampleCm(const FVoxelLandChunkKey& Key, int64 VoxelSizeCm)
{
	const FInt64Vector Coordinate(Key.Coordinate);
	return (Coordinate * int64(FVoxelLandChunkData::Cells) - FInt64Vector(1)) * VoxelSizeCm;
}

uint64 FVoxelLandM6SphereMath::FloorSqrt(uint64 Squared)
{
	// A floating estimate only selects the starting value. The exact integer
	// inequalities establish the result even at a square or a rounding boundary.
	uint64 Root = uint64(FMath::Sqrt(double(Squared)));
	while (Root * Root > Squared)
	{
		--Root;
	}
	while ((Root + 1) * (Root + 1) <= Squared)
	{
		++Root;
	}
	return Root;
}

int16 FVoxelLandM6SphereMath::Evaluate(
	const FVoxelLandM6SphereStamp& Stamp, const FInt64Vector& PositionCm, int64 VoxelSizeCm)
{
	// Both endpoints are admitted within the coordinate envelope. Prove the
	// worst-case scaled sum fits uint64 before squaring any offsets.
	constexpr uint64 MaxDeltaQ8 = 2 * CoordinateLimitCm * CentimetreScaleQ8;
	static_assert(MaxDeltaQ8 + 1 <= (MAX_uint64 / 3) / (MaxDeltaQ8 + 1));
	static_assert(FVoxelLandM6SphereSettings::MaxStamps * FVoxelLandChunkData::SampleCount == 686000);

	const FInt64Vector DeltaQ8 = (PositionCm - FInt64Vector(Stamp.CenterCm)) * CentimetreScaleQ8;
	const uint64 Squared = uint64(DeltaQ8.X * DeltaQ8.X)
		+ uint64(DeltaQ8.Y * DeltaQ8.Y) + uint64(DeltaQ8.Z * DeltaQ8.Z);
	int64 Numerator = (int64(FloorSqrt(Squared)) - int64(Stamp.RadiusCm) * CentimetreScaleQ8)
		* ToDensityScale;
	if (Stamp.Operation == EVoxelLandM6SphereOperation::Subtract)
	{
		Numerator = -Numerator;
	}
	const int64 Magnitude = FMath::Abs(Numerator);
	const int64 Rounded = (Magnitude + VoxelSizeCm / 2) / VoxelSizeCm;
	return int16(FMath::Clamp<int64>(Numerator < 0 ? -Rounded : Rounded, MIN_int16, MAX_int16));
}

FVoxelLandError FVoxelLandGenerator::InitializeM6Spheres(const FVoxelLandM6SphereSettings& Settings)
{
	if (Settings.Version != FVoxelLandM6SphereSettings::CurrentVersion
		|| Settings.Stamps.Num() > FVoxelLandM6SphereSettings::MaxStamps)
	{
		return {EVoxelLandErrorCode::InvalidParameter, TEXT("Unsupported M6 sphere version or stamp count")};
	}

	const TConstArrayView<FVoxelLandM6SphereStamp> Stamps = Settings.Stamps;
	for (int32 Index = 0; Index < Stamps.Num(); ++Index)
	{
		const FVoxelLandM6SphereStamp& Stamp = Stamps[Index];
		if (!Stamp.Id.IsValid()
			|| (Stamp.Operation != EVoxelLandM6SphereOperation::Add
				&& Stamp.Operation != EVoxelLandM6SphereOperation::Subtract)
			|| Stamp.RadiusCm <= 0
			|| !FVoxelLandM6SphereMath::FitsSphereAxis(Stamp.CenterCm.X, Stamp.RadiusCm)
			|| !FVoxelLandM6SphereMath::FitsSphereAxis(Stamp.CenterCm.Y, Stamp.RadiusCm)
			|| !FVoxelLandM6SphereMath::FitsSphereAxis(Stamp.CenterCm.Z, Stamp.RadiusCm))
		{
			return {EVoxelLandErrorCode::InvalidParameter, TEXT("Invalid M6 sphere stamp ID, operation, radius or bounds")};
		}
		for (int32 Prior = 0; Prior < Index; ++Prior)
		{
			if (Stamps[Prior].Id == Stamp.Id)
			{
				return {EVoxelLandErrorCode::InvalidParameter, TEXT("Duplicate M6 sphere stamp ID")};
			}
		}
	}

	FCanonicalWriter Writer;
	constexpr int32 StampRecordBytes = sizeof(FGuid) + sizeof(uint8) + 4 * sizeof(uint32);
	Writer.Bytes.Reserve(3 * sizeof(uint32) + Stamps.Num() * StampRecordBytes);
	Writer.U32(FVoxelLandM6SphereMath::IdentityTag);
	Writer.U32(Settings.Version);
	Writer.U32(uint32(Stamps.Num()));
	for (const FVoxelLandM6SphereStamp& Stamp : Stamps)
	{
		Writer.U32(Stamp.Id.A);
		Writer.U32(Stamp.Id.B);
		Writer.U32(Stamp.Id.C);
		Writer.U32(Stamp.Id.D);
		Writer.U8(uint8(Stamp.Operation));
		Writer.U32(uint32(Stamp.CenterCm.X));
		Writer.U32(uint32(Stamp.CenterCm.Y));
		Writer.U32(uint32(Stamp.CenterCm.Z));
		Writer.U32(uint32(Stamp.RadiusCm));
	}
	M6SphereIdentity = MoveTemp(Writer.Bytes);
	M6Spheres.Append(Stamps.GetData(), Stamps.Num());
	return {};
}

FVoxelLandError FVoxelLandGenerator::ValidateM6SphereFootprint(const FVoxelLandChunkKey& Key) const
{
	constexpr int64 Limit = FVoxelLandM6SphereMath::CoordinateLimitCm;
	const int64 VoxelSizeCm = int64(Profile.VoxelSizeCm);
	const FInt64Vector First = FVoxelLandM6SphereMath::FirstSampleCm(Key, VoxelSizeCm);
	const FInt64Vector Last = First + FInt64Vector((FVoxelLandChunkData::SamplesPerAxis - 1) * VoxelSizeCm);
	if (First.X < -Limit || First.Y < -Limit || First.Z < -Limit
		|| Last.X > Limit || Last.Y > Limit || Last.Z > Limit)
	{
		return {EVoxelLandErrorCode::NonFiniteCoordinate, TEXT("M6 sphere sample footprint exceeds the coordinate range")};
	}
	return {};
}

FVoxelLandError FVoxelLandGenerator::ApplyM6Spheres(FVoxelLandChunkData& Chunk, TFunctionRef<bool()> IsCancelled) const
{
	check(Chunk.Density.Num() == FVoxelLandChunkData::SampleCount);
	constexpr int32 Axis = FVoxelLandChunkData::SamplesPerAxis;
	const int64 VoxelSizeCm = int64(Profile.VoxelSizeCm);
	const FInt64Vector First = FVoxelLandM6SphereMath::FirstSampleCm(Chunk.Key, VoxelSizeCm);
	int32 Index = 0;
	for (int32 Z = 0; Z < Axis; ++Z)
	{
		for (int32 Y = 0; Y < Axis; ++Y)
		{
			if (IsCancelled())
			{
				return {EVoxelLandErrorCode::Cancelled, TEXT("M6 sphere stamp evaluation cancelled")};
			}
			for (int32 X = 0; X < Axis; ++X)
			{
				const FInt64Vector PositionCm = First + FInt64Vector(X, Y, Z) * VoxelSizeCm;
				int16& Density = Chunk.Density[Index++];
				for (const FVoxelLandM6SphereStamp& Stamp : M6Spheres)
				{
					const int16 Shape = FVoxelLandM6SphereMath::Evaluate(Stamp, PositionCm, VoxelSizeCm);
					Density = Stamp.Operation == EVoxelLandM6SphereOperation::Add
						? FMath::Min(Density, Shape) : FMath::Max(Density, Shape);
				}
			}
		}
	}
	return {};
}

FVoxelLandError FVoxelLandGenerator::FinalizeDensityChunk(TSharedRef<FVoxelLandChunkData, ESPMode::ThreadSafe> Chunk,
	FVoxelLandWorkerContext& Worker, TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe>& OutChunk,
	TFunctionRef<bool()> IsCancelled) const
{
	if (!M6Spheres.IsEmpty())
	{
		if (const FVoxelLandError Error = ApplyM6Spheres(*Chunk, IsCancelled))
		{
			return Error;
		}
	}
	if (IsCancelled())
	{
		return {EVoxelLandErrorCode::Cancelled, TEXT("Chunk generation cancelled")};
	}
	Chunk->Hash = Worker.Impl->HashChunkDensity(Chunk->Density);
	if (IsCancelled())
	{
		return {EVoxelLandErrorCode::Cancelled, TEXT("Chunk generation cancelled")};
	}
	OutChunk = MoveTemp(Chunk);
	return {};
}
