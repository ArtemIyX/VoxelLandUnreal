#include "VoxelLandMaterialArrayBaker.h"

#include "VoxelLandMaterialTextureChannel.h"
#include "Data/Assets/Materials/VoxelLandMaterialPalette.h"
#include "Data/Assets/Materials/VoxelLandSurfaceMaterial.h"
#include "Engine/Texture2D.h"
#include "Engine/Texture2DArray.h"
#include "UObject/UObjectGlobals.h"

#define LOCTEXT_NAMESPACE "VoxelLandMaterialArrayBaker"

class FVoxelLandMaterialSource final
{
public:
	static constexpr int32 MaxSourceExtent = 2048;
	static constexpr int32 SourceMip = 0;

	[[nodiscard]] static FName GetArrayName(const EVoxelLandMaterialTextureChannel Channel)
	{
		switch (Channel)
		{
			case EVoxelLandMaterialTextureChannel::BaseColor:
				return TEXT("BaseColor");
			case EVoxelLandMaterialTextureChannel::Normal:
				return TEXT("Normal");
			case EVoxelLandMaterialTextureChannel::ORMH:
				return TEXT("ORMH");
		}
		checkNoEntry();
		return NAME_None;
	}

	[[nodiscard]] static UTexture2D* GetTexture(
		const UVoxelLandSurfaceMaterial& Surface, const EVoxelLandMaterialTextureChannel Channel)
	{
		switch (Channel)
		{
			case EVoxelLandMaterialTextureChannel::BaseColor:
				return Surface.BaseColor;
			case EVoxelLandMaterialTextureChannel::Normal:
				return Surface.Normal;
			case EVoxelLandMaterialTextureChannel::ORMH:
				return Surface.ORMH;
		}
		checkNoEntry();
		return nullptr;
	}

	[[nodiscard]] static FText GetError(const UVoxelLandSurfaceMaterial& Surface, const UTexture2D* Texture,
		const int32 Slot, const EVoxelLandMaterialTextureChannel Channel, const FText& Reason)
	{
		return FText::Format(LOCTEXT("BadSource", "Slot {0} ({1}), {2} texture {3}: {4}"), FText::AsNumber(Slot),
			FText::FromString(Surface.GetPathName()), FText::FromName(GetArrayName(Channel)),
			Texture ? FText::FromString(Texture->GetPathName()) : LOCTEXT("MissingTexture", "(missing)"), Reason);
	}

	[[nodiscard]] static FText ValidateTexture(
		const UTexture2D* Texture, const UTexture2D* FirstTexture, const EVoxelLandMaterialTextureChannel Channel)
	{
		if (!Texture || !Texture->Source.IsValid())
		{
			return LOCTEXT("BadArt", "Select a texture with valid source art.");
		}
		const FTextureSource& Source = Texture->Source;
		if (Source.GetFormat() != TSF_BGRA8 || Source.GetNumBlocks() != 1 || Source.GetNumSlices() != 1
			|| Source.GetNumLayers() != 1 || Source.GetNumMips() != 1 || !Source.AreAllBlocksPowerOfTwo()
			|| Source.GetSizeX() > MaxSourceExtent || Source.GetSizeY() > MaxSourceExtent)
		{
			return FText::Format(
				LOCTEXT("BadFormat", "Use a single top mip of power-of-two BGRA8 art up to {0} pixels on each axis."),
				FText::AsNumber(MaxSourceExtent));
		}

		const bool bExpectedSRGB = Channel == EVoxelLandMaterialTextureChannel::BaseColor;
		if (Texture->SRGB != bExpectedSRGB || Texture->VirtualTextureStreaming || Texture->AddressX != TA_Wrap
			|| Texture->AddressY != TA_Wrap)
		{
			return bExpectedSRGB ? LOCTEXT("BadSRGB", "Use a non-virtual sRGB texture with wrapping in X and Y.")
								 : LOCTEXT("BadLinear", "Use a non-virtual linear texture with wrapping in X and Y.");
		}
		if (Channel == EVoxelLandMaterialTextureChannel::Normal && Texture->bFlipGreenChannel)
		{
			return LOCTEXT(
				"BadNormalConvention", "Import normals with the Unreal green-channel convention in the source pixels.");
		}
		if (Channel == EVoxelLandMaterialTextureChannel::ORMH && Texture->CompressionNoAlpha)
		{
			return LOCTEXT("DisabledHeight", "Enable the alpha channel for packed blend height.");
		}
		if (FirstTexture
			&& (Source.GetSizeX() != FirstTexture->Source.GetSizeX()
				|| Source.GetSizeY() != FirstTexture->Source.GetSizeY()
				|| Texture->MipGenSettings != FirstTexture->MipGenSettings))
		{
			return LOCTEXT("Incompatible", "Match the dimensions and mip settings of slot zero in this array.");
		}
		return FText::GetEmpty();
	}

