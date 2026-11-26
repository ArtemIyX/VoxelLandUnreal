#if WITH_DEV_AUTOMATION_TESTS

#include "Materials/VoxelLandMaterialPaletteBaker.h"
#include "Data/Assets/Materials/VoxelLandMaterialPalette.h"
#include "Data/Assets/Materials/VoxelLandMaterialSetup.h"
#include "Data/Assets/Materials/VoxelLandSurfaceMaterial.h"
#include "Data/Terrain/Worlds/VoxelLandWorldConfig.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Texture2D.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UObject/SavePackage.h"
#include "UObject/StrongObjectPtr.h"

class FVoxelLandTerrainMaterialExampleBuilder final
{
public:
	static constexpr const TCHAR* ExampleRoot = TEXT("/VoxelLand/Examples/TerrainMaterials/");
	static constexpr const TCHAR* CookRoot = TEXT("/Game/VoxelLandMaterialVerification/");
	static constexpr const TCHAR* MeadowName = TEXT("DA_Surface_Meadow");
	static constexpr const TCHAR* SlateName = TEXT("DA_Surface_Slate");
	static constexpr const TCHAR* PaletteName = TEXT("DA_Palette_MeadowSlate");
	static constexpr const TCHAR* SetupName = TEXT("DA_MaterialSetup_MeadowSlate");
	static constexpr const TCHAR* CookPaletteName = TEXT("DA_Palette_AllSlots");
	static constexpr const TCHAR* MountainPath = TEXT("/VoxelLand/Examples/WC_MountainBiome.WC_MountainBiome");
	static constexpr const TCHAR* RollingPath = TEXT("/VoxelLand/Examples/WC_RollingHills.WC_RollingHills");
	static constexpr const TCHAR* DensityPath = TEXT("/VoxelLand/Examples/DA_DirectDensityWorld.DA_DirectDensityWorld");
	static constexpr const TCHAR* InfiniteName = TEXT("WC_InfiniteMountainBiome");
	static constexpr const TCHAR* DensityName = TEXT("WC_DensityMaterials");
	static constexpr int32 TileSize = 64;

	static FString MakeObjectPath(const TCHAR* Root, const TCHAR* Name)
	{
		return FString(Root) + Name + TEXT(".") + Name;
	}

	template <typename AssetType> static AssetType* CreateAsset(const TCHAR* Root, const TCHAR* Name)
	{
		const FString PackagePath = FString(Root) + Name;
		return NewObject<AssetType>(
			CreatePackage(*PackagePath), FName(Name), RF_Public | RF_Standalone | RF_Transactional);
	}

	static bool Save(UObject& Asset, bool bNewAsset = true)
	{
		if (bNewAsset)
		{
			FAssetRegistryModule::AssetCreated(&Asset);
		}
		UPackage* Package = Asset.GetOutermost();
		Package->MarkPackageDirty();
		const FString Filename =
			FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
		const FString Directory = FPaths::GetPath(Filename);
		if (!IFileManager::Get().DirectoryExists(*Directory) && !IFileManager::Get().MakeDirectory(*Directory, true))
		{
			return false;
		}
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		return UPackage::SavePackage(Package, &Asset, *Filename, Args);
	}

	static bool HasExistingAssets(const TCHAR* Root)
	{
		TArray<FString> Existing;
		FPackageName::FindPackagesInDirectory(Existing, FPackageName::LongPackageNameToFilename(Root));
		return !Existing.IsEmpty();
	}

