#include "Data/Assets/Materials/VoxelLandSurfaceMaterial.h"

void UVoxelLandSurfaceMaterial::PostInitProperties()
{
	Super::PostInitProperties();
#if WITH_EDITOR
	if (!HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject | RF_NeedLoad) && !SurfaceId.IsValid())
	{
		SurfaceId = FGuid::NewGuid();
	}
#endif
}

#if WITH_EDITOR
void UVoxelLandSurfaceMaterial::PostDuplicate(const EDuplicateMode::Type DuplicateMode)
{
	Super::PostDuplicate(DuplicateMode);
	if (DuplicateMode == EDuplicateMode::Normal)
	{
		SurfaceId = FGuid::NewGuid();
	}
}
#endif
