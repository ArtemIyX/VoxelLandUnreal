#include "Materials/VoxelLandMaterialPaletteBaker.h"

#include "VoxelLandMaterialArrayBaker.h"
#include "VoxelLandMaterialTextureChannel.h"
#include "Materials/VoxelLandTerrainMaterialBuilder.h"
#include "Data/Assets/Materials/VoxelLandMaterialPalette.h"
#include "Data/Assets/Materials/VoxelLandSurfaceMaterial.h"
#include "Engine/Texture2DArray.h"
#include "Materials/Material.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectGlobals.h"

#define LOCTEXT_NAMESPACE "VoxelLandMaterialPaletteBaker"

class FVoxelLandMaterialPaletteAuthoring final
{
public:
	[[nodiscard]] static bool BuildSlots(
		const UVoxelLandMaterialPalette& Palette, TArray<FVoxelLandMaterialPaletteSlot>& OutSlots, FText& OutError)
	{
		if (Palette.Surfaces.IsEmpty() || Palette.Surfaces.Num() > UVoxelLandMaterialPalette::MaxSlots)
		{
			OutError = FText::Format(
				LOCTEXT("SlotCount", "Add between 1 and {0} surfaces. Slot zero is the required fallback."),
				FText::AsNumber(UVoxelLandMaterialPalette::MaxSlots));
			return false;
		}

		TSet<FGuid> Ids;
		OutSlots.Reserve(Palette.Surfaces.Num());
		for (int32 Index = 0; Index < Palette.Surfaces.Num(); ++Index)
		{
			const UVoxelLandSurfaceMaterial* Surface = Palette.Surfaces[Index];
			if (!Surface)
			{
				OutError =
					FText::Format(LOCTEXT("MissingSlot", "Select a surface for slot {0}."), FText::AsNumber(Index));
				return false;
			}
			FVoxelLandMaterialPaletteSlot Slot;
			Slot.SurfaceId = Surface->SurfaceId;
			Slot.TilePeriodCm = Surface->TilePeriodCm;
			Slot.Tint = Surface->Tint;
			Slot.HeightBlendContrast = Surface->HeightBlendContrast;
			if (!Slot.IsValid() || Ids.Contains(Slot.SurfaceId))
			{
				OutError = FText::Format(
					LOCTEXT("BadSlot", "Slot {0} ({1}) has invalid appearance parameters or a duplicate surface ID."),
					FText::AsNumber(Index), FText::FromString(Surface->GetPathName()));
				return false;
			}
			Ids.Add(Slot.SurfaceId);
			OutSlots.Add(Slot);
		}
		return true;
	}

	[[nodiscard]] static bool BuildResources(
		UVoxelLandMaterialPalette& Palette, UVoxelLandMaterialPalette& Staged, FText& OutError)
	{
		Staged.BaseColorArray =
			FVoxelLandMaterialArrayBaker::Build(Palette, EVoxelLandMaterialTextureChannel::BaseColor, OutError);
		if (!Staged.BaseColorArray)
		{
			return false;
		}
		Staged.NormalArray =
			FVoxelLandMaterialArrayBaker::Build(Palette, EVoxelLandMaterialTextureChannel::Normal, OutError);
		if (!Staged.NormalArray)
		{
			return false;
		}
		Staged.ORMHArray =
			FVoxelLandMaterialArrayBaker::Build(Palette, EVoxelLandMaterialTextureChannel::ORMH, OutError);
		if (!Staged.ORMHArray)
		{
			return false;
		}
		if (!Staged.MasterMaterial)
		{
			const FName Name = MakeUniqueObjectName(&Palette, UMaterial::StaticClass(), TEXT("TerrainMaster"));
			Staged.MasterMaterial = FVoxelLandTerrainMaterialBuilder::Build(&Palette, Name, Staged);
		}
		return Staged.ValidateBake(OutError);
	}
};

bool FVoxelLandMaterialPaletteBaker::Bake(UVoxelLandMaterialPalette& Palette, FText& OutError)
{
	if (!IsInGameThread())
	{
		OutError = LOCTEXT("WrongThread", "Material palette baking requires the game thread.");
		return false;
	}
	OutError = FText::GetEmpty();
	const TStrongObjectPtr<UVoxelLandMaterialPalette> Staged(
		NewObject<UVoxelLandMaterialPalette>(GetTransientPackage()));
	if (!FVoxelLandMaterialPaletteAuthoring::BuildSlots(Palette, Staged->BakedSlots, OutError)
		|| !FVoxelLandMaterialArrayBaker::ValidateSourceBudget(Palette, OutError))
	{
		return false;
	}
	Staged->MasterMaterial = FVoxelLandTerrainMaterialBuilder::LoadSharedMaster();
	if (!Staged->MasterMaterial && Palette.GetPackage() != GetTransientPackage())
	{
		OutError = LOCTEXT("MissingMaster",
			"The VoxelLand terrain master material is missing. Restore it from the plugin before baking a saved palette.");
		return false;
	}
	Staged->BakeVersion = UVoxelLandMaterialPalette::CurrentBakeVersion;
	Staged->LayoutIdentity = UVoxelLandMaterialPalette::MakeLayoutIdentity(Staged->BakedSlots);
	Staged->ShadingIdentity = FGuid::NewGuid();
	if (!FVoxelLandMaterialPaletteAuthoring::BuildResources(Palette, *Staged, OutError))
	{
		return false;
	}
	TArray<FGuid> SourceFingerprints;
	if (!Palette.CaptureSourceFingerprints(SourceFingerprints))
	{
		OutError =
			LOCTEXT("ChangedSources", "Unable to fingerprint the source art. Verify the textures and bake again.");
		return false;
	}

	Palette.Modify();
	Palette.BakeVersion = Staged->BakeVersion;
	Palette.LayoutIdentity = Staged->LayoutIdentity;
	Palette.ShadingIdentity = Staged->ShadingIdentity;
	Palette.BakedSlots = MoveTemp(Staged->BakedSlots);
	Palette.BaseColorArray = Staged->BaseColorArray;
	Palette.NormalArray = Staged->NormalArray;
	Palette.ORMHArray = Staged->ORMHArray;
	Palette.MasterMaterial = Staged->MasterMaterial;
	Palette.SourceFingerprints = MoveTemp(SourceFingerprints);
	Palette.MarkPackageDirty();
	Palette.PostEditChange();
	return true;
}

#undef LOCTEXT_NAMESPACE
