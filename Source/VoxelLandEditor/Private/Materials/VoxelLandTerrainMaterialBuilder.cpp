#include "Materials/VoxelLandTerrainMaterialBuilder.h"

#include "Data/Assets/Materials/VoxelLandMaterialPalette.h"
#include "Rendering/Materials/VoxelLandTerrainMaterial.h"
#include "Engine/Texture2DArray.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionLocalPosition.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionTextureObjectParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionVertexNormalWS.h"
#include "Materials/MaterialExpressionWorldPosition.h"
#include "UObject/Package.h"

const TCHAR* const FVoxelLandTerrainMaterialBuilder::SharedMasterPackage =
	TEXT("/VoxelLand/Materials/M_VoxelLandTerrain");
const TCHAR* const FVoxelLandTerrainMaterialBuilder::SharedMasterPath =
	TEXT("/VoxelLand/Materials/M_VoxelLandTerrain.M_VoxelLandTerrain");

class FVoxelLandTerrainMaterialNodes
{
public:
	template <typename ExpressionType> static ExpressionType* Add(UMaterial& Material)
	{
		ExpressionType* Expression = NewObject<ExpressionType>(&Material, NAME_None, RF_Transactional);
		Expression->Material = &Material;
		Material.GetExpressionCollection().AddExpression(Expression);
		return Expression;
	}

	static UMaterialExpressionTextureObjectParameter* AddArray(
		UMaterial& Material, FName Parameter, UTexture2DArray* Array, EMaterialSamplerType Type)
	{
		UMaterialExpressionTextureObjectParameter* Expression =
			Add<UMaterialExpressionTextureObjectParameter>(Material);
		Expression->ParameterName = Parameter;
		Expression->Texture = Array;
		Expression->SamplerType = Type;
		return Expression;
	}

	static UMaterialExpressionVectorParameter* AddVector(UMaterial& Material, FName Name, const FLinearColor& Value)
	{
		UMaterialExpressionVectorParameter* Expression = Add<UMaterialExpressionVectorParameter>(Material);
		Expression->ParameterName = Name;
		Expression->DefaultValue = Value;
		return Expression;
	}

	static UMaterialExpressionVectorParameter* AddOrigin(UMaterial& Material, FName Name, uint8 DataIndex)
	{
		UMaterialExpressionVectorParameter* Expression = AddVector(Material, Name, FLinearColor::Transparent);
		Expression->bUseCustomPrimitiveData = true;
		Expression->PrimitiveDataIndex = DataIndex;
		return Expression;
	}

	static UMaterialExpressionScalarParameter* AddScalar(UMaterial& Material, FName Name, float Value)
	{
		UMaterialExpressionScalarParameter* Expression = Add<UMaterialExpressionScalarParameter>(Material);
		Expression->ParameterName = Name;
		Expression->DefaultValue = Value;
		return Expression;
	}

	static UTexture2DArray* AddNeutralArray(
		UMaterial& Material, FName Name, FColor Pixel, TextureCompressionSettings Compression, bool bIsSRGB)
	{
		UTexture2DArray* Array = NewObject<UTexture2DArray>(&Material, Name, RF_Transactional);
		Array->Source.Init(1, 1, 1, 1, TSF_BGRA8, reinterpret_cast<const uint8*>(&Pixel));
		Array->AddressX = TA_Wrap;
		Array->AddressY = TA_Wrap;
		Array->CompressionSettings = Compression;
		Array->SRGB = bIsSRGB;
		Array->PostEditChange();
		return Array;
	}

	static constexpr int32 VectorRGBAOutput = 5;
	static constexpr int32 FirstMaterialUV = 1;
	static constexpr int32 MaterialUVCount = 4;
	static constexpr int32 PeriodGroupCount =
		UVoxelLandMaterialPalette::MaxSlots / FVoxelLandTerrainMaterial::PeriodsPerParameter;
};

UMaterial* FVoxelLandTerrainMaterialBuilder::Build(UObject* Outer, FName Name, const UVoxelLandMaterialPalette& Palette)
{
	if (!IsValid(Outer) || !IsValid(Palette.BaseColorArray) || !IsValid(Palette.NormalArray)
		|| !IsValid(Palette.ORMHArray) || Palette.BakedSlots.IsEmpty())
	{
		return nullptr;
	}

	UMaterial* Material = NewObject<UMaterial>(Outer, Name, RF_Transactional);
	BuildExpressions(*Material, Palette);
	return Material;
}

