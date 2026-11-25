#include "Data/Structs/VoxelLandM6WorldOverlay.h"

#include "Data/Structs/VoxelLandGeneratorSettings.h"

bool FVoxelLandM6WorldOverlay::IsActive() const
{
	return GetStamps().IsActive() || GetFilters().IsActive() || Redistance.bEnabled;
}

FVoxelLandM6StampSettings FVoxelLandM6WorldOverlay::GetStamps() const
{
	FVoxelLandM6StampSettings Settings;
	Settings.bEnabled = bStampsEnabled;
	Settings.Stamps = Stamps;
	return Settings;
}

FVoxelLandM6FilterSettings FVoxelLandM6WorldOverlay::GetFilters() const
{
	FVoxelLandM6FilterSettings Settings;
	Settings.bEnabled = bFiltersEnabled;
	Settings.Stages = Filters;
	return Settings;
}

void FVoxelLandM6WorldOverlay::ApplyTo(FVoxelLandGeneratorSettings& Settings) const
{
	Settings.M6Stamps = GetStamps();
	Settings.M6Filters = GetFilters();
	Settings.M6Redistance = Redistance;
}
