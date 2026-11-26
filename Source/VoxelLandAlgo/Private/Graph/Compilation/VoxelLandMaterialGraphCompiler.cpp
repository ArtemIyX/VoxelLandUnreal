#include "Graph/Compilation/VoxelLandMaterialGraphProgram.h"

#include "Data/Graph/Schema/VoxelLandBuiltinCatalog.h"
#include "Data/Graph/Sources/VoxelLandGraphSnapshot.h"
#include "Graph/Compilation/VoxelLandGraphFunctionFlatten.h"

class FVoxelLandMaterialGraphCompile final
{
public:
	FVoxelLandMaterialGraphCompile(const FVoxelLandGraphSnapshot& InSource, TConstArrayView<FGuid> InPalette,
		FVoxelLandMaterialGraphProgram& InProgram, FString& InError)
		: Source(InSource), Palette(InPalette), Program(InProgram), Error(InError)
	{
	}

	bool Run()
	{
		if (Source.Nodes.Num() > FVoxelLandMaterialGraphProgram::MaxInstructions)
		{
			Error = TEXT("Material graph exceeds the surface instruction limit.");
			return false;
		}
		int32 Root = INDEX_NONE;
		if (!IndexGraph(Root))
		{
			return false;
		}
		if (Root == INDEX_NONE)
		{
			Error = TEXT("Connect a Surface to a Surface Material Output node.");
			return false;
		}
		Active.Init(false, Source.Nodes.Num());
		Resolved.Init(INDEX_NONE, Source.Nodes.Num());
		Program.Instructions.Reserve(Source.Nodes.Num());
		return Resolve(Root) != INDEX_NONE;
	}

private:
	bool IndexGraph(int32& Root)
	{
		TSet<FGuid> AllPins;
		for (int32 Index = 0; Index < Source.Nodes.Num(); ++Index)
		{
			const FVoxelLandGraphNodeRecord& Node = Source.Nodes[Index];
			if (!Node.Id.IsValid() || Nodes.Contains(Node.Id))
			{
				return Fail(Node, TEXT("Invalid or duplicate node ID."));
			}
			Nodes.Add(Node.Id, Index);
			for (const FVoxelLandGraphPinRecord& Pin : Node.Pins)
			{
				if (!Pin.Id.IsValid() || AllPins.Contains(Pin.Id))
				{
					return Fail(Node, TEXT("Invalid or duplicate pin ID."));
				}
				AllPins.Add(Pin.Id);
			}
			if (Node.OperatorId == VoxelLandBuiltinNodeId(EVoxelLandBuiltinNode::SurfaceMaterialOutput))
			{
				if (Root != INDEX_NONE)
				{
					return Fail(Node, TEXT("Only one Surface Material Output is allowed."));
				}
				Root = Index;
			}
		}
		for (const FVoxelLandGraphLinkRecord& Link : Source.Links)
		{
			if (!AllPins.Contains(Link.TargetPin) || !AllPins.Contains(Link.SourcePin) || Links.Contains(Link.TargetPin))
			{
				Error = TEXT("Invalid or duplicate material graph link.");
				return false;
			}
			Links.Add(Link.TargetPin, &Link);
		}
		return true;
	}

	int32 Resolve(int32 Index)
	{
		if (Resolved[Index] != INDEX_NONE)
		{
			return Resolved[Index];
		}
		const FVoxelLandGraphNodeRecord& Node = Source.Nodes[Index];
		if (Active[Index])
		{
			Fail(Node, TEXT("Material graph contains a cycle."));
			return INDEX_NONE;
		}

		FVoxelLandMaterialGraphInstruction Instruction;
		Instruction.Kind = static_cast<EVoxelLandBuiltinNode>(Node.OperatorId.D);
		Instruction.NodeId = Node.Id;
		Instruction.SeedSalt = static_cast<uint32>(Node.SeedSalt);
		if (Node.OperatorId != VoxelLandBuiltinNodeId(Instruction.Kind) || Node.OperatorVersion != 1
			|| !FVoxelLandMaterialGraphProgram::IsSupportedNode(Instruction.Kind))
		{
			Fail(Node, TEXT("The node is not supported in material graphs."));
			return INDEX_NONE;
		}
		FVoxelLandBuiltinNodeSpec Spec;
		if (!FVoxelLandBuiltinCatalog::Find(Instruction.Kind, Spec, Node.Pins.Num() - 1)
			|| Spec.Pins.Num() != Node.Pins.Num())
		{
			Fail(Node, TEXT("Unexpected material node pin count."));
			return INDEX_NONE;
		}

		Active[Index] = true;
		if (!ResolveInputs(Node, Spec, Instruction) || !ResolveSettings(Node, Instruction))
		{
			return INDEX_NONE;
		}
		Active[Index] = false;
		Program.bHasSlope |= Instruction.Kind == EVoxelLandBuiltinNode::SurfaceSlope;
		Resolved[Index] = Program.Instructions.Add(MoveTemp(Instruction));
		return Resolved[Index];
	}

