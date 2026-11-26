#if WITH_DEV_AUTOMATION_TESTS

#include "../../Graph/Toolkit/VoxelLandGraphToolkit.h"
#include "Data/Assets/Graphs/VoxelLandTerrainGraph.h"
#include "Data/Assets/Materials/VoxelLandMaterialPalette.h"
#include "Data/Assets/Materials/VoxelLandMaterialSetup.h"
#include "Data/Assets/Materials/VoxelLandSurfaceMaterial.h"
#include "Data/Terrain/Worlds/VoxelLandWorldConfig.h"
#include "Materials/VoxelLandMaterialPaletteBaker.h"
#include "Engine/Texture2D.h"
#include "HAL/PlatformProcess.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandTerrainPalettePreviewTest, "VoxelLand.Materials.Editor.TerrainPreview",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

class FVoxelLandTerrainPalettePreviewFixture final
{
public:
	static UVoxelLandSurfaceMaterial* CreateSurface(const FColor& Albedo)
	{
		UVoxelLandSurfaceMaterial* Surface = NewObject<UVoxelLandSurfaceMaterial>();
		Surface->BaseColor = CreateTexture(Albedo, true);
		Surface->Normal = CreateTexture(FColor(128, 128, 255), false);
		Surface->ORMH = CreateTexture(FColor(255, 128, 0, 64), false);
		return Surface;
	}

private:
	static UTexture2D* CreateTexture(const FColor& Color, bool bIsSRGB)
	{
		constexpr int32 Size = 4;
		TArray<FColor> Pixels;
		Pixels.Init(Color, Size * Size);
		UTexture2D* Texture = NewObject<UTexture2D>();
		Texture->Source.Init(Size, Size, 1, 1, TSF_BGRA8, reinterpret_cast<const uint8*>(Pixels.GetData()));
		Texture->SRGB = bIsSRGB;
		Texture->AddressX = TA_Wrap;
		Texture->AddressY = TA_Wrap;
		return Texture;
	}
};

bool FVoxelLandTerrainPalettePreviewTest::RunTest(const FString& Parameters)
{
	const UVoxelLandWorldConfig* Example =
		LoadObject<UVoxelLandWorldConfig>(nullptr, TEXT("/VoxelLand/Examples/WC_RollingHills.WC_RollingHills"));
	if (!TestNotNull(TEXT("Editable hills config"), Example)
		|| !TestNotNull(TEXT("Editable hills graph"), Example->Graph.Get()))
	{
		return false;
	}
	const TStrongObjectPtr<UVoxelLandWorldConfig> Config(DuplicateObject(Example, GetTransientPackage()));
	const TStrongObjectPtr<UVoxelLandMaterialPalette> Palette(NewObject<UVoxelLandMaterialPalette>());
	const TStrongObjectPtr<UVoxelLandMaterialSetup> Setup(NewObject<UVoxelLandMaterialSetup>());
	Palette->Surfaces.Add(FVoxelLandTerrainPalettePreviewFixture::CreateSurface(FColor::Green));
	Palette->Surfaces.Add(FVoxelLandTerrainPalettePreviewFixture::CreateSurface(FColor::Red));
	FText BakeError;
	if (!FVoxelLandMaterialPaletteBaker::Bake(*Palette, BakeError))
	{
		AddError(BakeError.ToString());
		return false;
	}
	Setup->Palette = Palette.Get();
	FVoxelLandMaterialRule& Rule = Setup->Rules.AddDefaulted_GetRef();
	Rule.SurfaceId = Palette->Surfaces[1]->SurfaceId;
	Rule.Input = EVoxelLandMaterialRuleInput::ElevationCm;
	Rule.BlendWidth = 1000.0;
	Config->MaterialSetup = Setup.Get();
	Config->HorizontalRadius = 1;
	Config->VerticalRadius = 1;

	const TSharedRef<FVoxelLandGraphToolkit> Toolkit = MakeShared<FVoxelLandGraphToolkit>();
	Toolkit->InitializeForTests(Config->Graph);
	Toolkit->InitializePreviewForTests();
	Toolkit->StartM4PreviewForTests(Config.Get());
	const double Deadline = FPlatformTime::Seconds() + 10.0;
	while (Toolkit->HasM4PreviewWorkForTests() && FPlatformTime::Seconds() < Deadline)
	{
		Toolkit->PollJobsForTests();
		FPlatformProcess::Sleep(0.005f);
	}
	TestEqual(*Toolkit->GetStatusForTests(), Toolkit->GetPreviewPublishCountForTests(), 1);
	TestTrue(TEXT("Terrain published the resolved mesh appearance"), Toolkit->HasPublishedAppearanceForTests());
	TestNotNull(TEXT("Terrain bound its palette material instance"),
		Cast<UMaterialInstanceDynamic>(Toolkit->GetEffectiveMaterialForTests()));
	TestTrue(TEXT("Terrain produced visible geometry"), Toolkit->GetPreviewBoundsForTests().IsValid != 0);

	const TStrongObjectPtr<UMaterialInterface> FirstMaterial(Toolkit->GetEffectiveMaterialForTests());
	const int32 FirstStartCount = Toolkit->GetPreviewStartCountForTests();
	Palette->Surfaces[0]->Tint = FLinearColor::Gray;
	if (!FVoxelLandMaterialPaletteBaker::Bake(*Palette, BakeError))
	{
		AddError(BakeError.ToString());
		Toolkit->Stop();
		return false;
	}
	TestTrue(TEXT("Shading-only rebake replaced the preview material"),
		Toolkit->GetEffectiveMaterialForTests() != FirstMaterial.Get());
	TestEqual(TEXT("Compatible shading edit reused the existing mesh"), Toolkit->GetPreviewStartCountForTests(),
		FirstStartCount);

	const TStrongObjectPtr<UMaterialInterface> UpdatedMaterial(Toolkit->GetEffectiveMaterialForTests());
	const TStrongObjectPtr<UVoxelLandMaterialPalette> Unbaked(NewObject<UVoxelLandMaterialPalette>());
	Setup->Palette = Unbaked.Get();
	Toolkit->StartM4PreviewForTests(Config.Get());
	TestTrue(TEXT("Invalid palette reports that the previous preview is retained"),
		Toolkit->GetStatusForTests().Contains(TEXT("previous preview is retained")));
	TestEqual(TEXT("Invalid palette published no replacement mesh"), Toolkit->GetPreviewPublishCountForTests(), 1);
	TestTrue(TEXT("Invalid palette preserved the previous material"),
		Toolkit->GetEffectiveMaterialForTests() == UpdatedMaterial.Get());
	Toolkit->Stop();
	return true;
}

#endif
