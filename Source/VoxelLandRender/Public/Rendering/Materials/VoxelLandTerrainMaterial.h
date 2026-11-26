#pragma once

#include "CoreMinimal.h"

class UObject;
class UMaterialInstanceDynamic;
class UPrimitiveComponent;
class UVoxelLandMaterialPalette;

class VOXELLANDRENDER_API FVoxelLandTerrainMaterial
{
public:
	static const FName BaseColorParameter;
	static const FName NormalParameter;
	static const FName ORMHParameter;
	static const FName LogicalOriginHighParameter;
	static const FName LogicalOriginLowParameter;
	static const FName FarDistanceParameter;
	static const FName DebugModeParameter;
	static const FName AmbientOcclusionStrengthParameter;

	static constexpr int32 AmbientOcclusionUVIndex = 5;
	static constexpr int32 LogicalOriginHighIndex = 0;
	static constexpr int32 LogicalOriginLowIndex = 4;
	static constexpr int32 PeriodsPerParameter = 4;
	static constexpr float DefaultFarDistanceCm = 50000.0f;

	[[nodiscard]] static UMaterialInstanceDynamic* CreateInstance(
		UObject* Outer, const UVoxelLandMaterialPalette& Palette);
	static void SetLogicalOrigin(UPrimitiveComponent& Component, const FVector3d& LogicalOrigin);

	[[nodiscard]] static FName SlotTintContrastParameter(int32 Slot);
	[[nodiscard]] static FName PeriodGroupParameter(int32 Group);
	[[nodiscard]] static FLinearColor SlotTintContrast(const UVoxelLandMaterialPalette& Palette, int32 Slot);
	[[nodiscard]] static FLinearColor PeriodGroup(const UVoxelLandMaterialPalette& Palette, int32 Group);
};
