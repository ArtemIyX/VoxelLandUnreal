#include "Data/Assets/Materials/VoxelLandMaterialPalette.h"

#include "Engine/Texture2DArray.h"
#include "Materials/MaterialInterface.h"
#if WITH_EDITOR
#include "Data/Assets/Materials/VoxelLandSurfaceMaterial.h"
#include "Engine/Texture2D.h"
#include "Misc/DataValidation.h"
#endif

#define LOCTEXT_NAMESPACE "VoxelLandMaterialPalette"

class FVoxelLandPaletteValidation final
{
public:
	[[nodiscard]] static int32 GetSliceCount(const UTexture2DArray& Array)
	{
#if WITH_EDITORONLY_DATA
		if (Array.Source.IsValid())
		{
			return Array.Source.GetNumSlices();
		}
#endif
		return Array.GetArraySize();
	}

#if WITH_EDITOR
	[[nodiscard]] static bool AppendTextureFingerprint(FString& Key, const UTexture2D* Texture)
	{
		if (!Texture || !Texture->Source.IsValid())
		{
			return false;
		}
		Key += FString::Printf(TEXT("/%s/%d/%d/%d/%d/%d/%d/%d/%d"), *Texture->Source.GetId().ToString(),
			static_cast<int32>(Texture->SRGB), static_cast<int32>(Texture->AddressX),
			static_cast<int32>(Texture->AddressY), static_cast<int32>(Texture->MipGenSettings),
			static_cast<int32>(Texture->CompressionSettings), static_cast<int32>(Texture->CompressionNoAlpha),
			static_cast<int32>(Texture->bFlipGreenChannel), static_cast<int32>(Texture->VirtualTextureStreaming));
		return true;
	}
#endif
};

FGuid UVoxelLandMaterialPalette::MakeLayoutIdentity(const TConstArrayView<FVoxelLandMaterialPaletteSlot> Slots)
{
	FString Key(TEXT("VoxelLandMaterialPaletteSlots/"));
	for (const FVoxelLandMaterialPaletteSlot& Slot : Slots)
	{
		Key += Slot.SurfaceId.ToString(EGuidFormats::Digits);
	}
	return FGuid::NewDeterministicGuid(Key);
}

bool UVoxelLandMaterialPalette::ValidateBake(FText& OutError) const
{
	OutError = FText::GetEmpty();
	if (BakeVersion != CurrentBakeVersion || BakedSlots.IsEmpty() || BakedSlots.Num() > MaxSlots
		|| !ShadingIdentity.IsValid())
	{
		OutError = FText::Format(LOCTEXT("InvalidManifest", "Palette {0} has no supported bake. Bake the palette."),
			FText::FromString(GetPathName()));
		return false;
	}

	TSet<FGuid> AssignedIds;
	for (int32 Index = 0; Index < BakedSlots.Num(); ++Index)
	{
		const FVoxelLandMaterialPaletteSlot& Slot = BakedSlots[Index];
		if (!Slot.IsValid() || AssignedIds.Contains(Slot.SurfaceId))
		{
			OutError =
				FText::Format(LOCTEXT("InvalidSlot",
								  "Palette {0} has invalid baked parameters or a duplicate surface ID in slot {1}."),
					FText::FromString(GetPathName()), FText::AsNumber(Index));
			return false;
		}
		AssignedIds.Add(Slot.SurfaceId);
	}
	const int32 SliceCount = BakedSlots.Num();
	if (LayoutIdentity != MakeLayoutIdentity(BakedSlots) || !MasterMaterial || !BaseColorArray || !NormalArray
		|| !ORMHArray || FVoxelLandPaletteValidation::GetSliceCount(*BaseColorArray) != SliceCount
		|| FVoxelLandPaletteValidation::GetSliceCount(*NormalArray) != SliceCount
		|| FVoxelLandPaletteValidation::GetSliceCount(*ORMHArray) != SliceCount)
	{
		OutError =
			FText::Format(LOCTEXT("InvalidResources",
							  "Palette {0} has missing or incompatible baked arrays or material. Bake the palette."),
				FText::FromString(GetPathName()));
		return false;
	}
	return true;
}

#if WITH_EDITOR
bool UVoxelLandMaterialPalette::CaptureSourceFingerprints(TArray<FGuid>& OutFingerprints) const
{
	OutFingerprints.Reset(Surfaces.Num());
	for (const UVoxelLandSurfaceMaterial* Surface : Surfaces)
	{
		if (!Surface)
		{
			return false;
		}

		FString Key = FString::Printf(TEXT("%s/%.17g/%.17g/%.17g/%.17g/%.17g/%.17g/%d"), *Surface->SurfaceId.ToString(),
			Surface->TilePeriodCm, Surface->Tint.R, Surface->Tint.G, Surface->Tint.B, Surface->Tint.A,
			Surface->HeightBlendContrast, static_cast<int32>(Surface->bAllowOpaqueBlendHeight));
		if (!FVoxelLandPaletteValidation::AppendTextureFingerprint(Key, Surface->BaseColor)
			|| !FVoxelLandPaletteValidation::AppendTextureFingerprint(Key, Surface->Normal)
			|| !FVoxelLandPaletteValidation::AppendTextureFingerprint(Key, Surface->ORMH))
		{
			return false;
		}
		OutFingerprints.Add(FGuid::NewDeterministicGuid(Key));
	}
	return true;
}

bool UVoxelLandMaterialPalette::IsBakeStale() const
{
	TArray<FGuid> Fingerprints;
	return BakeVersion != CurrentBakeVersion || SourceFingerprints.Num() != BakedSlots.Num()
		|| !CaptureSourceFingerprints(Fingerprints) || SourceFingerprints != Fingerprints;
}

EDataValidationResult UVoxelLandMaterialPalette::IsDataValid(FDataValidationContext& Context) const
{
	FText Error;
	if (!ValidateBake(Error))
	{
		Context.AddError(Error);
		return EDataValidationResult::Invalid;
	}
	if (IsBakeStale())
	{
		Context.AddWarning(LOCTEXT(
			"StaleBake", "The palette sources changed. The previous bake remains active until you bake again."));
	}
	const EDataValidationResult ParentResult = Super::IsDataValid(Context);
	return ParentResult == EDataValidationResult::Invalid ? ParentResult : EDataValidationResult::Valid;
}
#endif

#undef LOCTEXT_NAMESPACE