	static bool BuildExamples(FAutomationTestBase& Test)
	{
		if (HasExistingAssets(ExampleRoot))
		{
			Test.AddError(TEXT("Terrain material examples already exist. Existing editable assets were left intact."));
			return false;
		}
		TStrongObjectPtr<UVoxelLandWorldConfig> Mountain(LoadObject<UVoxelLandWorldConfig>(nullptr, MountainPath));
		TStrongObjectPtr<UVoxelLandWorldConfig> Density(LoadObject<UVoxelLandWorldConfig>(nullptr, DensityPath));
		if (!Mountain || !Density
			|| (!Mountain->MaterialSetup.IsNull()
				&& Mountain->MaterialSetup.ToSoftObjectPath()
					   != FSoftObjectPath(MakeObjectPath(ExampleRoot, SetupName))))
		{
			Test.AddError(
				TEXT("Example templates are missing or Mountain Biome already has an unrelated Material Setup."));
			return false;
		}

		UVoxelLandSurfaceMaterial* Meadow = CreateAsset<UVoxelLandSurfaceMaterial>(ExampleRoot, MeadowName);
		UVoxelLandSurfaceMaterial* Slate = CreateAsset<UVoxelLandSurfaceMaterial>(ExampleRoot, SlateName);
		TArray<UObject*> Textures = BuildExampleSurface(*Meadow, true);
		Textures.Append(BuildExampleSurface(*Slate, false));
		UVoxelLandMaterialPalette* Palette = CreateAsset<UVoxelLandMaterialPalette>(ExampleRoot, PaletteName);
		Palette->Surfaces = {Meadow, Slate};
		FText Error;
		if (!FVoxelLandMaterialPaletteBaker::Bake(*Palette, Error))
		{
			Test.AddError(Error.ToString());
			return false;
		}

		UVoxelLandMaterialSetup* Setup = CreateAsset<UVoxelLandMaterialSetup>(ExampleRoot, SetupName);
		Setup->Palette = Palette;
		AddRules(*Setup, *Slate);
		for (UObject* Asset : Textures)
		{
			if (!Save(*Asset))
			{
				Test.AddError(FString::Printf(TEXT("Could not save %s."), *Asset->GetPathName()));
				return false;
			}
		}
		if (!Save(*Meadow) || !Save(*Slate) || !Save(*Palette) || !Save(*Setup))
		{
			Test.AddError(TEXT("Could not persist both surfaces, the baked palette and its setup."));
			return false;
		}
		UVoxelLandWorldConfig* Infinite = DuplicateConfig(*Mountain, InfiniteName);
		Infinite->WorldExtent = EVoxelLandWorldExtent::Infinite;
		Infinite->bGlobalErosion = false;
		Infinite->MaterialSetup = Setup;
		UVoxelLandWorldConfig* DensityExample = DuplicateConfig(*Density, DensityName);
		DensityExample->MaterialSetup = Setup;
		if (!Save(*Infinite) || !Save(*DensityExample))
		{
			Test.AddError(TEXT("Could not save terrain materials demo configurations."));
			return false;
		}
		Mountain->Modify();
		Mountain->MaterialSetup = Setup;
		return Test.TestTrue(
			TEXT("Assign and save Material Setup on the shipped Mountain Biome"), Save(*Mountain, false));
	}

	static void PopulateAllSlots(UVoxelLandMaterialPalette& Palette)
	{
		UTexture2D* Color = NewObject<UTexture2D>(&Palette, TEXT("Albedo"));
		UTexture2D* Normal = NewObject<UTexture2D>(&Palette, TEXT("Normal"));
		UTexture2D* ORMH = NewObject<UTexture2D>(&Palette, TEXT("ORMH"));
		BuildTextures(*Color, *Normal, *ORMH, false);
		Palette.Surfaces.Reserve(UVoxelLandMaterialPalette::MaxSlots);
		for (int32 Index = 0; Index < UVoxelLandMaterialPalette::MaxSlots; ++Index)
		{
			const FName Name(*FString::Printf(TEXT("Surface_%02d"), Index));
			UVoxelLandSurfaceMaterial* Surface = NewObject<UVoxelLandSurfaceMaterial>(&Palette, Name);
			Surface->SurfaceId = FGuid(0x564c4d50, 0, 0, Index + 1);
			Surface->BaseColor = Color;
			Surface->Normal = Normal;
			Surface->ORMH = ORMH;
			Surface->TilePeriodCm = 192.0 + 17.0 * Index;
			Surface->Tint = FLinearColor(1.f - 0.01f * Index, 0.55f + 0.01f * Index, 1.f);
			Palette.Surfaces.Add(Surface);
		}
	}

private:
	static UVoxelLandWorldConfig* DuplicateConfig(UVoxelLandWorldConfig& Src, const TCHAR* Name)
	{
		const FString PackageName = FString(ExampleRoot) + Name;
		UVoxelLandWorldConfig* Config =
			DuplicateObject<UVoxelLandWorldConfig>(&Src, CreatePackage(*PackageName), FName(Name));
		Config->SetFlags(RF_Public | RF_Standalone | RF_Transactional);
		return Config;
	}

	static void AddRules(UVoxelLandMaterialSetup& Setup, UVoxelLandSurfaceMaterial& Slate)
	{
		FVoxelLandMaterialRule& HighRock = Setup.Rules.AddDefaulted_GetRef();
		HighRock.Surface = &Slate;
		HighRock.SurfaceId = Slate.SurfaceId;
		HighRock.Input = EVoxelLandMaterialRuleInput::ElevationCm;
		HighRock.Threshold = 2300.0;
		HighRock.BlendWidth = 1400.0;
		FVoxelLandMaterialRule& SteepRock = Setup.Rules.AddDefaulted_GetRef();
		SteepRock.Surface = &Slate;
		SteepRock.SurfaceId = Slate.SurfaceId;
		SteepRock.Input = EVoxelLandMaterialRuleInput::SlopeDegrees;
		SteepRock.Threshold = 34.0;
		SteepRock.BlendWidth = 20.0;
	}

