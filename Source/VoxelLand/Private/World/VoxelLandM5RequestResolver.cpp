#include "World/VoxelLandM5RequestResolver.h"

#include "Data/Structs/VoxelLandChunkKey.h"
#include "Data/Structs/VoxelLandM5Bounds.h"
#include "Data/Structs/VoxelLandM5ChunkFootprint.h"
#include "Data/Structs/VoxelLandM5WorldSnapshot.h"
#include "Generation/VoxelLandM5ChunkFootprint.h"
#include "World/VoxelLandM5ResolvedRequest.h"

class FVoxelLandM5RequestResolverInternals final
{
public:
	static constexpr int32 MaxIdentityBytes = 8 * 1024 * 1024 + 4096;

	static FVoxelLandM5Diagnostic Error(EVoxelLandM5ErrorCode Code, const TCHAR* Message)
	{
		FVoxelLandM5Diagnostic Result;
		Result.Code = Code;
		Result.Stage = TEXT("M5.RequestResolution");
		Result.Message = Message;
		return Result;
	}

	static bool IsValid(const FVoxelLandM5Bounds& Bounds)
	{
		return Bounds.MinInclusive.X < Bounds.MaxExclusive.X && Bounds.MinInclusive.Y < Bounds.MaxExclusive.Y
			&& Bounds.MinInclusive.Z < Bounds.MaxExclusive.Z;
	}

	static bool Contains(const FVoxelLandM5Bounds& Bounds, const FVoxelLandM5PointCm& Point)
	{
		return Point.X >= Bounds.MinInclusive.X && Point.X < Bounds.MaxExclusive.X && Point.Y >= Bounds.MinInclusive.Y
			&& Point.Y < Bounds.MaxExclusive.Y && Point.Z >= Bounds.MinInclusive.Z && Point.Z < Bounds.MaxExclusive.Z;
	}

	static bool AxisIntersects(int64 First, int64 Last, int64 Min, int64 Max) { return First < Max && Last >= Min; }
};

FVoxelLandM5Diagnostic FVoxelLandM5RequestResolver::Resolve(const FVoxelLandM5WorldSnapshot& Snapshot,
	const FVoxelLandM5PointCm& Location, TConstArrayView<uint8> ValidatedIdentity,
	FVoxelLandM5ResolvedRequest& OutRequest)
{
	FVoxelLandM5ResolvedRequest Result;
	if (!Snapshot.Caves.bEnabled && !Snapshot.Caves.Pockets.bEnabled)
	{
		Result.bDisabled = true;
		OutRequest = MoveTemp(Result);
		return {};
	}
	if (Snapshot.Caves.PlanningMode != EVoxelLandM5PlanningMode::Finite)
	{
		return FVoxelLandM5RequestResolverInternals::Error(
			EVoxelLandM5ErrorCode::Unproven, TEXT("Regional request closure is not available"));
	}
	if (!FVoxelLandM5RequestResolverInternals::IsValid(Snapshot.Caves.Bounds) || ValidatedIdentity.IsEmpty()
		|| ValidatedIdentity.Num() > FVoxelLandM5RequestResolverInternals::MaxIdentityBytes)
	{
		return FVoxelLandM5RequestResolverInternals::Error(
			EVoxelLandM5ErrorCode::InvalidInput, TEXT("Finite bounds or validated identity are invalid"));
	}
	if (!FVoxelLandM5RequestResolverInternals::Contains(Snapshot.Caves.Bounds, Location))
	{
		return FVoxelLandM5RequestResolverInternals::Error(
			EVoxelLandM5ErrorCode::OutOfDomain, TEXT("Request location is outside finite cave bounds"));
	}
	Result.Identity.Append(ValidatedIdentity.GetData(), ValidatedIdentity.Num());
	OutRequest = MoveTemp(Result);
	return {};
}

FVoxelLandM5Diagnostic FVoxelLandM5RequestResolver::IntersectsStoredSamples(const FVoxelLandM5WorldSnapshot& Snapshot,
	const FVoxelLandM5Bounds& Influence, const FVoxelLandChunkKey& Key, bool& bOutIntersects)
{
	if (!Snapshot.Caves.bEnabled && !Snapshot.Caves.Pockets.bEnabled)
	{
		bOutIntersects = false;
		return {};
	}
	if (!FVoxelLandM5RequestResolverInternals::IsValid(Influence) || Snapshot.Base.VoxelSizeCm <= 0 || Key.LOD != 0)
	{
		return FVoxelLandM5RequestResolverInternals::Error(
			EVoxelLandM5ErrorCode::InvalidInput, TEXT("Invalid influence bounds, voxel size, or chunk LOD"));
	}
	FVoxelLandM5ChunkFootprint Footprint;
	// Gate conservatively for smoothing regardless of the current configuration.
	if (!VoxelLand::M5::ChunkFootprint::TryBuild(Key, Snapshot.Base.VoxelSizeCm, true, false, Footprint))
	{
		return FVoxelLandM5RequestResolverInternals::Error(
			EVoxelLandM5ErrorCode::ArithmeticOverflow, TEXT("Chunk sample footprint overflows"));
	}
	bOutIntersects = FVoxelLandM5RequestResolverInternals::AxisIntersects(Footprint.DensityInputMin.X,
						 Footprint.DensityInputMax.X, Influence.MinInclusive.X, Influence.MaxExclusive.X)
				  && FVoxelLandM5RequestResolverInternals::AxisIntersects(Footprint.DensityInputMin.Y,
					  Footprint.DensityInputMax.Y, Influence.MinInclusive.Y, Influence.MaxExclusive.Y)
				  && FVoxelLandM5RequestResolverInternals::AxisIntersects(Footprint.DensityInputMin.Z,
					  Footprint.DensityInputMax.Z, Influence.MinInclusive.Z, Influence.MaxExclusive.Z);
	return {};
}
