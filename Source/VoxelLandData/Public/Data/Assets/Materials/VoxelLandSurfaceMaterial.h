#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "VoxelLandSurfaceMaterial.generated.h"

class UTexture2D;

UCLASS(BlueprintType, HideDropdown, meta = (DisplayName = "Surface Material"))
class VOXELLANDDATA_API UVoxelLandSurfaceMaterial final : public UDataAsset
{
	GENERATED_BODY()

public:
	static constexpr double MinTilePeriodCm = 1.0;
	static constexpr double MaxTilePeriodCm = 1000000.0;

	UPROPERTY(VisibleAnywhere, Category = "Identity")
	FGuid SurfaceId;

#if WITH_EDITORONLY_DATA
	UPROPERTY(EditAnywhere, Category = "Textures", meta = (ToolTip = "Tileable sRGB albedo."))
	TObjectPtr<UTexture2D> BaseColor;

	UPROPERTY(EditAnywhere, Category = "Textures", meta = (ToolTip = "Tileable linear tangent-space normal."))
	TObjectPtr<UTexture2D> Normal;

	UPROPERTY(EditAnywhere, Category = "Textures",
		meta = (ToolTip = "Linear packed channels: occlusion, roughness, metallic, and blend height in alpha."))
	TObjectPtr<UTexture2D> ORMH;

	UPROPERTY(EditAnywhere, Category = "Textures",
		meta = (ToolTip =
					"Confirm that an entirely opaque alpha channel intentionally represents a constant full blend height."))
	bool bAllowOpaqueBlendHeight = false;

	UPROPERTY(EditAnywhere, Category = "Appearance", meta = (Units = "cm", ClampMin = "1.0", ClampMax = "1000000.0"))
	double TilePeriodCm = 512.0;

	UPROPERTY(EditAnywhere, Category = "Appearance")
	FLinearColor Tint = FLinearColor::White;

	UPROPERTY(EditAnywhere, Category = "Appearance", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float HeightBlendContrast = 0.25f;
#endif

protected:
	virtual void PostInitProperties() override;
#if WITH_EDITOR
	virtual void PostDuplicate(EDuplicateMode::Type DuplicateMode) override;
#endif
};