	[[nodiscard]] static bool HasBlendHeight(const FTextureSource::FMipLock& Mip, const int64 Size)
	{
		const FColor* Pixels = static_cast<const FColor*>(Mip.GetRawData());
		const int64 PixelCount = Size / sizeof(FColor);
		for (int64 Index = 0; Index < PixelCount; ++Index)
		{
			if (Pixels[Index].A != MAX_uint8)
			{
				return true;
			}
		}
		return false;
	}

	[[nodiscard]] static UTexture2DArray* PrepareArray(UVoxelLandMaterialPalette& Palette,
		const UTexture2D& FirstTexture, const EVoxelLandMaterialTextureChannel Channel)
	{
		const FName Name = MakeUniqueObjectName(&Palette, UTexture2DArray::StaticClass(), GetArrayName(Channel));
		UTexture2DArray* Array = NewObject<UTexture2DArray>(&Palette, Name);
		Array->PreEditChange(nullptr);
		Array->CompressionSettings = Channel == EVoxelLandMaterialTextureChannel::Normal ? TC_Normalmap : TC_BC7;
		Array->CompressionNoAlpha = Channel == EVoxelLandMaterialTextureChannel::BaseColor;
		Array->CompressionForceAlpha = Channel == EVoxelLandMaterialTextureChannel::ORMH;
		Array->SRGB = Channel == EVoxelLandMaterialTextureChannel::BaseColor;
		Array->NeverStream = true;
		Array->MipGenSettings = TMGS_SimpleAverage;
		Array->AddressX = TA_Wrap;
		Array->AddressY = TA_Wrap;
		Array->AddressZ = TA_Clamp;
		Array->Source.Init(static_cast<int32>(FirstTexture.Source.GetSizeX()),
			static_cast<int32>(FirstTexture.Source.GetSizeY()), Palette.Surfaces.Num(), 1, TSF_BGRA8);
		return Array;
	}

	[[nodiscard]] static FText CopySlice(UTexture2D& Texture, const UVoxelLandSurfaceMaterial& Surface,
		const EVoxelLandMaterialTextureChannel Channel, uint8* Destination, const int64 SliceSize)
	{
		const FTextureSource::FMipLock Mip(FTextureSource::ELockState::ReadOnly, &Texture.Source, SourceMip);
		if (!Mip.IsValid())
		{
			return LOCTEXT("UnreadableMip", "The texture source mip could not be read.");
		}
		if (Channel == EVoxelLandMaterialTextureChannel::ORMH && !Surface.bAllowOpaqueBlendHeight
			&& !HasBlendHeight(Mip, SliceSize))
		{
			return LOCTEXT("MissingHeight",
				"Packed alpha is entirely opaque. Supply blend height or confirm that constant full height is intentional.");
		}
		FMemory::Memcpy(Destination, Mip.GetRawData(), SliceSize);
		return FText::GetEmpty();
	}
};

bool FVoxelLandMaterialArrayBaker::TryAddSourceBytes(
	const int64 BytesPerSlice, const int32 SliceCount, int64& InOutSourceBytes)
{
	if (BytesPerSlice <= 0 || SliceCount <= 0 || InOutSourceBytes < 0 || InOutSourceBytes > MaxStagedSourceBytes
		|| BytesPerSlice > (MaxStagedSourceBytes - InOutSourceBytes) / SliceCount)
	{
		return false;
	}
	InOutSourceBytes += BytesPerSlice * SliceCount;
	return true;
}

