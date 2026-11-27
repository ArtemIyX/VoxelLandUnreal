#include "Rendering/Materials/VoxelLandTerrainPresentationResolver.h"
#include "Graph/Compilation/VoxelLandMaterialSignalBindings.h"

#include "Data/Assets/Graphs/VoxelLandTerrainGraph.h"
#include "Data/Assets/Materials/VoxelLandMaterialPalette.h"
#include "Data/Terrain/Worlds/VoxelLandWorldConfig.h"
#include "Graph/Compilation/VoxelLandMaterialGraphProgram.h"
#include "Graph/Serialization/VoxelLandProgramCodec.h"
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

TArray<uint8> FVoxelLandTerrainPresentationResolver::CaptureIdentity(const UVoxelLandWorldConfig& Config)
{
	FCanonicalWriter Writer;
	Writer.Bytes = CaptureIdentity(Config.MaterialSetup);
	Writer.Float(Config.TerrainAmbientOcclusionStrength);
	Writer.String(Config.MaterialGraph.ToSoftObjectPath().ToString());
	if (!Config.MaterialGraph.IsNull())
	{
		Writer.String(LexToString(Config.Seed));
		Writer.U8(static_cast<uint8>(Config.WorldExtent));
		Writer.U32(Config.Overrides.Num());
		for (const FVoxelLandParameterOverride& Override : Config.Overrides)
		{
			Writer.String(Override.ParameterId.ToString());
			Writer.U8(static_cast<uint8>(Override.Type));
			Writer.Float(Override.FloatValue);
			Writer.U32(static_cast<uint32>(Override.IntValue));
			Writer.U8(Override.BoolValue);
		}
		FVoxelLandGraphSnapshot Terrain;
		FString Error;
		if (Config.Graph && Config.Graph->CaptureSnapshot(Terrain, Error))
		{
			Writer.String(Config.Graph->GetPathName());
			const uint64 Fingerprint = FVoxelLandProgramCodec::SourceFingerprint(Terrain);
			Writer.U32(static_cast<uint32>(Fingerprint));
			Writer.U32(static_cast<uint32>(Fingerprint >> 32));
		}
	}
	if (const UVoxelLandTerrainGraph* Graph = Config.MaterialGraph.Get())
	{
		FVoxelLandGraphSnapshot Snapshot;
		FString Error;
		if (Graph->CaptureSnapshot(Snapshot, Error))
		{
			const uint64 Fingerprint = FVoxelLandProgramCodec::SourceFingerprint(Snapshot);
			Writer.U32(static_cast<uint32>(Fingerprint));
			Writer.U32(static_cast<uint32>(Fingerprint >> 32));
			Writer.U8(static_cast<uint8>(Graph->GraphRole));
		}
	}
	return MoveTemp(Writer.Bytes);
}

FVoxelLandTerrainPresentation FVoxelLandTerrainPresentationResolver::Resolve(UObject* Outer,
	const UVoxelLandWorldConfig& Config,
	TSharedPtr<const FVoxelLandWorldSignalSnapshot, ESPMode::ThreadSafe> WorldSignals)
{
	const float Strength = FMath::IsFinite(Config.TerrainAmbientOcclusionStrength)
							 ? FMath::Clamp(Config.TerrainAmbientOcclusionStrength, 0.0f, 1.0f)
							 : 0.0f;
	FVoxelLandTerrainPresentation Result =
		ResolveSetup(Outer, Config.MaterialSetup, Config.MaterialGraph.IsNull(), Strength);
	if (Config.MaterialGraph.IsNull())
	{
		Result.SourceIdentity = CaptureIdentity(Config);
		return Result;
	}

	const TStrongObjectPtr<UVoxelLandTerrainGraph> Graph(Config.MaterialGraph.LoadSynchronous());
	const UVoxelLandMaterialSetup* Setup = Config.MaterialSetup.Get();
	const UVoxelLandMaterialPalette* Palette = Setup ? Setup->Palette.Get() : nullptr;
	Result.SourceIdentity = CaptureIdentity(Config);
	Result.bHasConfiguredPalette = true;
	FString Error;
	FVoxelLandGraphSnapshot Snapshot;
	TSharedPtr<const FVoxelLandMaterialGraphProgram, ESPMode::ThreadSafe> Program;
	TArray<FGuid, TInlineAllocator<FVoxelLandMaterialAppearance::MaxSlots>> Slots;
	if (Palette)
	{
		for (const FVoxelLandMaterialPaletteSlot& Slot : Palette->BakedSlots)
		{
			Slots.Add(Slot.SurfaceId);
		}
	}
	if (!Graph || Graph->GraphRole != EVoxelLandGraphRole::Material || !Palette || !Result.Appearance)
	{
		Error = TEXT("A Material Graph requires a material-role graph and a valid baked palette.");
	}
	else if (Graph->CaptureSnapshot(Snapshot, Error))
	{
		TSharedPtr<const IVoxelLandMaterialSignalSource, ESPMode::ThreadSafe> Bindings;
		if (FVoxelLandMaterialGraphProgram::Compile(Snapshot, Slots, Program, Error)
			&& FVoxelLandMaterialSignalBindings::Capture(Config, *Program, MoveTemp(WorldSignals), Bindings, Error))
		{
			TSharedRef<FVoxelLandMaterialAppearance, ESPMode::ThreadSafe> Appearance =
				MakeShared<FVoxelLandMaterialAppearance, ESPMode::ThreadSafe>(*Result.Appearance);
			Appearance->Program = MoveTemp(Program);
			Appearance->Signals = MoveTemp(Bindings);
			Appearance->Seed = static_cast<uint64>(Config.Seed);
			Result.Appearance = MoveTemp(Appearance);
			return Result;
		}
	}
	Result.Appearance.Reset();
	Result.Diagnostic = FString::Printf(TEXT("Material graph %s: %s"), *Config.MaterialGraph.ToString(), *Error);
	Result.Material.Reset(UMaterial::GetDefaultMaterial(MD_Surface));
	return Result;
}

FVoxelLandTerrainPresentation FVoxelLandTerrainPresentationResolver::Resolve(
	UObject* Outer, const TSoftObjectPtr<UVoxelLandMaterialSetup>& SetupPath)
{
	return ResolveSetup(Outer, SetupPath, true, 0.0f);
}

FVoxelLandTerrainPresentation FVoxelLandTerrainPresentationResolver::ResolveSetup(
	UObject* Outer, const TSoftObjectPtr<UVoxelLandMaterialSetup>& SetupPath, bool bUseRules, float OcclusionStrength)
{
	check(IsInGameThread());
	FVoxelLandTerrainPresentation Result;
	TStrongObjectPtr<UVoxelLandMaterialSetup> Setup(SetupPath.LoadSynchronous());
	if (Setup && !Setup->Palette.IsNull())
	{
		Result.bHasConfiguredPalette = true;
		const TSoftObjectPtr<UVoxelLandMaterialPalette> PalettePath = Setup->Palette;
		TStrongObjectPtr<UVoxelLandMaterialPalette> Palette(PalettePath.LoadSynchronous());
		const TArray<FVoxelLandMaterialRule> Rules = bUseRules ? Setup->Rules : TArray<FVoxelLandMaterialRule>();
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
				if (UMaterialInstanceDynamic* Material = FVoxelLandTerrainMaterial::CreateInstance(Outer, *Palette))
				{
					Material->SetScalarParameterValue(
						FVoxelLandTerrainMaterial::AmbientOcclusionStrengthParameter, OcclusionStrength);
					Result.Material.Reset(Material);
					Appearance->TerrainAmbientOcclusionStrength = OcclusionStrength;
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
