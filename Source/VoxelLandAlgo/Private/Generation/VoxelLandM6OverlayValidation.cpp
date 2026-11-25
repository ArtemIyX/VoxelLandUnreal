#include "Generation/VoxelLandM6OverlayValidation.h"

#include "Data/Structs/VoxelLandM6FilterSettings.h"
#include "Data/Structs/VoxelLandM6StampSettings.h"
#include "Data/Structs/VoxelLandM6WorldOverlay.h"
#include "Generation/VoxelLandM6StampMath.h"

FVoxelLandError FVoxelLandM6OverlayValidation::ValidateVoxelSize(float VoxelSizeCm)
{
	if (!FMath::IsFinite(VoxelSizeCm) || VoxelSizeCm < 1.f
		|| VoxelSizeCm > float(FVoxelLandM6StampMath::MaxVoxelSizeCm)
		|| FMath::FloorToDouble(double(VoxelSizeCm)) != double(VoxelSizeCm))
	{
		return {EVoxelLandErrorCode::InvalidProfile, TEXT("M6 requires an integer voxel size between 1 and 134217 centimetres")};
	}
	return {};
}

FVoxelLandError FVoxelLandM6OverlayValidation::Validate(const FVoxelLandM6WorldOverlay& Overlay, float VoxelSizeCm)
{
	// Bound the copied asset data even when a list is disabled.
	if (Overlay.Stamps.Num() > FVoxelLandM6StampSettings::MaxStamps
		|| Overlay.Filters.Num() > FVoxelLandM6FilterSettings::MaxStages)
	{
		return {EVoxelLandErrorCode::InvalidParameter, TEXT("M6 world config supports at most 16 stamps and 8 region filters")};
	}
	const FVoxelLandM6StampSettings Stamps = Overlay.GetStamps();
	const FVoxelLandM6FilterSettings Filters = Overlay.GetFilters();
	if (Stamps.IsActive())
	{
		if (const FVoxelLandError Error = ValidateStamps(Stamps, VoxelSizeCm))
		{
			return Error;
		}
	}
	if (Filters.IsActive())
	{
		if (const FVoxelLandError Error = ValidateFilters(Filters, VoxelSizeCm))
		{
			return Error;
		}
	}
	if (Overlay.Redistance.bEnabled)
	{
		return ValidateRedistance(Overlay.Redistance,
			Filters.IsActive() ? Filters.Stages : TConstArrayView<FVoxelLandM6FilterStage>(), VoxelSizeCm);
	}
	return {};
}