	bool ResolveInputs(const FVoxelLandGraphNodeRecord& Node, const FVoxelLandBuiltinNodeSpec& Spec,
		FVoxelLandMaterialGraphInstruction& Instruction)
	{
		for (int32 Index = 0; Index < Node.Pins.Num(); ++Index)
		{
			const FVoxelLandGraphPinRecord& Pin = Node.Pins[Index];
			const FVoxelLandBuiltinPinSpec& PinSpec = Spec.Pins[Index];
			const bool bIsTypedSignal = Pin.bOutput && Instruction.Kind == EVoxelLandBuiltinNode::TerrainSignal
				&& (Pin.Type == EVoxelLandGraphValueType::Int32 || Pin.Type == EVoxelLandGraphValueType::Bool);
			if ((!bIsTypedSignal && Pin.Type != PinSpec.Type) || Pin.bOutput != PinSpec.bOutput || Pin.bInvalidDefault)
			{
				return Fail(Node, TEXT("Material node pin is incompatible or has an invalid default."));
			}
			if (!Pin.bOutput && !ResolveInput(Node, Pin, Instruction.Inputs.AddDefaulted_GetRef()))
			{
				return false;
			}
		}
		return true;
	}

	bool ResolveInput(const FVoxelLandGraphNodeRecord& Node, const FVoxelLandGraphPinRecord& Pin,
		FVoxelLandMaterialGraphInput& Input)
	{
		const FVoxelLandGraphLinkRecord* const* Wire = Links.Find(Pin.Id);
		if (!Wire)
		{
			if (Pin.Type == EVoxelLandGraphValueType::Surface)
			{
				return Fail(Node, TEXT("Connect a Surface to each Surface input."));
			}
			Input.Default = ReadLiteral(Pin);
			return FMath::IsFinite(Input.Default) || Fail(Node, TEXT("A scalar default is not finite."));
		}
		const FVoxelLandGraphLinkRecord& Link = **Wire;
		const int32* LinkedIndex = Nodes.Find(Link.SourceNode);
		if (!LinkedIndex || Link.TargetNode != Node.Id)
		{
			return Fail(Node, TEXT("Invalid material graph link ownership."));
		}
		const FVoxelLandGraphNodeRecord& Linked = Source.Nodes[*LinkedIndex];
		if (Linked.Pins.IsEmpty() || Linked.Pins.Last().Id != Link.SourcePin || !Linked.Pins.Last().bOutput
			|| Linked.Pins.Last().Type != Pin.Type)
		{
			return Fail(Node, TEXT("Invalid material graph link type."));
		}
		Input.Instruction = Resolve(*LinkedIndex);
		return Input.Instruction != INDEX_NONE;
	}

	static double ReadLiteral(const FVoxelLandGraphPinRecord& Pin)
	{
		if (Pin.Type == EVoxelLandGraphValueType::Bool)
		{
			return Pin.BoolDefault ? 1.0 : 0.0;
		}
		if (Pin.Type == EVoxelLandGraphValueType::Int32)
		{
			return static_cast<double>(Pin.IntDefault);
		}
		return Pin.FloatDefault;
	}

	bool ResolveSettings(const FVoxelLandGraphNodeRecord& Node, FVoxelLandMaterialGraphInstruction& Instruction)
	{
		switch (Instruction.Kind)
		{
			case EVoxelLandBuiltinNode::Surface:
			{
				FGuid Id;
				if (!FVoxelLandMaterialGraphProgram::ReadGuid(Node.Configuration, Id))
				{
					return Fail(Node, TEXT("Select a surface from the World Config's material palette."));
				}
				const int32 Slot = Palette.Find(Id);
				if (Slot == INDEX_NONE)
				{
					Error = FString::Printf(TEXT("Surface '%s' (%s), node %s, is absent from the material palette."),
						*Node.SurfaceAsset.ToString(), *Id.ToString(), *Node.Id.ToString());
					return false;
				}
				Instruction.Slot = static_cast<uint8>(Slot);
				return true;
			}
			case EVoxelLandBuiltinNode::TerrainSignal:
				return FVoxelLandMaterialGraphProgram::ReadGuid(Node.Configuration, Instruction.SignalId)
					|| Fail(Node, TEXT("Select a named terrain output."));
			case EVoxelLandBuiltinNode::WorldSignal:
				return ReadWorldChannel(Node, Instruction);
			case EVoxelLandBuiltinNode::FloatParameter:
			case EVoxelLandBuiltinNode::IntParameter:
			case EVoxelLandBuiltinNode::BoolParameter:
				return ResolveParameter(Node, Instruction);
			default:
				return true;
		}
	}

