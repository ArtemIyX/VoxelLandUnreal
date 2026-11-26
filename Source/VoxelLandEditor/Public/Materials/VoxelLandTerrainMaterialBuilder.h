#pragma once

#include "CoreMinimal.h"

class UObject;
class UMaterial;
class UMaterialExpression;
class UMaterialExpressionCustom;
class UVoxelLandMaterialPalette;

class VOXELLANDEDITOR_API FVoxelLandTerrainMaterialBuilder
{
public:
	[[nodiscard]] static UMaterial* Build(UObject* Outer, FName Name, const UVoxelLandMaterialPalette& Palette);
	[[nodiscard]] static UMaterial* LoadSharedMaster();
	[[nodiscard]] static UMaterial* CreateSharedMaster();
	[[nodiscard]] static UMaterial* UpdateSharedMasterAmbientOcclusion(bool& bUpdated);

private:
	static const TCHAR* const SharedMasterPackage;
	static const TCHAR* const SharedMasterPath;
	static void BuildExpressions(UMaterial& Material, const UVoxelLandMaterialPalette& Palette);
	static void AddInput(
		UMaterialExpressionCustom& Custom, FName Name, UMaterialExpression* Expression, int32 OutputIndex = 0);
	static void AddMeshInputs(UMaterial& Material, UMaterialExpressionCustom& Custom);
	static void AddAmbientOcclusionInputs(UMaterial& Material, UMaterialExpressionCustom& Custom);
	static void AddSlotInputs(
		UMaterial& Material, UMaterialExpressionCustom& Custom, const UVoxelLandMaterialPalette& Palette);
	static void ConnectOutputs(UMaterial& Material, UMaterialExpressionCustom& Custom);
};
