#include "World/Terrain/Persistence/VoxelLandSavedTerrainWorld.h"

#include "Data/Terrain/Persistence/VoxelLandSavedTerrainWorldRecord.h"
#include "World/Terrain/Persistence/VoxelLandWorldIdentity.h"
#include "World/Terrain/Persistence/VoxelLandSavedTerrainWorldPreflight.h"

bool VoxelLandM4SavedWorld::Capture(
	const FVoxelLandM4WorldSnapshot& Snapshot, FVoxelLandM4SavedWorld& Out, FString& Error)
{
	if (!Snapshot.AuthoredHeightOutputId.IsValid())
	{
		Error =
			TEXT("Cannot save legacy three-channel Height. Reauthor a named Float output and regenerate the world.");
		return false;
	}
	// Validate metadata before the identity encoder or the saved copy allocate.
	if (!VoxelLandM4SavedWorldPreflight::ValidateSnapshot(Snapshot, Error))
	{
		return false;
	}
	FVoxelLandM4SavedWorld Saved;
	Saved.FormatVersion = FVoxelLandM4SavedWorld::AuthoredFormatVersion;
	if (!VoxelLandM4WorldIdentity::Capture(Snapshot, Saved.Identity, Error))
	{
		return false;
	}
	Saved.Snapshot = Snapshot;
	Out = MoveTemp(Saved);
	return true;
}

bool VoxelLandM4SavedWorld::Restore(const FVoxelLandM4SavedWorld& Saved, FVoxelLandM4WorldSnapshot& Out, FString& Error)
{
	if (!VoxelLandM4SavedWorldPreflight::Validate(Saved, Error))
	{
		return false;
	}
	if (!VoxelLandM4WorldIdentity::Matches(Saved.Identity, Saved.Snapshot, Error))
	{
		return false;
	}
	Out = Saved.Snapshot;
	return true;
}