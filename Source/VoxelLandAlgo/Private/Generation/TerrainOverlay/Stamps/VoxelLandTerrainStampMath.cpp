#include "VoxelLandTerrainStampMath.h"

#include "Data/Field/Chunks/VoxelLandChunkData.h"

FInt64Vector FVoxelLandM6StampMath::FirstSampleCm(const FVoxelLandChunkKey& Key, int64 VoxelSizeCm)
{
	// The signed int32 chunk key, chunk stride and admitted voxel size
	// cannot overflow when multiplied to produce int64 centimetres.
	const FInt64Vector Coordinate(Key.Coordinate);
	return (Coordinate * int64(FVoxelLandChunkData::Cells) - FInt64Vector(1)) * VoxelSizeCm;
}

bool FVoxelLandM6StampMath::PositionInRange(const FInt64Vector& Point, int64 Limit)
{
	return Point.X >= -Limit && Point.X <= Limit && Point.Y >= -Limit && Point.Y <= Limit && Point.Z >= -Limit
		&& Point.Z <= Limit;
}

bool FVoxelLandM6StampMath::DimensionInRange(int64 Size)
{
	return Size > 0 && Size <= MaxStampDimensionCm;
}

void FVoxelLandM6StampMath::GetBounds(const FVoxelLandM6Stamp& Stamp, FInt64Vector& Min, FInt64Vector& Max)
{
	const bool bCapsule = Stamp.Shape == EVoxelLandM6StampShape::Capsule;
	const FInt64Vector Extent =
		Stamp.Shape == EVoxelLandM6StampShape::Box ? FInt64Vector(Stamp.HalfExtentCm) : FInt64Vector(Stamp.RadiusCm);
	Min = (bCapsule ? Stamp.OriginCm.ComponentMin(Stamp.SegmentEndCm) : Stamp.OriginCm) - Extent;
	Max = (bCapsule ? Stamp.OriginCm.ComponentMax(Stamp.SegmentEndCm) : Stamp.OriginCm) + Extent;
}

FVoxelLandError FVoxelLandM6StampMath::Validate(const FVoxelLandM6Stamp& Stamp, int64 CoordinateLimit)
{
	using EOperation = EVoxelLandM6StampOperation;
	using EShape = EVoxelLandM6StampShape;
	if (Stamp.BlendRadiusCm != 0)
	{
		return {EVoxelLandErrorCode::InvalidParameter, TEXT("M6 supports hard stamps only")};
	}
	if (!Stamp.Id.IsValid() || (Stamp.Operation != EOperation::Add && Stamp.Operation != EOperation::Subtract)
		|| !PositionInRange(Stamp.OriginCm, CoordinateLimit))
	{
		return {EVoxelLandErrorCode::InvalidParameter, TEXT("Invalid M6 stamp ID, operation or origin")};
	}
	switch (Stamp.Shape)
	{
		case EShape::Sphere:
			if (!DimensionInRange(Stamp.RadiusCm))
			{
				return {EVoxelLandErrorCode::InvalidParameter, TEXT("Invalid M6 sphere radius")};
			}
			break;
		case EShape::Box:
			if (!DimensionInRange(Stamp.HalfExtentCm.X) || !DimensionInRange(Stamp.HalfExtentCm.Y)
				|| !DimensionInRange(Stamp.HalfExtentCm.Z))
			{
				return {EVoxelLandErrorCode::InvalidParameter, TEXT("Invalid M6 box half extent")};
			}
			break;
		case EShape::Capsule:
			if (!DimensionInRange(Stamp.RadiusCm) || !PositionInRange(Stamp.SegmentEndCm, CoordinateLimit)
				|| Stamp.OriginCm == Stamp.SegmentEndCm
				|| !PositionInRange(Stamp.SegmentEndCm - Stamp.OriginCm, MaxCapsuleSpanCm))
			{
				return {EVoxelLandErrorCode::InvalidParameter, TEXT("Invalid M6 capsule radius, endpoint or span")};
			}
			break;
		default:
			return {EVoxelLandErrorCode::InvalidParameter, TEXT("Unsupported M6 stamp shape")};
	}

	FInt64Vector Min, Max;
	GetBounds(Stamp, Min, Max);
	if (!PositionInRange(Min, CoordinateLimit) || !PositionInRange(Max, CoordinateLimit))
	{
		return {EVoxelLandErrorCode::InvalidParameter, TEXT("M6 stamp bounds exceed the coordinate range")};
	}
	return {};
}

