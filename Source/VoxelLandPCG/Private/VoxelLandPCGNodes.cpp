#include "VoxelLandPCGNodes.h"
#include "Generation/Placement/VoxelLandTerrainPlacementPoint.h"
#include "VoxelLandPCGTerrainData.h"
#include "Data/PCGBasePointData.h"
#include "Metadata/PCGMetadata.h"
#include "PCGContext.h"

const FName FVoxelLandPCGNodes::TerrainPinName(TEXT("Terrain"));

FPCGPinProperties FVoxelLandPCGNodes::TerrainPin(bool bRequired)
{
	FPCGPinProperties Pin(TerrainPinName, FPCGDataTypeIdentifier(FPCGDataTypeInfoVoxelTerrain::AsId()), false, false);
	Pin.PinStatus = bRequired ? EPCGPinStatus::Required : EPCGPinStatus::Normal;
	return Pin;
}

const UPCGVoxelTerrainData* FVoxelLandPCGNodes::GetTerrain(const FPCGContext& Context)
{
	const TArray<FPCGTaggedData> Inputs = Context.InputData.GetInputsByPin(TerrainPinName);
	return Inputs.Num() == 1 ? Cast<UPCGVoxelTerrainData>(Inputs[0].Data) : nullptr;
}

UPCGBasePointData* FVoxelLandPCGNodes::MakePoints(FPCGContext& Context, int32 Count, const UPCGBasePointData* Input)
{
	UPCGBasePointData* Points = FPCGContext::NewPointData_AnyThread(&Context);
	if (Input)
	{
		FPCGInitializeFromDataParams Params(Input);
		Params.bInheritSpatialData = false;
		Points->InitializeFromDataWithParams(Params);
	}
	Points->SetNumPoints(Count);
	Points->AllocateProperties(Input ? Input->GetAllocatedProperties() | SurfaceProperties() : SurfaceProperties());
	if (Input)
	{
		Points->CopyUnallocatedPropertiesFrom(Input);
	}
	return Points;
}

void FVoxelLandPCGNodes::PublishPoints(FPCGContext& Context, UPCGBasePointData& Points, const FPCGTaggedData* Input)
{
	FPCGTaggedData& Output = Context.OutputData.TaggedData.AddDefaulted_GetRef();
	if (Input)
	{
		Output = *Input;
	}
	Output.Pin = PCGPinConstants::DefaultOutputLabel;
	Output.Data = &Points;
}

bool FVoxelLandPCGNodes::Fail(FPCGContext& Context, const FText& Reason)
{
	Context.LogVisual(ELogVerbosity::Error, Reason);
	Context.OutputData.TaggedData.Reset();
	Context.OutputData.bCancelExecution = true;
	return true;
}

EPCGPointNativeProperties FVoxelLandPCGNodes::SurfaceProperties()
{
	return EPCGPointNativeProperties::Transform | EPCGPointNativeProperties::Seed
		 | EPCGPointNativeProperties::MetadataEntry;
}

bool FVoxelLandPCGNodes::WriteSurfaceMetadata(
	UPCGBasePointData& Points, TConstArrayView<FVoxelLandTerrainPlacementPoint> Samples)
{
	UPCGMetadata* Metadata = Points.MutableMetadata();
	if (!Metadata)
	{
		return false;
	}
	FPCGMetadataAttribute<FVector>* Normal =
		Metadata->FindOrCreateAttribute<FVector>(FName(TEXT("VoxelNormal")), FVector::UpVector);
	FPCGMetadataAttribute<double>* Elevation =
		Metadata->FindOrCreateAttribute<double>(FName(TEXT("VoxelElevationCm")), 0.0);
	FPCGMetadataAttribute<double>* Slope =
		Metadata->FindOrCreateAttribute<double>(FName(TEXT("VoxelSlopeDegrees")), 0.0);
	if (!Normal || !Elevation || !Slope)
	{
		return false;
	}
	FPCGPointMetadataEntry::ValueRange Entries = Points.GetMetadataEntryValueRange();
	for (int32 Index = 0; Index < Samples.Num(); ++Index)
	{
		const FVoxelLandTerrainPlacementPoint& Sample = Samples[Index];
		const int64 Entry = Metadata->AddEntry(Entries[Index]);
		Entries[Index] = Entry;
		Normal->SetValue(Entry, Sample.Normal);
		Elevation->SetValue(Entry, Sample.Position.Z);
		Slope->SetValue(Entry, FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Sample.Normal.Z, -1.0, 1.0))));
	}
	return true;
}
