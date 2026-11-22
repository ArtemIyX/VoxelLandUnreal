#include "World/VoxelLandM4SavedWorld.h"

#include "Data/Structs/VoxelLandM4SavedWorld.h"
#include "World/VoxelLandM4WorldIdentity.h"
#include "World/VoxelLandM4SavedWorldPreflight.h"

bool VoxelLandM4SavedWorld::Capture(
	const FVoxelLandM4WorldSnapshot& Snapshot, FVoxelLandM4SavedWorld& Out, FString& Error)
{
	// Validate metadata before the identity encoder or the saved copy allocate.
	if (!VoxelLandM4SavedWorldPreflight::ValidateSnapshot(Snapshot, Error))
	{
		return false;
	}
	FVoxelLandM4SavedWorld Saved;
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