	bool ReadWorldChannel(const FVoxelLandGraphNodeRecord& Node, FVoxelLandMaterialGraphInstruction& Instruction)
	{
		constexpr int32 MaxChannelBytes = 128;
		if (Node.Configuration.IsEmpty() || Node.Configuration.Num() > MaxChannelBytes
			|| Node.Configuration.Contains(0) || Instruction.Inputs[1].Instruction != INDEX_NONE)
		{
			return Fail(Node, TEXT("World Signal needs a channel name and a literal Required setting."));
		}
		const FUTF8ToTCHAR Channel(reinterpret_cast<const ANSICHAR*>(Node.Configuration.GetData()), Node.Configuration.Num());
		Instruction.Channel = FName(*FString(Channel.Length(), Channel.Get()));
		return !Instruction.Channel.IsNone() || Fail(Node, TEXT("World Signal channel cannot be None."));
	}

	bool ResolveParameter(const FVoxelLandGraphNodeRecord& Node, FVoxelLandMaterialGraphInstruction& Instruction)
	{
		for (const FVoxelLandGraphParameter& Parameter : Source.Parameters)
		{
			if (Parameter.Id != Node.ParameterId)
			{
				continue;
			}
			if (Node.Pins.Last().Type != Parameter.Type || !FMath::IsFinite(Parameter.FloatDefault))
			{
				return Fail(Node, TEXT("Material parameter has an incompatible type or invalid default."));
			}
			FVoxelLandGraphPinRecord Literal;
			Literal.Type = Parameter.Type;
			Literal.FloatDefault = Parameter.FloatDefault;
			Literal.IntDefault = Parameter.IntDefault;
			Literal.BoolDefault = Parameter.BoolDefault;
			Instruction.Inputs.Add({INDEX_NONE, ReadLiteral(Literal)});
			return true;
		}
		return Fail(Node, TEXT("Material parameter is missing."));
	}

	bool Fail(const FVoxelLandGraphNodeRecord& Node, const TCHAR* Reason)
	{
		Error = FString::Printf(TEXT("Material node %s: %s"), *Node.Id.ToString(), Reason);
		return false;
	}

	const FVoxelLandGraphSnapshot& Source;
	TConstArrayView<FGuid> Palette;
	FVoxelLandMaterialGraphProgram& Program;
	FString& Error;
	TMap<FGuid, int32> Nodes;
	TMap<FGuid, const FVoxelLandGraphLinkRecord*> Links;
	TArray<bool> Active;
	TArray<int32> Resolved;
};

bool FVoxelLandMaterialGraphProgram::Compile(const FVoxelLandGraphSnapshot& Source, TConstArrayView<FGuid> Palette,
	TSharedPtr<const FVoxelLandMaterialGraphProgram, ESPMode::ThreadSafe>& Out, FString& Error)
{
	Error.Reset();
	if (Source.SourceVersion != 1 || Source.GraphRole != EVoxelLandGraphRole::Material || Palette.IsEmpty()
		|| Palette.Num() > 32 || Source.Nodes.Num() > MaxInstructions)
	{
		Error = TEXT("Expected a supported Material graph and a palette with 1 to 32 slots.");
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
	const TSharedRef<FVoxelLandMaterialGraphProgram, ESPMode::ThreadSafe> Candidate =
		MakeShared<FVoxelLandMaterialGraphProgram, ESPMode::ThreadSafe>();
	Candidate->SlotCount = static_cast<uint8>(Palette.Num());
	FVoxelLandMaterialGraphCompile Compiler(*Input, Palette, *Candidate, Error);
	if (!Compiler.Run())
	{
		return false;
	}
	Out = Candidate;
	return true;
}

bool FVoxelLandMaterialGraphProgram::ReadGuid(TConstArrayView<uint8> Bytes, FGuid& Out)
{
	constexpr int32 GuidBytes = sizeof(uint32) * 4;
	if (Bytes.Num() != GuidBytes)
	{
		return false;
	}
	uint32 Fields[4] = {};
	for (int32 Index = 0; Index < GuidBytes; ++Index)
	{
		Fields[Index / sizeof(uint32)] |= uint32(Bytes[Index]) << (8 * (Index % sizeof(uint32)));
	}
	Out = FGuid(Fields[0], Fields[1], Fields[2], Fields[3]);
	return Out.IsValid();
}

void FVoxelLandMaterialGraphProgram::WriteGuid(FGuid Id, TArray<uint8>& Out)
{
	Out.Reset(sizeof(uint32) * 4);
	for (const uint32 Field : {Id.A, Id.B, Id.C, Id.D})
	{
		for (int32 Byte = 0; Byte < sizeof(uint32); ++Byte)
		{
			Out.Add(static_cast<uint8>(Field >> (8 * Byte)));
		}
	}
}
