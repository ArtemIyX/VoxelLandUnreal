#include "VoxelLandPCGVoxelMaskSampleElement.h"
#include "VoxelLandPCGNodes.h"
#include "Generation/Placement/VoxelLandTerrainPlacement.h"
#include "VoxelLandPCGTerrainData.h"
#include "VoxelLandPCGVoxelMaskSampleSettings.h"
#include "Data/PCGBasePointData.h"
#include "Generation/Collision/VoxelLandCollisionField.h"
#include "Graph/Compilation/VoxelLandMaskGraphProgram.h"
#include "Graph/Compilation/VoxelLandMaterialGraphPoint.h"
#include "Metadata/PCGMetadata.h"
#include "PCGContext.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

bool FVoxelLandPCGVoxelMaskSampleElement::ExecuteInternal(FPCGContext* Context) const
{
	check(Context);
	TRACE_CPUPROFILER_EVENT_SCOPE(VoxelLand_PCGEvaluateMasks);
	const UVoxelLandPCGVoxelMaskSampleSettings* Settings =
		Context->GetInputSettings<UVoxelLandPCGVoxelMaskSampleSettings>();
	const UPCGVoxelTerrainData* Data = FVoxelLandPCGNodes::GetTerrain(*Context);
	if (!Settings || !Data)
	{
		return FVoxelLandPCGNodes::Fail(*Context,
			NSLOCTEXT("VoxelLandPCG", "MaskNeedsTerrain", "Voxel Mask Sample requires a ready voxel terrain input."));
	}
	const TArray<FPCGTaggedData> Inputs = Context->InputData.GetInputsByPin(PCGPinConstants::DefaultInputLabel);
	if (Settings->Bindings.IsEmpty())
	{
		for (const FPCGTaggedData& Input : Inputs)
		{
			FPCGTaggedData& Output = Context->OutputData.TaggedData.Add_GetRef(Input);
			Output.Pin = PCGPinConstants::DefaultOutputLabel;
		}
		return true;
	}
	const FVoxelLandMaskGraphProgram* Program = Data->GetMaskProgram();
	constexpr int32 MaximumBindings = 32;
	if (!Program || Settings->Bindings.Num() > MaximumBindings)
	{
		return FVoxelLandPCGNodes::Fail(
			*Context, NSLOCTEXT("VoxelLandPCG", "MaskProgramMissing",
						  "Voxel Mask Sample requires a compiled Mask Graph and at most 32 output bindings."));
	}
	TSet<FName> Attributes;
	for (const FVoxelLandPCGMaskSampleSettingsBinding& Binding : Settings->Bindings)
	{
		bool bResolved = false;
		for (const FVoxelLandMaskGraphOutput& Output : Program->GetOutputs())
		{
			bResolved |= Output.Id == Binding.OutputId;
		}
		if (!bResolved || Binding.Attribute.IsNone() || Attributes.Contains(Binding.Attribute))
		{
			return FVoxelLandPCGNodes::Fail(
				*Context, FText::Format(NSLOCTEXT("VoxelLandPCG", "MaskBindingInvalid",
											"Invalid Mask Graph binding {0} on PCG attribute {1}."),
							  FText::FromString(Binding.OutputId.ToString()), FText::FromName(Binding.Attribute)));
		}
		Attributes.Add(Binding.Attribute);
	}
	int32 Processed = 0;
	for (const FPCGTaggedData& Tagged : Inputs)
	{
		const UPCGBasePointData* Input = Cast<UPCGBasePointData>(Tagged.Data);
		if (!Input || Input->GetNumPoints() > FVoxelLandTerrainPlacement::MaxPoints - Processed
			|| !SampleInput(*Context, Tagged, *Input, *Data, *Settings))
		{
			return FVoxelLandPCGNodes::Fail(*Context,
				NSLOCTEXT("VoxelLandPCG", "MaskSampleFailed",
					"Voxel Mask Sample exceeded its budget or could not evaluate an output on the terrain snapshot."));
		}
		Processed += Input->GetNumPoints();
		if (Data->IsCancelled())
		{
			return true;
		}
	}
	return true;
}

bool FVoxelLandPCGVoxelMaskSampleElement::SampleInput(FPCGContext& Context, const FPCGTaggedData& Tagged,
	const UPCGBasePointData& Input, const UPCGVoxelTerrainData& Data,
	const UVoxelLandPCGVoxelMaskSampleSettings& Settings)
{
	const FVoxelLandCollisionField* Field = Data.GetView().Field.Get();
	const FVoxelLandMaskGraphProgram* Program = Data.GetMaskProgram();
	check(Field && Program);
	const int32 Count = Input.GetNumPoints();
	UPCGBasePointData* Points = FVoxelLandPCGNodes::MakePoints(Context, Count, &Input);
	Input.CopyPointsTo(Points, 0, 0, Count);
	UPCGMetadata* Metadata = Points->MutableMetadata();
	if (!Metadata)
	{
		return false;
	}
	TArray<FPCGMetadataAttribute<double>*, TInlineAllocator<8>> Outputs;
	for (const FVoxelLandPCGMaskSampleSettingsBinding& Binding : Settings.Bindings)
	{
		FPCGMetadataAttribute<double>* Attribute = Metadata->FindOrCreateAttribute<double>(Binding.Attribute, 0.0);
		if (!Attribute)
		{
			return false;
		}
		Outputs.Add(Attribute);
	}
	const FPCGPointTransform::ConstValueRange Transforms = Input.GetConstTransformValueRange();
	FPCGPointMetadataEntry::ValueRange Entries = Points->GetMetadataEntryValueRange();
	const FVector Origin(Data.GetView().OriginLocation);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		if (Data.IsCancelled())
		{
			return true;
		}
		FVoxelLandMaterialGraphPoint Point;
		Point.Position = Transforms[Index].GetLocation() + Origin;
		Point.Seed = Data.GetMaskSeed();
		Point.Signals = Data.GetTerrainSignals();
		double Density = 0.0;
		FVector Gradient;
		if (Field->Sample(Point.Position, Density, &Gradient) != EVoxelLandCollisionFieldStatus::Available)
		{
			return false;
		}
		const FVector Normal = Gradient.GetSafeNormal(UE_DOUBLE_SMALL_NUMBER, FVector::UpVector);
		Point.SlopeDegrees = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Normal.Z, -1.0, 1.0)));
		const int64 Entry = Metadata->AddEntry(Entries[Index]);
		Entries[Index] = Entry;
		for (int32 OutputIndex = 0; OutputIndex < Outputs.Num(); ++OutputIndex)
		{
			double Weight = 0.0;
			if (!Program->Evaluate(Settings.Bindings[OutputIndex].OutputId, Point, Weight))
			{
				return false;
			}
			Outputs[OutputIndex]->SetValue(Entry, Weight);
		}
	}
	FVoxelLandPCGNodes::PublishPoints(Context, *Points, &Tagged);
	return true;
}
