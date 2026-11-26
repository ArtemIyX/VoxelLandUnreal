#include "Rendering/Materials/VoxelLandTerrainPresentationResolver.h"

#include "Data/Assets/Materials/VoxelLandMaterialPalette.h"
#include "Data/Assets/Materials/VoxelLandMaterialSetup.h"
#include "Data/Meshing/VoxelLandMaterialAppearance.h"
#include "Data/Program/Serialization/VoxelLandCanonicalWriter.h"
#include "Rendering/Materials/VoxelLandTerrainMaterial.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"

TArray<uint8> FVoxelLandTerrainPresentationResolver::CaptureIdentity(
	const TSoftObjectPtr<UVoxelLandMaterialSetup>& SetupPath)
{
	check(IsInGameThread());
	FCanonicalWriter Writer;
	Writer.String(SetupPath.ToSoftObjectPath().ToString());
	const UVoxelLandMaterialSetup* Setup = SetupPath.Get();
	Writer.U8(Setup != nullptr);
	if (!Setup)
	{
		return MoveTemp(Writer.Bytes);
	}
	Writer.U32(Setup->FormatVersion);
	Writer.String(Setup->Material.ToSoftObjectPath().ToString());
	Writer.String(Setup->Palette.ToSoftObjectPath().ToString());
	Writer.U32(Setup->Rules.Num());
	for (const FVoxelLandMaterialRule& Rule : Setup->Rules)
	{
		Writer.String(Rule.SurfaceId.ToString());
		Writer.U8(static_cast<uint8>(Rule.Input));
		for (const double Value : {Rule.Threshold, Rule.BlendWidth})
		{
			uint64 Bits;
			FMemory::Memcpy(&Bits, &Value, sizeof(Bits));
			Writer.U32(static_cast<uint32>(Bits));
			Writer.U32(static_cast<uint32>(Bits >> 32));
		}
	}
	if (const UVoxelLandMaterialPalette* Palette = Setup->Palette.Get())
	{
		Writer.String(Palette->LayoutIdentity.ToString());
		Writer.String(Palette->ShadingIdentity.ToString());
	}
	return MoveTemp(Writer.Bytes);
}

FVoxelLandTerrainPresentation FVoxelLandTerrainPresentationResolver::Resolve(
	UObject* Outer, const TSoftObjectPtr<UVoxelLandMaterialSetup>& SetupPath)
{
	check(IsInGameThread());
	FVoxelLandTerrainPresentation Result;
	TStrongObjectPtr<UVoxelLandMaterialSetup> Setup(SetupPath.LoadSynchronous());
	if (Setup && !Setup->Palette.IsNull())
	{
		Result.bHasConfiguredPalette = true;
		const TSoftObjectPtr<UVoxelLandMaterialPalette> PalettePath = Setup->Palette;
		TStrongObjectPtr<UVoxelLandMaterialPalette> Palette(PalettePath.LoadSynchronous());
		const TArray<FVoxelLandMaterialRule> Rules = Setup->Rules;
		Result.SourceIdentity = CaptureIdentity(SetupPath);
		FText Error;
		if (Setup->Palette != PalettePath || !Palette || !Palette->ValidateBake(Error))
		{
			Result.Diagnostic = FString::Printf(
				TEXT("Palette %s is unavailable or invalid. %s"), *PalettePath.ToString(), *Error.ToString());
		}
		else
		{
			TSharedRef<FVoxelLandMaterialAppearance, ESPMode::ThreadSafe> Appearance =
				MakeShared<FVoxelLandMaterialAppearance, ESPMode::ThreadSafe>();
			Appearance->SlotCount = static_cast<uint8>(Palette->BakedSlots.Num());
			Appearance->Rules.Reserve(Rules.Num());
			for (const FVoxelLandMaterialRule& Rule : Rules)
			{
				const int32 Slot = Palette->BakedSlots.IndexOfByPredicate(
					[&Rule](const FVoxelLandMaterialPaletteSlot& Entry) { return Entry.SurfaceId == Rule.SurfaceId; });
				const bool bIsElevation = Rule.Input == EVoxelLandMaterialRuleInput::ElevationCm;
				const bool bIsSlope = Rule.Input == EVoxelLandMaterialRuleInput::SlopeDegrees;
				if (Slot == INDEX_NONE || (!bIsElevation && !bIsSlope) || !FMath::IsFinite(Rule.Threshold)
					|| !FMath::IsFinite(Rule.BlendWidth) || Rule.BlendWidth < 0.0
					|| (bIsSlope && (Rule.Threshold < 0.0 || Rule.Threshold > 180.0 || Rule.BlendWidth > 180.0)))
				{
					Result.Diagnostic =
						FString::Printf(TEXT("Palette %s has an invalid appearance rule for surface %s."),
							*PalettePath.ToString(), *Rule.SurfaceId.ToString());
					break;
				}
				FVoxelLandResolvedMaterialRule& Resolved = Appearance->Rules.AddDefaulted_GetRef();
				Resolved.Slot = static_cast<uint8>(Slot);
				Resolved.Input = Rule.Input;
				Resolved.Threshold = Rule.Threshold;
				Resolved.BlendWidth = Rule.BlendWidth;
			}
			if (Result.Diagnostic.IsEmpty())
			{
				Result.Material.Reset(FVoxelLandTerrainMaterial::CreateInstance(Outer, *Palette));
				if (Result.Material)
				{
					Result.Appearance = Appearance;
					Result.LayoutIdentity = Palette->LayoutIdentity;
					return Result;
				}
				Result.Diagnostic =
					FString::Printf(TEXT("Palette %s could not create its terrain material."), *PalettePath.ToString());
			}
		}
	}

	const TSoftObjectPtr<UMaterialInterface> LegacyPath =
		Setup && Setup->FormatVersion == 1 ? Setup->Material : TSoftObjectPtr<UMaterialInterface>();
	UMaterialInterface* Legacy = LegacyPath.LoadSynchronous();
	if (Result.SourceIdentity.IsEmpty())
	{
		Result.SourceIdentity = CaptureIdentity(SetupPath);
	}
	if (Legacy && Legacy->GetMaterial() && Legacy->GetMaterial()->MaterialDomain == MD_Surface)
	{
		Result.Material.Reset(Legacy);
		return Result;
	}
	if (Result.Diagnostic.IsEmpty())
	{
		Result.Diagnostic = TEXT("Missing or unsupported material; using the default surface material.");
	}
	Result.Material.Reset(UMaterial::GetDefaultMaterial(MD_Surface));
	return Result;
}
