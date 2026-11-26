#include "Data/Terrain/Worlds/VoxelLandWorldConfig.h"

#if WITH_EDITOR
#include "Data/Terrain/Worlds/VoxelLandTerrainWorldSnapshot.h"
#include "Misc/DataValidation.h"

EDataValidationResult UVoxelLandWorldConfig::IsDataValid(FDataValidationContext& Context) const
{
	FString Error;
	FVoxelLandM4WorldSnapshot Snapshot;
	bool bValid = false;
	if (IsDirectDensity())
	{
		FVoxelLandGraphSnapshot Bound;
		bValid =
			ValidateDirectDensitySettings(Error) && (!TerrainOutputId.IsValid() || CaptureBoundOutput(Bound, Error));
	}
	else if (!TerrainOutputId.IsValid())
	{
		Error = TEXT(
			"Height World Config requires a named Float output. Reauthor legacy three-channel terrain and regenerate the world.");
	}
	else
	{
		bValid = CaptureSnapshot(Snapshot, Error);
	}
	if (!bValid)
	{
		Context.AddError(FText::FromString(Error));
	}
	return bValid ? EDataValidationResult::Valid : EDataValidationResult::Invalid;
}
#endif
