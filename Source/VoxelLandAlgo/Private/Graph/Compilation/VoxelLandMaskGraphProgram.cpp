#include "Graph/Compilation/VoxelLandMaskGraphProgram.h"

#include "Data/Graph/Schema/VoxelLandGraphDiagnostic.h"
#include "Data/Graph/Sources/VoxelLandGraphOutputs.h"
#include "Graph/Compilation/VoxelLandGraphFunctionFlatten.h"
#include "Graph/Compilation/VoxelLandMaterialGraphPoint.h"
#include "Graph/Compilation/VoxelLandMaterialGraphProgram.h"

bool FVoxelLandMaskGraphProgram::Compile(const FVoxelLandGraphSnapshot& Source,
	TSharedPtr<const FVoxelLandMaskGraphProgram, ESPMode::ThreadSafe>& Out, FString& Error)
{
	Error.Reset();
	if (Source.SourceVersion != 1 || Source.GraphRole != EVoxelLandGraphRole::Mask
		|| Source.Nodes.Num() > FVoxelLandMaterialGraphProgram::MaxInstructions)
	{
		Error = TEXT("Expected a supported Mask Graph within the instruction limit.");
		return false;
	}

	FVoxelLandGraphSnapshot Expanded;
	const FVoxelLandGraphSnapshot* Input = &Source;
	if (!Source.Functions.IsEmpty())
	{
		TMap<FGuid, FGuid> Calls;
		TMap<FGuid, FString> Descriptions;
		FVoxelLandGraphDiagnostic Diagnostic;
		if (!FVoxelLandGraphFunctionFlatten::Expand(Source, Expanded, Diagnostic, Calls, Descriptions))
		{
			Error = Diagnostic.Message;
			return false;
		}
		Input = &Expanded;
	}
	if (Input->Nodes.Num() > FVoxelLandMaterialGraphProgram::MaxInstructions)
	{
		Error = TEXT("The expanded Mask Graph exceeds the instruction limit.");
		return false;
	}

	const TSharedRef<FVoxelLandMaskGraphProgram, ESPMode::ThreadSafe> Candidate =
		MakeShared<FVoxelLandMaskGraphProgram, ESPMode::ThreadSafe>();
	TSet<FName> Names;
	for (const FVoxelLandGraphNodeRecord& Node : Source.Nodes)
	{
		if (!FVoxelLandGraphOutputs::IsNamedOutput(Node.OperatorId))
		{
			continue;
		}
		const FName Name(*FVoxelLandGraphOutputs::GetName(Node));
		if (Name.IsNone() || Names.Contains(Name) || Candidate->Outputs.Num() >= MaxOutputs)
		{
			Error =
				FString::Printf(TEXT("Mask output '%s' (%s) needs a unique name. At most %d outputs are supported."),
					*Name.ToString(), *Node.Id.ToString(), MaxOutputs);
			return false;
		}
		TSharedPtr<const FVoxelLandMaterialGraphProgram, ESPMode::ThreadSafe> Scalar;
		if (!FVoxelLandMaterialGraphProgram::CompileScalarOutput(*Input, Node.Id, Scalar, Error))
		{
			Error = FString::Printf(TEXT("Mask output '%s': %s"), *Name.ToString(), *Error);
			return false;
		}
		for (const FVoxelLandMaterialGraphInstruction& Instruction : Scalar->GetInstructions())
		{
			if (Instruction.Kind == EVoxelLandBuiltinNode::TerrainSignal)
			{
				const EVoxelLandGraphValueType* Type = Candidate->TerrainSignals.Find(Instruction.SignalId);
				if (Type && *Type != Instruction.SignalType)
				{
					Error = FString::Printf(TEXT("Terrain signal %s is imported with conflicting types."),
						*Instruction.SignalId.ToString());
					return false;
				}
				Candidate->TerrainSignals.Add(Instruction.SignalId, Instruction.SignalType);
			}
		}
		Names.Add(Name);
		Candidate->Outputs.Add({Node.Id, Name});
		Candidate->Programs.Add(MoveTemp(Scalar));
	}
	if (Candidate->Outputs.IsEmpty())
	{
		Error = TEXT("Add a named Float output to the Mask Graph.");
		return false;
	}
	Out = Candidate;
	return true;
}

bool FVoxelLandMaskGraphProgram::IsSupportedNode(EVoxelLandBuiltinNode Kind)
{
	switch (Kind)
	{
		case EVoxelLandBuiltinNode::FloatOutput:
			return true;
		case EVoxelLandBuiltinNode::Surface:
		case EVoxelLandBuiltinNode::SurfaceMaterialOutput:
		case EVoxelLandBuiltinNode::BlendSurfaces:
		case EVoxelLandBuiltinNode::WorldSignal:
			return false;
		default:
			return FVoxelLandMaterialGraphProgram::IsSupportedNode(Kind);
	}
}

bool FVoxelLandMaskGraphProgram::Evaluate(FGuid OutputId, const FVoxelLandMaterialGraphPoint& Point, double& Out) const
{
	for (int32 Index = 0; Index < Outputs.Num(); ++Index)
	{
		if (Outputs[Index].Id != OutputId)
		{
			continue;
		}
		double Value = 0.0;
		if (!Programs[Index]->EvaluatePreview(OutputId, Point, Value) || !FMath::IsFinite(Value))
		{
			return false;
		}
		Out = FMath::Clamp(Value, 0.0, 1.0);
		return true;
	}
	return false;
}