UMaterial* FVoxelLandTerrainMaterialBuilder::LoadSharedMaster()
{
	return LoadObject<UMaterial>(nullptr, SharedMasterPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
}

UMaterial* FVoxelLandTerrainMaterialBuilder::CreateSharedMaster()
{
	if (UMaterial* Existing = LoadSharedMaster())
	{
		return Existing;
	}

	UPackage* Package = CreatePackage(SharedMasterPackage);
	UMaterial* Material =
		NewObject<UMaterial>(Package, TEXT("M_VoxelLandTerrain"), RF_Public | RF_Standalone | RF_Transactional);
	UVoxelLandMaterialPalette* Defaults = NewObject<UVoxelLandMaterialPalette>();
	Defaults->BakedSlots.AddDefaulted();
	Defaults->BaseColorArray =
		FVoxelLandTerrainMaterialNodes::AddNeutralArray(*Material, TEXT("White"), FColor::White, TC_Default, true);
	Defaults->NormalArray = FVoxelLandTerrainMaterialNodes::AddNeutralArray(
		*Material, TEXT("NeutralNormal"), FColor(128, 128, 255), TC_Normalmap, false);
	Defaults->ORMHArray = FVoxelLandTerrainMaterialNodes::AddNeutralArray(
		*Material, TEXT("DefaultORMH"), FColor(255, 191, 0, 128), TC_Masks, false);
	BuildExpressions(*Material, *Defaults);
	return Material;
}

void FVoxelLandTerrainMaterialBuilder::BuildExpressions(UMaterial& Material, const UVoxelLandMaterialPalette& Palette)
{
	Material.SetShadingModel(MSM_DefaultLit);
	Material.BlendMode = BLEND_Opaque;
	Material.bTangentSpaceNormal = false;

	UMaterialExpressionCustom* Custom = FVoxelLandTerrainMaterialNodes::Add<UMaterialExpressionCustom>(Material);
	Custom->Description = TEXT("VoxelLand biplanar terrain");
	Custom->OutputType = CMOT_Float3;
	Custom->IncludeFilePaths.Add(TEXT("/Plugin/VoxelLand/Private/VoxelLandTerrainMaterial.ush"));
	AddInput(*Custom, TEXT("ColorArray"),
		FVoxelLandTerrainMaterialNodes::AddArray(
			Material, FVoxelLandTerrainMaterial::BaseColorParameter, Palette.BaseColorArray, SAMPLERTYPE_Color));
	AddInput(*Custom, TEXT("NormalArray"),
		FVoxelLandTerrainMaterialNodes::AddArray(
			Material, FVoxelLandTerrainMaterial::NormalParameter, Palette.NormalArray, SAMPLERTYPE_Normal));
	AddInput(*Custom, TEXT("ORMHArray"),
		FVoxelLandTerrainMaterialNodes::AddArray(
			Material, FVoxelLandTerrainMaterial::ORMHParameter, Palette.ORMHArray, SAMPLERTYPE_LinearColor));
	AddMeshInputs(Material, *Custom);
	AddSlotInputs(Material, *Custom, Palette);
	ConnectOutputs(Material, *Custom);

	Material.PostEditChange();
}

void FVoxelLandTerrainMaterialBuilder::AddInput(
	UMaterialExpressionCustom& Custom, FName Name, UMaterialExpression* Expression, int32 OutputIndex)
{
	FCustomInput& Input = Custom.Inputs.AddDefaulted_GetRef();
	Input.InputName = Name;
	Input.Input.Connect(OutputIndex, Expression);
}

void FVoxelLandTerrainMaterialBuilder::AddMeshInputs(UMaterial& Material, UMaterialExpressionCustom& Custom)
{
	for (int32 Index = 0; Index < FVoxelLandTerrainMaterialNodes::MaterialUVCount; ++Index)
	{
		UMaterialExpressionTextureCoordinate* UV =
			FVoxelLandTerrainMaterialNodes::Add<UMaterialExpressionTextureCoordinate>(Material);
		UV->CoordinateIndex = FVoxelLandTerrainMaterialNodes::FirstMaterialUV + Index;
		AddInput(Custom, FName(*FString::Printf(TEXT("MeshUV%d"), UV->CoordinateIndex)), UV);
	}

	UMaterialExpressionLocalPosition* LocalPosition =
		FVoxelLandTerrainMaterialNodes::Add<UMaterialExpressionLocalPosition>(Material);
	LocalPosition->LocalOrigin = ELocalPositionOrigin::Primitive;
	AddInput(Custom, TEXT("LocalPosition"), LocalPosition);
	AddInput(Custom, TEXT("OriginHigh"),
		FVoxelLandTerrainMaterialNodes::AddOrigin(Material, FVoxelLandTerrainMaterial::LogicalOriginHighParameter,
			FVoxelLandTerrainMaterial::LogicalOriginHighIndex));
	AddInput(Custom, TEXT("OriginLow"),
		FVoxelLandTerrainMaterialNodes::AddOrigin(Material, FVoxelLandTerrainMaterial::LogicalOriginLowParameter,
			FVoxelLandTerrainMaterial::LogicalOriginLowIndex));
	AddInput(Custom, TEXT("GeometricNormal"),
		FVoxelLandTerrainMaterialNodes::Add<UMaterialExpressionVertexNormalWS>(Material));

	UMaterialExpressionWorldPosition* ViewPosition =
		FVoxelLandTerrainMaterialNodes::Add<UMaterialExpressionWorldPosition>(Material);
	ViewPosition->WorldPositionShaderOffset = WPT_CameraRelativeNoOffsets;
	AddInput(Custom, TEXT("ViewPosition"), ViewPosition);
	AddInput(Custom, TEXT("FarDistanceCm"),
		FVoxelLandTerrainMaterialNodes::AddScalar(Material, FVoxelLandTerrainMaterial::FarDistanceParameter,
			FVoxelLandTerrainMaterial::DefaultFarDistanceCm));
	AddInput(Custom, TEXT("DebugMode"),
		FVoxelLandTerrainMaterialNodes::AddScalar(Material, FVoxelLandTerrainMaterial::DebugModeParameter, 0.0f));
}

void FVoxelLandTerrainMaterialBuilder::AddSlotInputs(
	UMaterial& Material, UMaterialExpressionCustom& Custom, const UVoxelLandMaterialPalette& Palette)
{
	FString TintInputs;
	for (int32 Slot = 0; Slot < UVoxelLandMaterialPalette::MaxSlots; ++Slot)
	{
		const FName Name = FVoxelLandTerrainMaterial::SlotTintContrastParameter(Slot);
		UMaterialExpressionVectorParameter* Parameter = FVoxelLandTerrainMaterialNodes::AddVector(
			Material, Name, FVoxelLandTerrainMaterial::SlotTintContrast(Palette, Slot));
		AddInput(Custom, Name, Parameter, FVoxelLandTerrainMaterialNodes::VectorRGBAOutput);
		TintInputs += Slot > 0 ? TEXT(", ") : TEXT("");
		TintInputs += Name.ToString();
	}

	FString PeriodInputs;
	for (int32 Group = 0; Group < FVoxelLandTerrainMaterialNodes::PeriodGroupCount; ++Group)
	{
		const FName Name = FVoxelLandTerrainMaterial::PeriodGroupParameter(Group);
		UMaterialExpressionVectorParameter* Parameter = FVoxelLandTerrainMaterialNodes::AddVector(
			Material, Name, FVoxelLandTerrainMaterial::PeriodGroup(Palette, Group));
		AddInput(Custom, Name, Parameter, FVoxelLandTerrainMaterialNodes::VectorRGBAOutput);
		PeriodInputs += Group > 0 ? TEXT(", ") : TEXT("");
		PeriodInputs += Name.ToString();
	}

	Custom.Code = FString::Printf(
		TEXT(
			"const float4 SlotTintContrast[%d] = { %s };\n"
			"const float4 PackedPeriods[%d] = { %s };\n"
			"return VoxelLandShadeTerrain(ColorArray, ColorArraySampler, NormalArray, NormalArraySampler, ORMHArray, ORMHArraySampler,"
			" float4(MeshUV1, MeshUV2), float4(MeshUV3, MeshUV4), LocalPosition, OriginHigh, OriginLow, GeometricNormal, ViewPosition,"
			" FarDistanceCm, DebugMode, SlotTintContrast, PackedPeriods, WorldNormal, AO, Roughness, Metallic);"),
		UVoxelLandMaterialPalette::MaxSlots, *TintInputs, FVoxelLandTerrainMaterialNodes::PeriodGroupCount,
		*PeriodInputs);
}

void FVoxelLandTerrainMaterialBuilder::ConnectOutputs(UMaterial& Material, UMaterialExpressionCustom& Custom)
{
	FCustomOutput& Normal = Custom.AdditionalOutputs.AddDefaulted_GetRef();
	Normal.OutputName = TEXT("WorldNormal");
	Normal.OutputType = CMOT_Float3;
	Custom.AdditionalOutputs.AddDefaulted_GetRef().OutputName = TEXT("AO");
	Custom.AdditionalOutputs.AddDefaulted_GetRef().OutputName = TEXT("Roughness");
	Custom.AdditionalOutputs.AddDefaulted_GetRef().OutputName = TEXT("Metallic");
	Custom.RebuildOutputs();

	UMaterialEditorOnlyData* Data = Material.GetEditorOnlyData();
	check(Data);
	Data->BaseColor.Connect(0, &Custom);
	Data->Normal.Connect(1, &Custom);
	Data->AmbientOcclusion.Connect(2, &Custom);
	Data->Roughness.Connect(3, &Custom);
	Data->Metallic.Connect(4, &Custom);
}