	static TArray<UObject*> BuildExampleSurface(UVoxelLandSurfaceMaterial& Surface, bool bIsMeadow)
	{
		const TCHAR* Variety = bIsMeadow ? TEXT("Meadow") : TEXT("Slate");
		UTexture2D* Color = CreateAsset<UTexture2D>(ExampleRoot, *FString::Printf(TEXT("T_%s_Albedo"), Variety));
		UTexture2D* Normal = CreateAsset<UTexture2D>(ExampleRoot, *FString::Printf(TEXT("T_%s_Normal"), Variety));
		UTexture2D* ORMH = CreateAsset<UTexture2D>(ExampleRoot, *FString::Printf(TEXT("T_%s_ORMH"), Variety));
		BuildTextures(*Color, *Normal, *ORMH, bIsMeadow);
		Surface.BaseColor = Color;
		Surface.Normal = Normal;
		Surface.ORMH = ORMH;
		Surface.TilePeriodCm = bIsMeadow ? 317.0 : 541.0;
		Surface.HeightBlendContrast = bIsMeadow ? 0.28f : 0.46f;
		return {Color, Normal, ORMH};
	}

	static void BuildTextures(UTexture2D& Color, UTexture2D& Normal, UTexture2D& ORMH, bool bIsMeadow)
	{
		TArray<FColor> AlbedoPixels;
		TArray<FColor> NormalPixels;
		TArray<FColor> PackedPixels;
		constexpr int32 Texels = TileSize * TileSize;
		AlbedoPixels.Reserve(Texels);
		NormalPixels.Reserve(Texels);
		PackedPixels.Reserve(Texels);
		for (int32 Y = 0; Y < TileSize; ++Y)
		{
			for (int32 X = 0; X < TileSize; ++X)
			{
				const float U = UE_TWO_PI * static_cast<float>(X) / TileSize;
				const float V = UE_TWO_PI * static_cast<float>(Y) / TileSize;
				const float Wave = bIsMeadow ? 5.0f * U + 3.0f * V : 2.0f * U - 7.0f * V;
				const float Grain = FMath::Sin(Wave) * 0.62f + FMath::Sin(13.0f * U - 11.0f * V) * 0.38f;
				const int32 Mark = FMath::RoundToInt(Grain * 12.0f);
				const FColor Albedo =
					bIsMeadow ? FColor(69 + Mark, 112 + Mark, 49 + Mark) : FColor(92 + Mark, 98 + Mark, 127 + Mark);
				const int32 NormalOffset = FMath::RoundToInt(FMath::Cos(Wave) * 13.0f);
				AlbedoPixels.Add(Albedo);
				NormalPixels.Emplace(128 + NormalOffset, 128 - NormalOffset, 254);
				PackedPixels.Emplace(234 + Mark, (bIsMeadow ? 205 : 159) + Mark, 0, 116 + 4 * Mark);
			}
		}
		InitTexture(Color, AlbedoPixels, true, TC_BC7);
		InitTexture(Normal, NormalPixels, false, TC_Normalmap);
		InitTexture(ORMH, PackedPixels, false, TC_BC7);
	}