bool FVoxelLandMaterialArrayBaker::ValidateSourceBudget(const UVoxelLandMaterialPalette& Palette, FText& OutError)
{
	check(!Palette.Surfaces.IsEmpty() && Palette.Surfaces[0]);
	const UVoxelLandSurfaceMaterial& First = *Palette.Surfaces[0];
	constexpr EVoxelLandMaterialTextureChannel Channels[] = {EVoxelLandMaterialTextureChannel::BaseColor,
		EVoxelLandMaterialTextureChannel::Normal, EVoxelLandMaterialTextureChannel::ORMH};
	int64 TotalSourceBytes = 0;
	for (const EVoxelLandMaterialTextureChannel Channel : Channels)
	{
		const UTexture2D* Texture = FVoxelLandMaterialSource::GetTexture(First, Channel);
		const FText Issue = FVoxelLandMaterialSource::ValidateTexture(Texture, nullptr, Channel);
		if (!Issue.IsEmpty())
		{
			OutError = FVoxelLandMaterialSource::GetError(First, Texture, 0, Channel, Issue);
			return false;
		}
		const int64 SliceBytes = Texture->Source.CalcMipSize(0, 0, FVoxelLandMaterialSource::SourceMip);
		if (!TryAddSourceBytes(SliceBytes, Palette.Surfaces.Num(), TotalSourceBytes))
		{
			OutError = FText::Format(
				LOCTEXT("SourceBudget",
					"Palette {0} exceeds the {1} MiB combined uncompressed source limit when adding its {2} array. Reduce texture resolution or remove surfaces, then bake again."),
				FText::FromString(Palette.GetPathName()), FText::AsNumber(MaxStagedSourceBytes / BytesPerMiB),
				FText::FromName(FVoxelLandMaterialSource::GetArrayName(Channel)));
			return false;
		}
	}
	return true;
}

UTexture2DArray* FVoxelLandMaterialArrayBaker::Build(
	UVoxelLandMaterialPalette& Palette, const EVoxelLandMaterialTextureChannel Channel, FText& OutError)
{
	TArray<UTexture2D*, TInlineAllocator<UVoxelLandMaterialPalette::MaxSlots>> Textures;
	for (int32 Index = 0; Index < Palette.Surfaces.Num(); ++Index)
	{
		const UVoxelLandSurfaceMaterial& Surface = *Palette.Surfaces[Index];
		UTexture2D* Texture = FVoxelLandMaterialSource::GetTexture(Surface, Channel);
		const FText Issue =
			FVoxelLandMaterialSource::ValidateTexture(Texture, Textures.IsEmpty() ? nullptr : Textures[0], Channel);
		if (!Issue.IsEmpty())
		{
			OutError = FVoxelLandMaterialSource::GetError(Surface, Texture, Index, Channel, Issue);
			return nullptr;
		}
		Textures.Add(Texture);
	}

	UTexture2DArray* Array = FVoxelLandMaterialSource::PrepareArray(Palette, *Textures[0], Channel);
	const int64 SliceSize = Textures[0]->Source.CalcMipSize(0, 0, FVoxelLandMaterialSource::SourceMip);
	{
		const FTextureSource::FMipLock TargetMip(
			FTextureSource::ELockState::ReadWrite, &Array->Source, FVoxelLandMaterialSource::SourceMip);
		if (!TargetMip.IsValid())
		{
			OutError = LOCTEXT("UnreadableDestination", "Unable to allocate the texture array source mip.");
			return nullptr;
		}
		uint8* ArrayBytes = static_cast<uint8*>(TargetMip.GetMutableData());
		for (int32 Index = 0; Index < Textures.Num(); ++Index)
		{
			const FText Issue = FVoxelLandMaterialSource::CopySlice(
				*Textures[Index], *Palette.Surfaces[Index], Channel, ArrayBytes + SliceSize * Index, SliceSize);
			if (!Issue.IsEmpty())
			{
				OutError = FVoxelLandMaterialSource::GetError(
					*Palette.Surfaces[Index], Textures[Index], Index, Channel, Issue);
				return nullptr;
			}
		}
	}
	Array->PostEditChange();
	return Array;
}

#undef LOCTEXT_NAMESPACE
