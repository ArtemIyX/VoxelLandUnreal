#include "Rendering/Materials/VoxelLandTerrainMaterial.h"

#include "Data/Assets/Materials/VoxelLandMaterialPalette.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/Texture2DArray.h"
#include "Materials/MaterialInstanceDynamic.h"

const FName FVoxelLandTerrainMaterial::BaseColorParameter(TEXT("TerrainBaseColor"));
const FName FVoxelLandTerrainMaterial::NormalParameter(TEXT("TerrainNormal"));
const FName FVoxelLandTerrainMaterial::ORMHParameter(TEXT("TerrainORMH"));
const FName FVoxelLandTerrainMaterial::LogicalOriginHighParameter(TEXT("TerrainLogicalOriginHigh"));
const FName FVoxelLandTerrainMaterial::LogicalOriginLowParameter(TEXT("TerrainLogicalOriginLow"));
const FName FVoxelLandTerrainMaterial::FarDistanceParameter(TEXT("TerrainFarDistanceCm"));
const FName FVoxelLandTerrainMaterial::DebugModeParameter(TEXT("TerrainDebugMode"));
const FName FVoxelLandTerrainMaterial::AmbientOcclusionStrengthParameter(TEXT("TerrainAmbientOcclusionStrength"));

UMaterialInstanceDynamic* FVoxelLandTerrainMaterial::CreateInstance(
	UObject* Outer, const UVoxelLandMaterialPalette& Palette)
{
	if (!IsValid(Outer) || !IsValid(Palette.MasterMaterial) || !IsValid(Palette.BaseColorArray)
		|| !IsValid(Palette.NormalArray) || !IsValid(Palette.ORMHArray) || Palette.BakedSlots.IsEmpty())
	{
		return nullptr;
	}

	UMaterialInstanceDynamic* Instance = UMaterialInstanceDynamic::Create(Palette.MasterMaterial, Outer);
	if (!IsValid(Instance))
	{
		return nullptr;
	}

	Instance->SetTextureParameterValue(BaseColorParameter, Palette.BaseColorArray);
	Instance->SetTextureParameterValue(NormalParameter, Palette.NormalArray);
	Instance->SetTextureParameterValue(ORMHParameter, Palette.ORMHArray);
	for (int32 Slot = 0; Slot < Palette.BakedSlots.Num(); ++Slot)
	{
		Instance->SetVectorParameterValue(SlotTintContrastParameter(Slot), SlotTintContrast(Palette, Slot));
	}
	const int32 GroupCount = FMath::DivideAndRoundUp(Palette.BakedSlots.Num(), PeriodsPerParameter);
	for (int32 Group = 0; Group < GroupCount; ++Group)
	{
		Instance->SetVectorParameterValue(PeriodGroupParameter(Group), PeriodGroup(Palette, Group));
	}
	return Instance;
}

void FVoxelLandTerrainMaterial::SetLogicalOrigin(UPrimitiveComponent& Component, const FVector3d& LogicalOrigin)
{
	const FVector3f High(LogicalOrigin);
	const FVector3f Low(LogicalOrigin - FVector3d(High));
	Component.SetCustomPrimitiveDataVector4f(LogicalOriginHighIndex, FVector4f(High, 0.0f));
	Component.SetCustomPrimitiveDataVector4f(LogicalOriginLowIndex, FVector4f(Low, 0.0f));
}

FName FVoxelLandTerrainMaterial::SlotTintContrastParameter(int32 Slot)
{
	return FName(TEXT("TerrainSlotTintContrast"), Slot + 1);
}

FName FVoxelLandTerrainMaterial::PeriodGroupParameter(int32 Group)
{
	return FName(TEXT("TerrainSlotPeriods"), Group + 1);
}

FLinearColor FVoxelLandTerrainMaterial::SlotTintContrast(const UVoxelLandMaterialPalette& Palette, int32 Slot)
{
	check(!Palette.BakedSlots.IsEmpty());
	const FVoxelLandMaterialPaletteSlot& Entry = Palette.BakedSlots[Palette.BakedSlots.IsValidIndex(Slot) ? Slot : 0];
	return FLinearColor(Entry.Tint.R, Entry.Tint.G, Entry.Tint.B, Entry.HeightBlendContrast);
}

FLinearColor FVoxelLandTerrainMaterial::PeriodGroup(const UVoxelLandMaterialPalette& Palette, int32 Group)
{
	check(!Palette.BakedSlots.IsEmpty());
	FLinearColor Periods;
	for (int32 Channel = 0; Channel < PeriodsPerParameter; ++Channel)
	{
		const int32 Slot = Group * PeriodsPerParameter + Channel;
		const FVoxelLandMaterialPaletteSlot& Entry =
			Palette.BakedSlots[Palette.BakedSlots.IsValidIndex(Slot) ? Slot : 0];
		Periods.Component(Channel) = static_cast<float>(Entry.TilePeriodCm);
	}
	return Periods;
}