	static void InitTexture(
		UTexture2D& Texture, const TArray<FColor>& Pixels, bool bIsSrgb, TextureCompressionSettings Compression)
	{
		Texture.SRGB = bIsSrgb;
		Texture.AddressX = TA_Wrap;
		Texture.AddressY = TA_Wrap;
		Texture.MipGenSettings = TMGS_SimpleAverage;
		Texture.CompressionSettings = Compression;
		Texture.Source.Init(TileSize, TileSize, 1, 1, TSF_BGRA8, reinterpret_cast<const uint8*>(Pixels.GetData()));
		Texture.PostEditChange();
	}
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandCreateTerrainMaterialExamples, "VoxelLand.Materials.CreateExamples",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVoxelLandCreateTerrainMaterialExamples::RunTest(const FString& Parameters)
{
	if (!FParse::Param(FCommandLine::Get(), TEXT("VoxelLandCreateTerrainMaterialExamples")))
	{
		return true;
	}
	return FVoxelLandTerrainMaterialExampleBuilder::BuildExamples(*this);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandTerrainMaterialExamplesTest, "VoxelLand.Materials.Examples",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVoxelLandTerrainMaterialExamplesTest::RunTest(const FString& Parameters)
{
	using FBuilder = FVoxelLandTerrainMaterialExampleBuilder;
	const FString SetupPath = FBuilder::MakeObjectPath(FBuilder::ExampleRoot, FBuilder::SetupName);
	const UVoxelLandMaterialSetup* Setup = LoadObject<UVoxelLandMaterialSetup>(nullptr, *SetupPath);
	const UVoxelLandWorldConfig* Mountain = LoadObject<UVoxelLandWorldConfig>(nullptr, FBuilder::MountainPath);
	const UVoxelLandWorldConfig* Rolling = LoadObject<UVoxelLandWorldConfig>(nullptr, FBuilder::RollingPath);
	if (!TestNotNull(TEXT("Example Material Setup loads"), Setup)
		|| !TestNotNull(TEXT("Mountain Biome loads"), Mountain) || !TestNotNull(TEXT("Rolling Hills loads"), Rolling))
	{
		return false;
	}
	const UVoxelLandMaterialPalette* Palette = Setup->Palette.LoadSynchronous();
	FText Error;
	if (!TestNotNull(TEXT("Example bake loads"), Palette)
		|| !TestTrue(TEXT("Example bake validates"), Palette->ValidateBake(Error)))
	{
		AddError(Error.ToString());
		return false;
	}
	TestFalse(TEXT("Example bake is current"), Palette->IsBakeStale());
	TestEqual(TEXT("Example contains two independently editable surfaces"), Palette->Surfaces.Num(), 2);
	TestEqual(TEXT("Example contains height and slope rules"), Setup->Rules.Num(), 2);
	TestEqual(TEXT("Mountain Biome points to the example setup"), Mountain->MaterialSetup.ToSoftObjectPath(),
		FSoftObjectPath(SetupPath));
	TestNotEqual(TEXT("Rolling Hills remains a compatibility control"), Rolling->MaterialSetup.ToSoftObjectPath(),
		FSoftObjectPath(SetupPath));

	const FString InfinitePath = FBuilder::MakeObjectPath(FBuilder::ExampleRoot, FBuilder::InfiniteName);
	const FString DensityPath = FBuilder::MakeObjectPath(FBuilder::ExampleRoot, FBuilder::DensityName);
	const UVoxelLandWorldConfig* Infinite = LoadObject<UVoxelLandWorldConfig>(nullptr, *InfinitePath);
	const UVoxelLandWorldConfig* Density = LoadObject<UVoxelLandWorldConfig>(nullptr, *DensityPath);
	if (!TestNotNull(TEXT("Infinite Height example loads"), Infinite)
		|| !TestNotNull(TEXT("Direct Density example loads"), Density))
	{
		return false;
	}
	TestEqual(TEXT("Height example streams as Infinite"), Infinite->WorldExtent, EVoxelLandWorldExtent::Infinite);
	TestEqual(TEXT("Density example retains the Direct Density source"), Density->TerrainSource,
		EVoxelLandTerrainSource::DirectDensity);
	TestEqual(TEXT("Density example shares the same material setup"), Density->MaterialSetup.ToSoftObjectPath(),
		FSoftObjectPath(SetupPath));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandTerrainMaterialAllSlotsTest, "VoxelLand.Materials.AllSlots",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVoxelLandTerrainMaterialAllSlotsTest::RunTest(const FString& Parameters)
{
	using FBuilder = FVoxelLandTerrainMaterialExampleBuilder;
	const bool bSave = FParse::Param(FCommandLine::Get(), TEXT("VoxelLandCreateTerrainMaterialCookFixture"));
	if (bSave && FBuilder::HasExistingAssets(FBuilder::CookRoot))
	{
		AddError(TEXT("The cook fixture already exists. Existing files were left intact."));
		return false;
	}
	TStrongObjectPtr<UVoxelLandMaterialPalette> Palette(
		bSave ? FBuilder::CreateAsset<UVoxelLandMaterialPalette>(FBuilder::CookRoot, FBuilder::CookPaletteName)
			  : NewObject<UVoxelLandMaterialPalette>(GetTransientPackage(), NAME_None, RF_Transient));
	FBuilder::PopulateAllSlots(*Palette);
	FText Error;
	if (!FVoxelLandMaterialPaletteBaker::Bake(*Palette, Error) || !Palette->ValidateBake(Error))
	{
		AddError(Error.ToString());
		return false;
	}
	TestEqual(TEXT("All 32 palette entries are baked"), Palette->BakedSlots.Num(), UVoxelLandMaterialPalette::MaxSlots);
	TestEqual(TEXT("Final slot resolves to its own source surface"), Palette->BakedSlots.Last().SurfaceId,
		Palette->Surfaces.Last()->SurfaceId);
	if (bSave)
	{
		TestTrue(TEXT("Save the opt-in 32-slot cook fixture"), FBuilder::Save(*Palette));
	}
	return true;
}

#endif
