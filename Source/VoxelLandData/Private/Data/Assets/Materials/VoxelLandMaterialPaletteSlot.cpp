#include "Data/Assets/Materials/VoxelLandMaterialPaletteSlot.h"

#include "Data/Assets/Materials/VoxelLandSurfaceMaterial.h"

bool FVoxelLandMaterialPaletteSlot::IsValid() const
{
	return SurfaceId.IsValid() && FMath::IsFinite(TilePeriodCm)
		&& TilePeriodCm >= UVoxelLandSurfaceMaterial::MinTilePeriodCm
		&& TilePeriodCm <= UVoxelLandSurfaceMaterial::MaxTilePeriodCm && FMath::IsFinite(Tint.R)
		&& FMath::IsFinite(Tint.G) && FMath::IsFinite(Tint.B) && FMath::IsFinite(Tint.A) && Tint.R >= 0.0f
		&& Tint.G >= 0.0f && Tint.B >= 0.0f && FMath::IsFinite(HeightBlendContrast) && HeightBlendContrast >= 0.0f
		&& HeightBlendContrast <= 1.0f;
}
