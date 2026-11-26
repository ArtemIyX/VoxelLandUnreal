#if WITH_DEV_AUTOMATION_TESTS

#include "Data/Assets/Materials/VoxelLandMaterialPalette.h"
#include "Engine/Texture2DArray.h"
#include "Materials/MaterialInterface.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "RHI.h"
#include "TextureResource.h"
#include "UObject/UObjectGlobals.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandTerrainMaterialCookedProbe, "VoxelLand.Materials.CookedProbe",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelLandTerrainMaterialCookedProbe::RunTest(const FString& Parameters)
{
	FString Root;
	if (!FParse::Value(FCommandLine::Get(), TEXT("VoxelLandCookedMaterialRoot="), Root))
	{
		AddInfo(TEXT("Supply -VoxelLandCookedMaterialRoot=<cooked project directory> to run this probe."));
		return true;
	}
	if (!GUsingNullRHI)
	{
		AddError(TEXT("CookedProbe inspects cooked data with -nullrhi. Run RenderSeam separately for DX12 rendering."));
		return false;
	}
	Root = FPaths::ConvertRelativePathToFull(Root);
	FPaths::NormalizeDirectoryName(Root);
	const FString CookedGame = Root / TEXT("Content/");
	const FString CookedPlugin = Root / TEXT("Plugins/VoxelLand/Content/");
	if (!FPaths::DirectoryExists(CookedGame) || !FPaths::DirectoryExists(CookedPlugin))
	{
		AddError(FString::Printf(TEXT("Cooked project or plugin content is missing under %s."), *Root));
		return false;
	}
	FPackageName::RegisterMountPoint(TEXT("/Game/"), CookedGame);
	FPackageName::RegisterMountPoint(TEXT("/VoxelLand/"), CookedPlugin);
	ON_SCOPE_EXIT
	{
		FPackageName::UnRegisterMountPoint(TEXT("/VoxelLand/"), CookedPlugin);
		FPackageName::UnRegisterMountPoint(TEXT("/Game/"), CookedGame);
	};
	TGuardValue<int32> AllowCooked(GAllowCookedDataInEditorBuilds, 1);

	const UVoxelLandMaterialPalette* Palette = LoadObject<UVoxelLandMaterialPalette>(
		nullptr, TEXT("/Game/VoxelLandMaterialVerification/DA_Palette_AllSlots.DA_Palette_AllSlots"));
	const UVoxelLandMaterialPalette* Demo = LoadObject<UVoxelLandMaterialPalette>(
		nullptr, TEXT("/VoxelLand/Examples/TerrainMaterials/DA_Palette_MeadowSlate.DA_Palette_MeadowSlate"));
	if (!TestNotNull(TEXT("Cooked 32-slot palette loads"), Palette)
		|| !TestNotNull(TEXT("Cooked example palette loads"), Demo))
	{
		return false;
	}
	TestTrue(TEXT("Both palettes load from cooked packages"),
		Palette->GetOutermost()->HasAnyPackageFlags(PKG_FilterEditorOnly)
			&& Demo->GetOutermost()->HasAnyPackageFlags(PKG_FilterEditorOnly));
	TestTrue(TEXT("Cook strips both palettes' source surface definitions"),
		Palette->Surfaces.IsEmpty() && Demo->Surfaces.IsEmpty());
	TestEqual(TEXT("All 32 slots survive cook"), Palette->BakedSlots.Num(), UVoxelLandMaterialPalette::MaxSlots);
	FText Error;
	if (!Palette->ValidateBake(Error) || !Demo->ValidateBake(Error))
	{
		AddError(FString::Printf(TEXT("Cooked palette validation failed. %s"), *Error.ToString()));
		return false;
	}
	TestTrue(TEXT("The fixture and example share one cooked terrain master"),
		Palette->MasterMaterial.Get() == Demo->MasterMaterial.Get()
			&& Palette->MasterMaterial->GetOutermost()->HasAnyPackageFlags(PKG_FilterEditorOnly));

	const UTexture2DArray* Arrays[] = {Palette->BaseColorArray, Palette->NormalArray, Palette->ORMHArray};
	const EPixelFormat Formats[] = {PF_BC7, PF_BC5, PF_BC7};
	uint64 AllBytes = 0;
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Arrays); ++Index)
	{
		const UTexture2DArray& Array = *Arrays[Index];
		TestFalse(TEXT("Cook strips texture array source art"), Array.Source.IsValid());
		TestEqual(
			TEXT("Cooked texture keeps all array slices"), Array.GetArraySize(), UVoxelLandMaterialPalette::MaxSlots);
		TestTrue(TEXT("Cooked texture keeps its mip chain"), Array.GetNumMips() > 1);
		TestEqual(TEXT("Cooked texture preserves its compressed Windows pixel format"), Array.GetPixelFormat(),
			Formats[Index]);
		uint64 Bytes = 0;
		if (const FTexturePlatformData* Platform = Array.GetPlatformData())
		{
			for (const FTexture2DMipMap& Mip : Platform->Mips)
			{
				Bytes += static_cast<uint64>(Mip.BulkData.GetBulkDataSize());
			}
		}
		AllBytes += Bytes;
		AddInfo(FString::Printf(TEXT("%s cooked texture is %d x %d, %d slices, %d mips, %llu mip payload bytes."),
			*Array.GetName(), Array.GetSizeX(), Array.GetSizeY(), Array.GetArraySize(), Array.GetNumMips(), Bytes));
	}
	AddInfo(FString::Printf(TEXT("Cooked 32-slot palette contains %llu bytes of compressed mip payload."), AllBytes));
	TestTrue(TEXT("Cooked palette contains texture mip payloads"), AllBytes > 0);
	return true;
}

#endif
