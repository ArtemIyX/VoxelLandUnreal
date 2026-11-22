#include "Data/Assets/VoxelLandInfiniteWorld.h"

#include "Data/Structs/VoxelLandInfiniteWorldSnapshot.h"

bool UVoxelLandInfiniteWorld::CaptureSnapshot(FVoxelLandInfiniteWorldSnapshot& OutSnapshot, FString& OutError) const
{
	OutError.Reset();
	if (!IsInGameThread())
	{
		OutError = TEXT("Infinite world settings must be captured on the game thread.");
		return false;
	}
	const FVoxelLandInfiniteWorldSettings Authored = Infinite;
	if (Authored.RegionSizeCm <= 0 || Common.VoxelSizeCm <= 0 || Authored.RegionSizeCm % Common.VoxelSizeCm != 0
		|| Authored.MinZCm >= Authored.MaxZCm)
	{
		OutError = TEXT("Infinite regions require a positive voxel-aligned width and ordered vertical bounds.");
		return false;
	}
	FVoxelLandInfiniteWorldSnapshot Scratch;
	if (!CaptureCommon(EVoxelLandWorldKind::Infinite, Scratch.Common, OutError))
	{
		return false;
	}
	if (!FVoxelLandInfiniteWorldSettings::StaticStruct()->CompareScriptStruct(&Infinite, &Authored, 0))
	{
		OutError = TEXT("Infinite world settings changed during capture.");
		return false;
	}
	Scratch.Infinite = Authored;
	Scratch.Common.Caves.RegionSizeCm = Authored.RegionSizeCm;
	Scratch.Common.Caves.RegionOriginXCm = Authored.RegionOriginXCm;
	Scratch.Common.Caves.RegionOriginYCm = Authored.RegionOriginYCm;
	// XY bounds belong to a requested region, not to an infinite world.
	Scratch.Common.Caves.Bounds.MinInclusive.X = 0;
	Scratch.Common.Caves.Bounds.MinInclusive.Y = 0;
	Scratch.Common.Caves.Bounds.MaxExclusive.X = 0;
	Scratch.Common.Caves.Bounds.MaxExclusive.Y = 0;
	Scratch.Common.Caves.Bounds.MinInclusive.Z = Authored.MinZCm;
	Scratch.Common.Caves.Bounds.MaxExclusive.Z = Authored.MaxZCm;
	OutSnapshot = MoveTemp(Scratch);
	return true;
}
