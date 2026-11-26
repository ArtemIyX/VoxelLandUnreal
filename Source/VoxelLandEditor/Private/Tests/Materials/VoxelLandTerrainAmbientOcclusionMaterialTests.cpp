#include "Materials/VoxelLandTerrainMaterialBuilder.h"
#include "Rendering/Components/VoxelLandChunkComponent.h"
#include "Rendering/Materials/VoxelLandTerrainMaterial.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandAmbientVisibilityUploadValidationTest, "VoxelLand.Lighting.UploadValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelLandAmbientVisibilityUploadValidationTest::RunTest(const FString& Parameters)
{
	UVoxelLandChunkComponent* const Component = NewObject<UVoxelLandChunkComponent>();
	const TSharedRef<FVoxelLandMeshData, ESPMode::ThreadSafe> Valid =
		MakeShared<FVoxelLandMeshData, ESPMode::ThreadSafe>();
	Valid->Vertices.SetNum(3);
	Valid->Vertices[0].Position = FVector3f::ZeroVector;
	Valid->Vertices[1].Position = FVector3f::UnitX();
	Valid->Vertices[2].Position = FVector3f::UnitY();
	Valid->Indices = {0, 1, 2};
	Valid->Bounds = FBox3f(FVector3f::ZeroVector, FVector3f(1.0f));
	Valid->AmbientVisibility = {255, 128, 255};
	if (!TestTrue(TEXT("Accept matching visibility payload"), Component->SetMesh(Valid)))
	{
		return false;
	}
	const FVector ResidentExtent = Component->CalcBounds(FTransform::Identity).BoxExtent;
	const TSharedRef<FVoxelLandMeshData, ESPMode::ThreadSafe> Invalid =
		MakeShared<FVoxelLandMeshData, ESPMode::ThreadSafe>(*Valid);
	Invalid->AmbientVisibility.Pop();
	Invalid->Bounds = FBox3f(FVector3f(-10.0f), FVector3f(10.0f));
	TestFalse(TEXT("Reject malformed visibility payload"), Component->SetMesh(Invalid));
	TestEqual(TEXT("Rejection keeps the resident bounds"), Component->CalcBounds(FTransform::Identity).BoxExtent,
		ResidentExtent);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandTerrainAmbientOcclusionMaterialTest, "VoxelLand.Lighting.MaterialTransport",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelLandTerrainAmbientOcclusionMaterialTest::RunTest(const FString& Parameters)
{
	UMaterial* Material = FVoxelLandTerrainMaterialBuilder::LoadSharedMaster();
	if (!TestNotNull(TEXT("Shipped material loads"), Material))
	{
		return false;
	}

	UPackage* Package = Material->GetOutermost();
	const bool bRebuild = FParse::Param(FCommandLine::Get(), TEXT("VoxelLandRebuildTerrainMaster"));
	bool bUpdated = false;
	if (bRebuild
		&& !TestEqual(TEXT("Updating the graph preserves the shared material object"),
			FVoxelLandTerrainMaterialBuilder::UpdateSharedMasterAmbientOcclusion(bUpdated), Material))
	{
		return false;
	}

	const UMaterialEditorOnlyData* Data = Material->GetEditorOnlyData();
	const UMaterialExpressionCustom* Custom =
		Data ? Cast<UMaterialExpressionCustom>(Data->AmbientOcclusion.Expression) : nullptr;
	if (!TestNotNull(TEXT("Material AO is connected to the terrain custom expression"), Custom))
	{
		return false;
	}

	TestEqual(TEXT("Material AO uses the terrain shader AO output"), Data->AmbientOcclusion.OutputIndex, 2);
	TestTrue(TEXT("The shader call consumes interpolated visibility and the strength scalar"),
		Custom->Code.Contains(TEXT("MeshUV5.x, AmbientOcclusionStrength"), ESearchCase::CaseSensitive));

	const FCustomInput* Visibility =
		Custom->Inputs.FindByPredicate([](const FCustomInput& Input) { return Input.InputName == TEXT("MeshUV5"); });
	const UMaterialExpressionTextureCoordinate* UV =
		Visibility ? Cast<UMaterialExpressionTextureCoordinate>(Visibility->Input.Expression) : nullptr;
	if (TestNotNull(TEXT("Geometry AO reads a mesh texture coordinate"), UV))
	{
		TestEqual(TEXT("Geometry AO uses the reserved UV channel"), UV->CoordinateIndex,
			FVoxelLandTerrainMaterial::AmbientOcclusionUVIndex);
	}

	const FCustomInput* Strength = Custom->Inputs.FindByPredicate(
		[](const FCustomInput& Input) { return Input.InputName == TEXT("AmbientOcclusionStrength"); });
	const UMaterialExpressionScalarParameter* StrengthParameter =
		Strength ? Cast<UMaterialExpressionScalarParameter>(Strength->Input.Expression) : nullptr;
	if (TestNotNull(TEXT("Ambient occlusion strength is a material scalar"), StrengthParameter))
	{
		TestEqual(TEXT("Strength uses the runtime parameter name"), StrengthParameter->ParameterName,
			FVoxelLandTerrainMaterial::AmbientOcclusionStrengthParameter);
		TestEqual(TEXT("Default shader strength uses full baked visibility"), StrengthParameter->DefaultValue, 1.0f);
	}

	TArray<FMaterialParameterInfo> TextureParameters;
	TArray<FGuid> TextureParameterIds;
	Material->GetAllTextureParameterInfo(TextureParameters, TextureParameterIds);
	TestEqual(TEXT("Shipped material still only binds three terrain texture arrays"), TextureParameters.Num(), 3);

	if (bUpdated && !HasAnyErrors())
	{
		const FString Filename =
			FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		return TestTrue(TEXT("Save the updated shared master in place"),
			UPackage::SavePackage(Package, Material, *Filename, SaveArgs));
	}
	return !HasAnyErrors();
}

#endif
