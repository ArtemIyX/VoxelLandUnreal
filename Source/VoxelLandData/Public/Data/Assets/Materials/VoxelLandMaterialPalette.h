#pragma once

#include "CoreMinimal.h"
#include "Data/Assets/Materials/VoxelLandMaterialPaletteSlot.h"
#include "Engine/DataAsset.h"
#include "VoxelLandMaterialPalette.generated.h"

class FVoxelLandMaterialPaletteBaker;
class UMaterialInterface;
class UTexture2DArray;
class UVoxelLandSurfaceMaterial;

UCLASS(BlueprintType, HideDropdown, meta = (DisplayName = "Material Palette"))
class VOXELLANDDATA_API UVoxelLandMaterialPalette final : public UDataAsset
{
	GENERATED_BODY()

public:
	static constexpr int32 MaxSlots = 32;
	static constexpr int32 CurrentBakeVersion = 1;

#if WITH_EDITORONLY_DATA
	UPROPERTY(EditAnywhere, Category = "Palette",
		meta = (ToolTip = "Slot 0 is the fallback. List order determines baked texture slices."))
	TArray<TObjectPtr<UVoxelLandSurfaceMaterial>> Surfaces;
#endif

	UPROPERTY(VisibleAnywhere, Category = "Bake")
	int32 BakeVersion = 0;

	UPROPERTY(VisibleAnywhere, Category = "Bake")
	FGuid LayoutIdentity;

	UPROPERTY(VisibleAnywhere, Category = "Bake")
	FGuid ShadingIdentity;

	UPROPERTY(VisibleAnywhere, Category = "Bake")
	TArray<FVoxelLandMaterialPaletteSlot> BakedSlots;

	UPROPERTY(VisibleAnywhere, Category = "Bake")
	TObjectPtr<UTexture2DArray> BaseColorArray;

	UPROPERTY(VisibleAnywhere, Category = "Bake")
	TObjectPtr<UTexture2DArray> NormalArray;

	UPROPERTY(VisibleAnywhere, Category = "Bake")
	TObjectPtr<UTexture2DArray> ORMHArray;

	UPROPERTY(VisibleAnywhere, Category = "Bake")
	TObjectPtr<UMaterialInterface> MasterMaterial;

	[[nodiscard]] bool ValidateBake(FText& OutError) const;
	[[nodiscard]] static FGuid MakeLayoutIdentity(TConstArrayView<FVoxelLandMaterialPaletteSlot> Slots);

#if WITH_EDITOR
	[[nodiscard]] bool IsBakeStale() const;
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif

private:
	friend class FVoxelLandMaterialPaletteBaker;

#if WITH_EDITOR
	[[nodiscard]] bool CaptureSourceFingerprints(TArray<FGuid>& OutFingerprints) const;
#endif

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	TArray<FGuid> SourceFingerprints;
#endif
};