uint64 FVoxelLandM6StampMath::FloorSqrt(uint64 Squared)
{
	// The floating estimate is corrected against exact integer inequalities.
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

int64 FVoxelLandM6StampMath::RoundRatio(int64 Numerator, int64 Denominator)
{
	const int64 Rounded = (FMath::Abs(Numerator) + Denominator / 2) / Denominator;
	return Numerator < 0 ? -Rounded : Rounded;
}

int64 FVoxelLandM6StampMath::LengthQ8(const FInt64Vector& DeltaQ8)
{
	// A position that survived the saturation guard is within the segment span
	// plus one radius and the guard distance on every axis. The Q8 projection
	// is between the endpoints, so the same bound holds for its residual.
	constexpr uint64 MaxDeltaQ8 =
		(MaxCapsuleSpanCm + MaxStampDimensionCm + SaturationRadiusVoxels * MaxVoxelSizeCm) * CentimetreScaleQ8;
	static_assert(MaxDeltaQ8 <= uint64(MAX_int64) / MaxDeltaQ8);
	static_assert(MaxDeltaQ8 + 1 <= (MAX_uint64 / 3) / (MaxDeltaQ8 + 1));
	const uint64 Sum = uint64(DeltaQ8.X * DeltaQ8.X) + uint64(DeltaQ8.Y * DeltaQ8.Y) + uint64(DeltaQ8.Z * DeltaQ8.Z);
	return int64(FloorSqrt(Sum));
}

int64 FVoxelLandM6StampMath::CapsuleParameterQ32(const FInt64Vector& Delta, const FInt64Vector& Segment)
{
	const int64 Dot = Delta.X * Segment.X + Delta.Y * Segment.Y + Delta.Z * Segment.Z;
	const int64 LengthSquared = Segment.X * Segment.X + Segment.Y * Segment.Y + Segment.Z * Segment.Z;
	if (Dot <= 0)
	{
		return 0;
	}
	if (Dot >= LengthSquared)
	{
		return ParameterOneQ32;
	}

	// Splitting the fraction into two 16-bit quotient steps keeps both shifted
	// numerators below 3 * MaxCapsuleSpanCm^2 * 2^16. Round the last remainder
	// to nearest, ties away from zero.
	constexpr uint64 MaxSegmentSquared = 3 * uint64(MaxCapsuleSpanCm) * uint64(MaxCapsuleSpanCm);
	static_assert(MaxSegmentSquared <= (MAX_uint64 >> 16));
	const uint64 Denominator = uint64(LengthSquared);
	const uint64 HighNumerator = uint64(Dot) << 16;
	const uint64 LowNumerator = (HighNumerator % Denominator) << 16;
	const uint64 High = HighNumerator / Denominator;
	const uint64 Low = LowNumerator / Denominator;
	const uint64 RoundUp = (LowNumerator % Denominator) >= (Denominator + 1) / 2 ? 1 : 0;
	return int64((High << 16) + Low + RoundUp);
}

FInt64Vector FVoxelLandM6StampMath::CapsuleClosestOffsetQ8(const FInt64Vector& Delta, const FInt64Vector& Segment)
{
	constexpr int64 ParameterToCmQ8 = ParameterOneQ32 / CentimetreScaleQ8;
	static_assert(MaxCapsuleSpanCm <= MAX_int64 / ParameterOneQ32);
	const int64 T = CapsuleParameterQ32(Delta, Segment);
	return FInt64Vector(RoundRatio(Segment.X * T, ParameterToCmQ8), RoundRatio(Segment.Y * T, ParameterToCmQ8),
		RoundRatio(Segment.Z * T, ParameterToCmQ8));
}

int64 FVoxelLandM6StampMath::SignedDistanceQ8(const FVoxelLandM6Stamp& Stamp, const FInt64Vector& Position)
{
	const FInt64Vector Delta = Position - Stamp.OriginCm;
	if (Stamp.Shape == EVoxelLandM6StampShape::Box)
	{
		const FInt64Vector Gap(FMath::Abs(Delta.X) - Stamp.HalfExtentCm.X, FMath::Abs(Delta.Y) - Stamp.HalfExtentCm.Y,
			FMath::Abs(Delta.Z) - Stamp.HalfExtentCm.Z);
		const FInt64Vector Outside = Gap.ComponentMax(FInt64Vector(0));
		return LengthQ8(Outside * CentimetreScaleQ8)
			 + FMath::Min<int64>(0, FMath::Max3(Gap.X, Gap.Y, Gap.Z)) * CentimetreScaleQ8;
	}

	FInt64Vector Closest(0);
	if (Stamp.Shape == EVoxelLandM6StampShape::Capsule)
	{
		Closest = CapsuleClosestOffsetQ8(Delta, Stamp.SegmentEndCm - Stamp.OriginCm);
	}
	return LengthQ8(Delta * CentimetreScaleQ8 - Closest) - int64(Stamp.RadiusCm) * CentimetreScaleQ8;
}

int16 FVoxelLandM6StampMath::Combine(
	int16 Density, const FVoxelLandM6Stamp& Stamp, const FInt64Vector& PositionCm, int64 VoxelSizeCm)
{
	FInt64Vector Min, Max;
	GetBounds(Stamp, Min, Max);
	const int64 GuardCm = SaturationRadiusVoxels * VoxelSizeCm;
	// The distance outside an enclosing box is a lower bound on positive
	// shape distance. At 32 voxels it quantizes past both int16 extremes
	// before saturation, making either hard operation an identity.
	if (PositionCm.X <= Min.X - GuardCm || PositionCm.X >= Max.X + GuardCm || PositionCm.Y <= Min.Y - GuardCm
		|| PositionCm.Y >= Max.Y + GuardCm || PositionCm.Z <= Min.Z - GuardCm || PositionCm.Z >= Max.Z + GuardCm)
	{
		return Density;
	}
	const bool bSubtract = Stamp.Operation == EVoxelLandM6StampOperation::Subtract;
	const int16 Shape = QuantizeDistanceQ8(SignedDistanceQ8(Stamp, PositionCm), VoxelSizeCm, bSubtract);
	return bSubtract ? FMath::Max(Density, Shape) : FMath::Min(Density, Shape);
}

int16 FVoxelLandM6StampMath::QuantizeDistanceQ8(int64 DistanceQ8, int64 VoxelSizeCm, bool bSubtract)
{
	const int64 DensityScale = DensityUnitsPerVoxel / CentimetreScaleQ8;
	const int64 Numerator = DistanceQ8 * (bSubtract ? -DensityScale : DensityScale);
	const int64 Rounded = RoundRatio(Numerator, VoxelSizeCm);
	return int16(FMath::Clamp<int64>(Rounded, MIN_int16, MAX_int16));
}
