#include "Graph/Compilation/VoxelLandGraphCompiler.h"
#include "Graph/Compilation/VoxelLandGraphFunctionFlatten.h"
#include "Data/Graph/Schema/VoxelLandGraphFunctionId.h"
#include "VoxelLandProgramOptimizer.h"

#include "Data/Graph/Schema/VoxelLandBuiltinNode.h"
#include "Data/Graph/Schema/VoxelLandBuiltinCatalog.h"
#include "Data/Graph/Schema/VoxelLandGraphCycle.h"
#include "Data/Program/Execution/VoxelLandParameterType.h"
#include "Operators/VoxelLandOperator.h"
#include "Generation/Field/VoxelLandPower.h"
#include "Generation/Caves/Infrastructure/VoxelLandCaveBudget.h"
#include "Generation/Caves/Infrastructure/VoxelLandCaveBudgetLease.h"
#include "Graph/Capabilities/VoxelLandWorldCapabilities.h"
#include <cmath>

class FVoxelLandGraphCompilePass final
{
	static constexpr int32 MaxInstructions = 8192;

public:
	FVoxelLandGraphCompilePass(const FVoxelLandGraphSnapshot& InSource,
		const FVoxelLandOperatorRegistrySnapshot& InProviders, FVoxelLandGraphDiagnostic& InDiagnostic,
		TArray<FGuid>* InInstructionNodes)
		: Source(InSource)
		, Providers(InProviders)
		, Diagnostic(InDiagnostic)
		, Error(InDiagnostic.Message)
		, InstructionNodes(InInstructionNodes)
	{
		Draft.FormatVersion = FVoxelLandProgramDraft::CurrentFormatVersion;
	}

	bool Run(FVoxelLandProgramDraft& Out)
	{
		if (!IndexSource() || !FindOutput())
		{
			return false;
		}
		uint16 Result = 0;
		if (!CompileRoot(Result))
		{
			return false;
		}
		FVoxelLandInstruction Store;
		Store.Op = EVoxelLandOpcode::StoreDensity;
		Store.A = Result;
		AddInstruction(Store, Output->Id);
		if (Draft.Instructions.Num() > MaxInstructions)
		{
			return Fail(TEXT("Graph exceeds instruction limit"));
		}
		if (!FVoxelLandProgramOptimizer::Optimize(Draft, Error, InstructionNodes))
		{
			return false;
		}
		Out = MoveTemp(Draft);
		return true;
	}

	static bool CompileSource(const FVoxelLandGraphSnapshot& Snapshot,
		const FVoxelLandOperatorRegistrySnapshot& Providers, FVoxelLandProgramDraft& OutDraft,
		FVoxelLandGraphDiagnostic& OutDiagnostic, TArray<FGuid>* InstructionNodes,
		TMap<FGuid, FVoxelLandGraphNodeOrigin>* NodeOrigins);

	struct FWorldValidationContext
	{
		const TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe>& Budget;
		TFunctionRef<bool()> IsCancelled;
		FVoxelLandWorldCapabilityDiagnostic& Capability;
		FVoxelLandGraphDiagnostic& Diagnostic;

		bool Fail(EVoxelLandWorldCapabilityError Code, const TCHAR* Message) const
		{
			Capability.Code = Code;
			Capability.Message = Message;
			Diagnostic.Message = Message;
			return false;
		}
	};

	static bool PrepareWorldOperators(const FVoxelLandGraphSnapshot& Snapshot, EVoxelLandWorldKind Kind,
		const FVoxelLandOperatorRegistrySnapshot& Providers, const FWorldValidationContext& Context,
		TArray<FVoxelLandWorldOperatorCapability>& Declarations,
		TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe>& DeclarationsLease);
	static bool ValidateFixedLatticeDraft(const FVoxelLandProgramDraft& Draft, TConstArrayView<FGuid> InstructionNodes,
		const TMap<FGuid, FVoxelLandGraphNodeOrigin>& NodeOrigins, const FWorldValidationContext& Context);

private:
	static constexpr int32 MaxWorldProviders = 256;
	static bool QuantizeFixedValue(float Value, int64& Out);
	static bool ReadFixedConstant(const FVoxelLandProgramDraft& Draft, const FVoxelLandValueSource& Source, float& Out);
	static const TCHAR* ValidateFixedNoise(const FVoxelLandProgramDraft& Draft, const FVoxelLandInstruction& I);
	static bool CollectWorldOperators(TConstArrayView<FVoxelLandGraphNodeRecord> Nodes,
		const FVoxelLandOperatorRegistrySnapshot& Providers, const FWorldValidationContext& Context,
		TArray<FVoxelLandWorldOperatorCapability>& Declarations);
	void AddInstruction(const FVoxelLandInstruction& Instruction, const FGuid& NodeId)
	{
		Draft.Instructions.Add(Instruction);
		if (InstructionNodes)
		{
			InstructionNodes->Add(NodeId);
		}
	}

	bool Fail(const TCHAR* Message)
	{
		Error = Message;
		Diagnostic.NodeId = CurrentNode ? CurrentNode->Id : FGuid();
		Diagnostic.PinId = ActivePin;
		return false;
	}

	bool FailAt(const TCHAR* Message, const FGuid& NodeId, const FGuid& PinId = FGuid())
	{
		Error = Message;
		Diagnostic.NodeId = NodeId;
		Diagnostic.PinId = PinId;
		return false;
	}

	bool IndexSource()
	{
		if (Source.SourceVersion != 1 || Source.Nodes.Num() > 4096 || Source.Links.Num() > 16384
			|| Source.Parameters.Num() > 4096)
		{
			return Fail(TEXT("Unsupported graph version or graph exceeds limits"));
		}
		return IndexParameters() && IndexNodes() && IndexLinks();
	}

	bool IndexParameters()
	{
		TSet<FString> Names;
		for (const FVoxelLandGraphParameter& Parameter : Source.Parameters)
		{
			FString Name = Parameter.Name.ToLower();
			if (!Parameter.Id.IsValid() || ParameterById.Contains(Parameter.Id) || Name.IsEmpty()
				|| Names.Contains(Name))
			{
				return Fail(TEXT("Invalid or duplicate parameter ID or name"));
			}
			for (TCHAR C : Name)
			{
				if (!((C >= 'a' && C <= 'z') || (C >= '0' && C <= '9') || C == '_'))
				{
					return Fail(TEXT("Parameter name must be an ASCII identifier"));
				}
			}
			if (Name[0] >= '0' && Name[0] <= '9')
			{
				return Fail(TEXT("Parameter name cannot begin with a digit"));
			}
			if (Parameter.Type == EVoxelLandGraphValueType::Float)
			{
				if (!FMath::IsFinite(Parameter.FloatDefault)
					|| (Parameter.bHasRange
						&& (!FMath::IsFinite(Parameter.FloatMin) || !FMath::IsFinite(Parameter.FloatMax)
							|| Parameter.FloatMin > Parameter.FloatMax || Parameter.FloatDefault < Parameter.FloatMin
							|| Parameter.FloatDefault > Parameter.FloatMax)))
				{
					return Fail(TEXT("Invalid float parameter default or range"));
				}
			}
			else if (Parameter.Type == EVoxelLandGraphValueType::Int32)
			{
				if (Parameter.bHasRange
					&& (Parameter.IntMin > Parameter.IntMax || Parameter.IntDefault < Parameter.IntMin
						|| Parameter.IntDefault > Parameter.IntMax))
				{
					return Fail(TEXT("Invalid integer parameter default or range"));
				}
			}
			else if (Parameter.Type != EVoxelLandGraphValueType::Bool || Parameter.bHasRange)
			{
				return Fail(TEXT("Invalid boolean parameter or type"));
			}
			Names.Add(Name);
			ParameterById.Add(Parameter.Id, &Parameter);
		}
		return true;
	}

	bool IndexNodes()
	{
		int64 ConfigurationBytes = 0;
		for (const FVoxelLandGraphNodeRecord& Node : Source.Nodes)
		{
			ConfigurationBytes += Node.Configuration.Num();
			if (!Node.OperatorId.IsValid() || Node.OperatorVersion < 1 || Node.Configuration.Num() > 65536
				|| ConfigurationBytes > 1048576 || !Node.Id.IsValid() || Nodes.Contains(Node.Id)
				|| Node.Pins.Num() > 64)
			{
				return FailAt(TEXT("Invalid or duplicate node ID or pin count"),
					Node.Id.IsValid() && !Nodes.Contains(Node.Id) ? Node.Id : FGuid());
			}
			Nodes.Add(Node.Id, &Node);
			for (const FVoxelLandGraphPinRecord& Pin : Node.Pins)
			{
				if (!Pin.Id.IsValid() || Pins.Contains(Pin.Id) || Pin.bInvalidDefault
					|| !FMath::IsFinite(Pin.FloatDefault)
					|| (Pin.Type != EVoxelLandGraphValueType::Float && Pin.Type != EVoxelLandGraphValueType::Int32
						&& Pin.Type != EVoxelLandGraphValueType::Bool))
				{
					return FailAt(TEXT("Invalid or duplicate pin ID or default"), Node.Id,
						Pin.Id.IsValid() && !Pins.Contains(Pin.Id) ? Pin.Id : FGuid());
				}
				Pins.Add(Pin.Id, &Pin);
				Owners.Add(Pin.Id, Node.Id);
			}
		}
		return true;
	}

	bool IndexLinks()
	{
		TMap<FGuid, int32> Incoming;
		TMap<FGuid, TArray<FGuid>> GraphEdges;
		for (const FVoxelLandGraphNodeRecord& Node : Source.Nodes)
		{
			Incoming.Add(Node.Id, 0);
		}
		for (const FVoxelLandGraphLinkRecord& Link : Source.Links)
		{
			const FVoxelLandGraphPinRecord* const* From = Pins.Find(Link.SourcePin);
			const FVoxelLandGraphPinRecord* const* To = Pins.Find(Link.TargetPin);
			const FGuid* FromOwner = Owners.Find(Link.SourcePin);
			const FGuid* ToOwner = Owners.Find(Link.TargetPin);
			if (!From || !To || !FromOwner || !ToOwner || *FromOwner != Link.SourceNode || *ToOwner != Link.TargetNode
				|| !(*From)->bOutput || (*To)->bOutput || (*From)->Type != (*To)->Type
				|| InputLinks.Contains(Link.TargetPin))
			{
				if (!From || !FromOwner || *FromOwner != Link.SourceNode || !(*From)->bOutput)
				{
					if (From && FromOwner)
					{
						return FailAt(TEXT("Invalid source pin or ownership"), *FromOwner, Link.SourcePin);
					}
					return FailAt(
						TEXT("Missing source pin"), Nodes.Contains(Link.SourceNode) ? Link.SourceNode : FGuid());
				}
				if (!To || !ToOwner || *ToOwner != Link.TargetNode || (*To)->bOutput)
				{
					if (To && ToOwner)
					{
						return FailAt(TEXT("Invalid target pin or ownership"), *ToOwner, Link.TargetPin);
					}
					return FailAt(
						TEXT("Missing target pin"), Nodes.Contains(Link.TargetNode) ? Link.TargetNode : FGuid());
				}
				return FailAt(TEXT("Invalid link type or multiple sources for input"), Link.TargetNode, Link.TargetPin);
			}
			InputLinks.Add(Link.TargetPin, Link.SourceNode);
			GraphEdges.FindOrAdd(Link.SourceNode).Add(Link.TargetNode);
			++Incoming.FindChecked(Link.TargetNode);
		}
		TArray<FGuid> Ready;
		for (const FVoxelLandGraphNodeRecord& Node : Source.Nodes)
		{
			if (Incoming.FindChecked(Node.Id) == 0)
			{
				Ready.Add(Node.Id);
			}
		}
		for (int32 Cursor = 0; Cursor < Ready.Num(); ++Cursor)
		{
			if (const TArray<FGuid>* Targets = GraphEdges.Find(Ready[Cursor]))
			{
				for (const FGuid& Target : *Targets)
				{
					if (--Incoming.FindChecked(Target) == 0)
					{
						Ready.Add(Target);
					}
				}
			}
		}
		if (Ready.Num() != Source.Nodes.Num())
		{
			FGuid Node, Pin;
			FVoxelLandGraphCycle::Find(Source.Nodes, Source.Links, Node, Pin);
			return FailAt(TEXT("Graph contains a cycle"), Node, Pin);
		}
		return true;
	}

	bool CompileRoot(uint16& Result)
	{
		TGuardValue<const FVoxelLandGraphNodeRecord*> OutputContext(CurrentNode, Output);
		const FGuid* UpstreamId = InputLinks.Find(Output->Pins[0].Id);
		const FVoxelLandGraphNodeRecord* const* Upstream = UpstreamId ? Nodes.Find(*UpstreamId) : nullptr;
		if (Upstream && (*Upstream)->OperatorId != VoxelLandBuiltinNodeId(EVoxelLandBuiltinNode::SmoothDensity))
		{
			const auto Provider = Providers.Resolve((*Upstream)->OperatorId, (*Upstream)->OperatorVersion);
			if (Provider && Provider->Stage() == EVoxelLandOperatorStage::OneCellFilter)
			{
				TGuardValue<const FVoxelLandGraphNodeRecord*> FilterContext(CurrentNode, *Upstream);
				return CompileExternalFilter(**Upstream, *Provider, Result);
			}
		}
		if (!Upstream || (*Upstream)->OperatorId != VoxelLandBuiltinNodeId(EVoxelLandBuiltinNode::SmoothDensity))
		{
			return CompileInput(*Output, 0, Result);
		}
		const FVoxelLandGraphNodeRecord& Filter = **Upstream;
		TGuardValue<const FVoxelLandGraphNodeRecord*> FilterContext(CurrentNode, &Filter);
		if (!CheckBuiltinPins(Filter))
		{
			return false;
		}
		int32 Consumers = 0;
		for (const FVoxelLandGraphLinkRecord& Link : Source.Links)
		{
			if (Link.SourcePin == Filter.Pins.Last().Id)
			{
				++Consumers;
			}
		}
		if (Consumers != 1)
		{
			return FailAt(
				TEXT("Smooth Density output must feed only Density Output"), Filter.Id, Filter.Pins.Last().Id);
		}
		if (!NoiseSource(Filter, 1, EVoxelLandGraphValueType::Bool, Draft.SmoothFilter.Enabled)
			|| !NoiseSource(Filter, 2, EVoxelLandGraphValueType::Float, Draft.SmoothFilter.Strength))
		{
			return false;
		}
		const FVoxelLandValueSource& Strength = Draft.SmoothFilter.Strength;
		const float DefaultStrength =
			Strength.bParameter ? Draft.Parameters[Strength.Index].FloatDefault : Draft.Constants[Strength.Index];
		if (DefaultStrength < 0.f || DefaultStrength > 1.f)
		{
			return FailAt(TEXT("Smooth strength must be in [0,1]"), Filter.Id, Filter.Pins[2].Id);
		}
		Draft.SmoothFilter.bPresent = true;
		return CompileInput(Filter, 0, Result);
	}

	bool CompileExternalFilter(
		const FVoxelLandGraphNodeRecord& Node, const IVoxelLandOperator& Provider, uint16& Result)
	{
		if (Provider.InputTypes() != TArray<EVoxelLandGraphValueType>{EVoxelLandGraphValueType::Float}
			|| Provider.OutputType() != EVoxelLandGraphValueType::Float || Node.Pins.Num() != 2
			|| Node.Configuration.Num() > 65536 || Provider.MaximumWorkerBytes() > 1048576)
		{
			return Fail(TEXT("Invalid filter signature or limits"));
		}
		for (int32 Index = 0; Index < 2; ++Index)
		{
			if (Node.Pins[Index].bOutput != (Index == 1) || Node.Pins[Index].Type != EVoxelLandGraphValueType::Float)
			{
				return FailAt(TEXT("Invalid filter pin direction or type"), Node.Id, Node.Pins[Index].Id);
			}
		}
		int32 Consumers = 0;
		for (const FVoxelLandGraphLinkRecord& Link : Source.Links)
		{
			if (Link.SourcePin == Node.Pins[1].Id)
			{
				++Consumers;
			}
		}
		if (Consumers != 1)
		{
			return FailAt(TEXT("Filter output must feed only Density Output"), Node.Id, Node.Pins[1].Id);
		}
		FString ProviderError;
		if (!Provider.ValidateConfiguration(Node.Configuration, ProviderError))
		{
			const FString Message = FString::Printf(TEXT("Filter configuration invalid: %s"), *ProviderError);
			return FailAt(*Message, Node.Id);
		}
		if (!CompileInput(Node, 0, Result))
		{
			return false;
		}
		FVoxelLandSeedSlot Seed;
		Seed.NodeId = Node.Id;
		Seed.Salt = uint32(Node.SeedSalt);
		FVoxelLandExternalOperator Operator;
		Operator.Id = Node.OperatorId;
		Operator.Version = Node.OperatorVersion;
		Operator.Stage = EVoxelLandOperatorStage::OneCellFilter;
		Operator.Inputs = Provider.InputTypes();
		Operator.Output = Provider.OutputType();
		Operator.Configuration = Node.Configuration;
		Operator.SeedSlot = Draft.Seeds.Add(Seed);
		Draft.ExternalFilterIndex = Draft.ExternalOperators.Add(MoveTemp(Operator));
		return true;
	}

	bool FindOutput()
	{
		for (const FVoxelLandGraphNodeRecord& Node : Source.Nodes)
		{
			if (Node.OperatorId == VoxelLandBuiltinNodeId(EVoxelLandBuiltinNode::DensityOutput))
			{
				if (Output)
				{
					return FailAt(TEXT("Exactly one valid density output is required"), Node.Id);
				}
				if (!CheckBuiltinPins(Node))
				{
					return false;
				}
				Output = &Node;
			}
		}
		return Output != nullptr || Fail(TEXT("Missing density output"));
	}

	bool CheckBuiltinPins(const FVoxelLandGraphNodeRecord& Node)
	{
		const EVoxelLandBuiltinNode Kind = static_cast<EVoxelLandBuiltinNode>(Node.OperatorId.D);
		const bool bVariadic = Kind == EVoxelLandBuiltinNode::Add || Kind == EVoxelLandBuiltinNode::Multiply;
		const int32 VariadicInputs = bVariadic ? Node.Pins.Num() - 1 : 2;
		FVoxelLandBuiltinNodeSpec Spec;
		if (!FVoxelLandBuiltinCatalog::Find(Kind, Spec, VariadicInputs) || Node.OperatorVersion != Spec.OperatorVersion
			|| Node.Configuration.Num() != 0 || Node.Pins.Num() != Spec.Pins.Num())
		{
			return FailAt(TEXT("Invalid builtin version, configuration or pin count"), Node.Id);
		}
		for (int32 Index = 0; Index < Node.Pins.Num(); ++Index)
		{
			if (Node.Pins[Index].Type != Spec.Pins[Index].Type || Node.Pins[Index].bOutput != Spec.Pins[Index].bOutput)
			{
				return FailAt(TEXT("Invalid builtin pin direction or type"), Node.Id, Node.Pins[Index].Id);
			}
		}
		return true;
	}

	bool Emit(EVoxelLandOpcode Op, uint16 A, uint16 B, uint16 C, uint32 Imm, uint16& Result)
	{
		if (Draft.RegisterCount >= MaxInstructions || Draft.Instructions.Num() >= MaxInstructions)
		{
			return Fail(TEXT("Graph exceeds VM register or instruction limit"));
		}
		const float* KA = KnownConstants.Find(A);
		const float* KB = KnownConstants.Find(B);
		const float* KC = KnownConstants.Find(C);
		if (Op == EVoxelLandOpcode::Divide && KB && *KB == 0.f)
		{
			return FailAt(TEXT("Division by a constant zero"), CurrentNode ? CurrentNode->Id : FGuid(),
				CurrentNode && CurrentNode->Pins.Num() > 1 ? CurrentNode->Pins[1].Id : FGuid());
		}
		if (Op == EVoxelLandOpcode::Clamp && KB && KC && *KB > *KC)
		{
			return Fail(TEXT("Clamp minimum exceeds maximum"));
		}
		if (Op == EVoxelLandOpcode::Power && KA && KB)
		{
			float Ignored = 0.f;
			if (!FVoxelLandPower::Evaluate(*KA, *KB, Ignored))
			{
				return Fail(TEXT("Invalid constant power domain or overflow"));
			}
		}
		float Folded = 0.f;
		bool bFold = KA != nullptr;
		switch (Op)
		{
			case EVoxelLandOpcode::Abs:
				if (bFold)
				{
					Folded = FMath::Abs(*KA);
				}
				break;
			case EVoxelLandOpcode::Negate:
				if (bFold)
				{
					Folded = -*KA;
				}
				break;
			case EVoxelLandOpcode::Add:
				bFold = bFold && KB;
				if (bFold)
				{
					Folded = *KA + *KB;
				}
				break;
			case EVoxelLandOpcode::Subtract:
				bFold = bFold && KB;
				if (bFold)
				{
					Folded = *KA - *KB;
				}
				break;
			case EVoxelLandOpcode::Multiply:
				bFold = bFold && KB;
				if (bFold)
				{
					Folded = *KA * *KB;
				}
				break;
			case EVoxelLandOpcode::Divide:
				bFold = bFold && KB;
				if (bFold)
				{
					Folded = *KA / *KB;
				}
				break;
			case EVoxelLandOpcode::Min:
			case EVoxelLandOpcode::Union:
				bFold = bFold && KB;
				if (bFold)
				{
					Folded = FMath::Min(*KA, *KB);
				}
				break;
			case EVoxelLandOpcode::Max:
			case EVoxelLandOpcode::Intersect:
				bFold = bFold && KB;
				if (bFold)
				{
					Folded = FMath::Max(*KA, *KB);
				}
				break;
			case EVoxelLandOpcode::SubtractShape:
				bFold = bFold && KB;
				if (bFold)
				{
					Folded = FMath::Max(*KA, -*KB);
				}
				break;
			case EVoxelLandOpcode::Clamp:
				bFold = bFold && KB && KC;
				if (bFold)
				{
					Folded = FMath::Clamp(*KA, *KB, *KC);
				}
				break;
			default:
				bFold = false;
				break;
		}
		if (bFold)
		{
			if (!FMath::IsFinite(Folded))
			{
				return Fail(TEXT("Non-finite constant expression"));
			}
			return Constant(Folded, Result);
		}
		FVoxelLandInstruction Instruction;
		Instruction.Op = Op;
		Instruction.Dst = uint16(Draft.RegisterCount++);
		Instruction.A = A;
		Instruction.B = B;
		Instruction.C = C;
		Instruction.Imm = Imm;
		Result = Instruction.Dst;
		AddInstruction(Instruction, CurrentNode ? CurrentNode->Id : FGuid());
		if (Op == EVoxelLandOpcode::LoadConstant)
		{
			KnownConstants.Add(Result, Draft.Constants[Imm]);
		}
		return true;
	}

	bool Constant(float Value, uint16& Result)
	{
		if (!FMath::IsFinite(Value))
		{
			return Fail(TEXT("Non-finite default value"));
		}
		const uint32 Index = uint32(Draft.Constants.Add(Value));
		return Emit(EVoxelLandOpcode::LoadConstant, 0, 0, 0, Index, Result);
	}

	bool CompileInput(const FVoxelLandGraphNodeRecord& Node, int32 Index, uint16& Result)
	{
		const FVoxelLandGraphPinRecord& Pin = Node.Pins[Index];
		TGuardValue<FGuid> PinContext(ActivePin, Pin.Id);
		if (const FGuid* NodeId = InputLinks.Find(Pin.Id))
		{
			const FVoxelLandGraphNodeRecord* const* Upstream = Nodes.Find(*NodeId);
			return Upstream && CompileNode(**Upstream, Result);
		}
		if (Pin.Type == EVoxelLandGraphValueType::Int32)
		{
			return Emit(EVoxelLandOpcode::LoadIntConstant, 0, 0, 0, uint32(Pin.IntDefault), Result);
		}
		if (Pin.Type == EVoxelLandGraphValueType::Bool)
		{
			return Emit(EVoxelLandOpcode::LoadBoolConstant, 0, 0, 0, Pin.BoolDefault ? 1u : 0u, Result);
		}
		return Constant(Pin.FloatDefault, Result);
	}

	bool FindOrAddParameter(const FVoxelLandGraphNodeRecord& Node, EVoxelLandGraphValueType Type, int32& Slot)
	{
		const FVoxelLandGraphParameter* const* Parameter = ParameterById.Find(Node.ParameterId);
		if (!Parameter || (*Parameter)->Type != Type || !FMath::IsFinite((*Parameter)->FloatDefault))
		{
			return Fail(TEXT("Float parameter reference is missing or has the wrong type"));
		}
		int32* Existing = ParameterSlots.Find(Node.ParameterId);
		Slot = INDEX_NONE;
		if (Existing)
		{
			Slot = *Existing;
		}
		else
		{
			FVoxelLandParameterSlot P;
			P.Name = (*Parameter)->Name.ToLower();
			P.Type = Type == EVoxelLandGraphValueType::Float ? EVoxelLandParameterType::Float
				   : Type == EVoxelLandGraphValueType::Int32 ? EVoxelLandParameterType::Int32
															 : EVoxelLandParameterType::Bool;
			P.IntDefault =
				Type == EVoxelLandGraphValueType::Bool ? ((*Parameter)->BoolDefault ? 1 : 0) : (*Parameter)->IntDefault;
			P.FloatDefault = (*Parameter)->FloatDefault;
			Slot = Draft.Parameters.Add(P);
			ParameterSlots.Add(Node.ParameterId, Slot);
		}
		return true;
	}

	bool CompileParameter(const FVoxelLandGraphNodeRecord& Node, EVoxelLandGraphValueType Type, uint16& Result)
	{
		int32 Slot = INDEX_NONE;
		if (!FindOrAddParameter(Node, Type, Slot))
		{
			return false;
		}
		const EVoxelLandOpcode Op = Type == EVoxelLandGraphValueType::Float ? EVoxelLandOpcode::LoadFloatParameter
								  : Type == EVoxelLandGraphValueType::Int32 ? EVoxelLandOpcode::LoadIntParameter
																			: EVoxelLandOpcode::LoadBoolParameter;
		return Emit(Op, 0, 0, 0, uint32(Slot), Result);
	}

	bool CompileNode(const FVoxelLandGraphNodeRecord& Node, uint16& Result)
	{
		TGuardValue<const FVoxelLandGraphNodeRecord*> NodeContext(CurrentNode, &Node);
		TGuardValue<FGuid> PinContext(ActivePin, FGuid());
		if (const uint16* Existing = Results.Find(Node.Id))
		{
			Result = *Existing;
			return true;
		}
		if (Visiting.Contains(Node.Id) || Visiting.Num() >= 1024)
		{
			return Fail(TEXT("Graph contains a cycle or exceeds maximum depth"));
		}
		Visiting.Add(Node.Id);
		if (!CompileBuiltin(Node, Result))
		{
			if (!Diagnostic.NodeId.IsValid())
			{
				Diagnostic.NodeId = Node.Id;
			}
			return false;
		}
		Visiting.Remove(Node.Id);
		Results.Add(Node.Id, Result);
		return true;
	}

	bool CompileBuiltin(const FVoxelLandGraphNodeRecord& Node, uint16& Result);
	bool CompileTypedBuiltin(const FVoxelLandGraphNodeRecord& Node, EVoxelLandBuiltinNode Kind, uint16& Result);
	bool CompileShape(const FVoxelLandGraphNodeRecord& Node, EVoxelLandBuiltinNode Kind, uint16& Result);
	bool CompileFloatBuiltin(const FVoxelLandGraphNodeRecord& Node, EVoxelLandBuiltinNode Kind, uint16& Result);
	bool CompileNoise(const FVoxelLandGraphNodeRecord& Node, EVoxelLandBuiltinNode Kind, uint16& Result);
	bool ValidateNoiseDefaults(
		const FVoxelLandGraphNodeRecord& Node, int32 Dimensions, const FVoxelLandNoiseConfig& Config);
	bool CompileExternal(const FVoxelLandGraphNodeRecord& Node, uint16& Result);
	bool NoiseSource(
		const FVoxelLandGraphNodeRecord& Node, int32 Index, EVoxelLandGraphValueType Type, FVoxelLandValueSource& Out);

	const FVoxelLandGraphSnapshot& Source;
	const FVoxelLandOperatorRegistrySnapshot& Providers;
	FVoxelLandGraphDiagnostic& Diagnostic;
	FString& Error;
	TArray<FGuid>* InstructionNodes;
	const FVoxelLandGraphNodeRecord* CurrentNode = nullptr;
	FGuid ActivePin;
	FVoxelLandProgramDraft Draft;
	const FVoxelLandGraphNodeRecord* Output = nullptr;
	TMap<FGuid, const FVoxelLandGraphNodeRecord*> Nodes;
	TMap<FGuid, const FVoxelLandGraphPinRecord*> Pins;
	TMap<FGuid, FGuid> Owners;
	TMap<FGuid, FGuid> InputLinks;
	TMap<FGuid, const FVoxelLandGraphParameter*> ParameterById;
	TMap<FGuid, int32> ParameterSlots;
	TMap<FGuid, uint16> Results;
	TMap<uint16, float> KnownConstants;
	TSet<FGuid> Visiting;
};

bool FVoxelLandGraphCompilePass::CompileBuiltin(const FVoxelLandGraphNodeRecord& Node, uint16& Result)
{
	if (Node.OperatorId.A != 0x564c4d33 || Node.OperatorId.B != 1 || Node.OperatorId.C != 0)
	{
		return CompileExternal(Node, Result);
	}
	const EVoxelLandBuiltinNode Kind = EVoxelLandBuiltinNode(Node.OperatorId.D);
	if (Kind == EVoxelLandBuiltinNode::RerouteFloat || Kind == EVoxelLandBuiltinNode::RerouteInt
		|| Kind == EVoxelLandBuiltinNode::RerouteBool)
	{
		return CheckBuiltinPins(Node) && CompileInput(Node, 0, Result);
	}
	if (Kind == EVoxelLandBuiltinNode::PositionX || Kind == EVoxelLandBuiltinNode::PositionY
		|| Kind == EVoxelLandBuiltinNode::PositionZ)
	{
		if (!CheckBuiltinPins(Node))
		{
			return false;
		}
		const EVoxelLandOpcode Op = Kind == EVoxelLandBuiltinNode::PositionX ? EVoxelLandOpcode::LoadX
								  : Kind == EVoxelLandBuiltinNode::PositionY ? EVoxelLandOpcode::LoadY
																			 : EVoxelLandOpcode::LoadZ;
		return Emit(Op, 0, 0, 0, 0, Result);
	}
	if (Kind == EVoxelLandBuiltinNode::FloatParameter)
	{
		return CheckBuiltinPins(Node) && CompileParameter(Node, EVoxelLandGraphValueType::Float, Result);
	}
	if (Kind == EVoxelLandBuiltinNode::FloatConstant)
	{
		if (!CheckBuiltinPins(Node) || InputLinks.Contains(Node.Pins[0].Id))
		{
			return Fail(TEXT("Float constant requires one unconnected value pin"));
		}
		return Constant(Node.Pins[0].FloatDefault, Result);
	}
	FVoxelLandBuiltinNodeSpec BuiltinSpec;
	const bool bKnownBuiltin = FVoxelLandBuiltinCatalog::Find(Kind, BuiltinSpec);
	if (bKnownBuiltin && BuiltinSpec.bTypedCompilerDispatch)
	{
		return CompileTypedBuiltin(Node, Kind, Result);
	}
	if (Kind == EVoxelLandBuiltinNode::Plane || Kind == EVoxelLandBuiltinNode::Sphere
		|| Kind == EVoxelLandBuiltinNode::Box)
	{
		return CompileShape(Node, Kind, Result);
	}
	if (bKnownBuiltin && BuiltinSpec.bNoise)
	{
		return CompileNoise(Node, Kind, Result);
	}
	return CompileFloatBuiltin(Node, Kind, Result);
}

bool FVoxelLandGraphCompilePass::CompileExternal(const FVoxelLandGraphNodeRecord& Node, uint16& Result)
{
	TSharedPtr<const IVoxelLandOperator, ESPMode::ThreadSafe> Provider =
		Providers.Resolve(Node.OperatorId, Node.OperatorVersion);
	if (!Provider)
	{
		return Fail(TEXT("Missing external operator provider or incompatible version"));
	}
	if (Provider->Stage() != EVoxelLandOperatorStage::Sample)
	{
		return Fail(TEXT("Filter must be directly before Density Output and cannot be chained"));
	}
	const TArray<EVoxelLandGraphValueType> Inputs = Provider->InputTypes();
	if (Inputs.Num() < 1 || Inputs.Num() > 3 || Provider->MaximumWorkerBytes() > 1048576
		|| Node.Configuration.Num() > 65536 || Node.Pins.Num() != Inputs.Num() + 1)
	{
		return Fail(TEXT("External operator exceeds its signature or memory limits"));
	}
	FString ProviderError;
	if (!Provider->ValidateConfiguration(Node.Configuration, ProviderError))
	{
		Error = FString::Printf(TEXT("External operator configuration invalid: %s"), *ProviderError);
		return false;
	}
	uint16 R[3] = {};
	for (int32 Index = 0; Index < Inputs.Num(); ++Index)
	{
		if (Node.Pins[Index].bOutput || Node.Pins[Index].Type != Inputs[Index])
		{
			return FailAt(TEXT("External operator input signature mismatch"), Node.Id, Node.Pins[Index].Id);
		}
		if (!CompileInput(Node, Index, R[Index]))
		{
			return false;
		}
	}
	if (!Node.Pins.Last().bOutput || Node.Pins.Last().Type != Provider->OutputType())
	{
		return FailAt(TEXT("External operator output signature mismatch"), Node.Id, Node.Pins.Last().Id);
	}
	FVoxelLandSeedSlot Seed;
	Seed.NodeId = Node.Id;
	Seed.Salt = uint32(Node.SeedSalt);
	FVoxelLandExternalOperator Operator;
	Operator.Id = Node.OperatorId;
	Operator.Version = Node.OperatorVersion;
	Operator.Inputs = Inputs;
	Operator.Output = Provider->OutputType();
	Operator.Configuration = Node.Configuration;
	Operator.SeedSlot = Draft.Seeds.Add(Seed);
	const uint32 Index = uint32(Draft.ExternalOperators.Add(MoveTemp(Operator)));
	return Emit(EVoxelLandOpcode::ExternalOperator, R[0], R[1], R[2], Index, Result);
}

bool FVoxelLandGraphCompilePass::NoiseSource(
	const FVoxelLandGraphNodeRecord& Node, int32 Index, EVoxelLandGraphValueType Type, FVoxelLandValueSource& Out)
{
	FVoxelLandBuiltinNodeSpec Spec;
	if (!FVoxelLandBuiltinCatalog::Find(EVoxelLandBuiltinNode(Node.OperatorId.D), Spec)
		|| !Spec.Pins.IsValidIndex(Index) || !Spec.Pins[Index].bUniform || Spec.Pins[Index].Type != Type)
	{
		return FailAt(TEXT("Invalid builtin uniform pin contract"), Node.Id);
	}
	const FVoxelLandGraphPinRecord& Pin = Node.Pins[Index];
	TGuardValue<FGuid> PinContext(ActivePin, Pin.Id);
	if (const FGuid* UpstreamId = InputLinks.Find(Pin.Id))
	{
		const FVoxelLandGraphNodeRecord* const* Upstream = Nodes.Find(*UpstreamId);
		const EVoxelLandBuiltinNode Expected =
			Type == EVoxelLandGraphValueType::Float  ? EVoxelLandBuiltinNode::FloatParameter
			: Type == EVoxelLandGraphValueType::Bool ? EVoxelLandBuiltinNode::BoolParameter
													 : EVoxelLandBuiltinNode::IntParameter;
		if (!Upstream || (*Upstream)->OperatorId != VoxelLandBuiltinNodeId(Expected))
		{
			return Fail(TEXT("Uniform settings must be unconnected defaults or direct parameter references"));
		}
		int32 Slot = INDEX_NONE;
		if (!CheckBuiltinPins(**Upstream))
		{
			return false;
		}
		{
			TGuardValue<const FVoxelLandGraphNodeRecord*> NodeContext(CurrentNode, *Upstream);
			TGuardValue<FGuid> ParameterPinContext(ActivePin, FGuid());
			if (!FindOrAddParameter(**Upstream, Type, Slot))
			{
				return false;
			}
		}
		Out.bParameter = true;
		Out.Index = uint32(Slot);
		return true;
	}
	const float Value = Type == EVoxelLandGraphValueType::Float ? Pin.FloatDefault
					  : Type == EVoxelLandGraphValueType::Bool  ? (Pin.BoolDefault ? 1.f : 0.f)
																: float(Pin.IntDefault);
	if (!FMath::IsFinite(Value))
	{
		return Fail(TEXT("Invalid noise setting"));
	}
	Out.Index = uint32(Draft.Constants.Add(Value));
	return true;
}

bool FVoxelLandGraphCompilePass::ValidateNoiseDefaults(
	const FVoxelLandGraphNodeRecord& Node, int32 Dimensions, const FVoxelLandNoiseConfig& Config)
{
	const auto FloatDefault = [this](const FVoxelLandValueSource& Source) {
		return Source.bParameter ? Draft.Parameters[Source.Index].FloatDefault : Draft.Constants[Source.Index];
	};
	const float Frequency = FloatDefault(Config.Frequency);
	const int32 Octaves = Config.Octaves.bParameter ? Draft.Parameters[Config.Octaves.Index].IntDefault
													: Node.Pins[Dimensions + 1].IntDefault;
	const float Gain = FloatDefault(Config.Gain);
	const float Lacunarity = FloatDefault(Config.Lacunarity);
	if (!FMath::IsFinite(Frequency) || Frequency <= 0.f)
	{
		return FailAt(TEXT("Noise frequency must be finite and positive"), Node.Id, Node.Pins[Dimensions].Id);
	}
	if (Octaves < 1 || Octaves > 16)
	{
		return FailAt(TEXT("Noise octaves must be in [1,16]"), Node.Id, Node.Pins[Dimensions + 1].Id);
	}
	if (!FMath::IsFinite(Gain) || Gain < 0.f || Gain > 1.f)
	{
		return FailAt(TEXT("Noise gain must be finite and in [0,1]"), Node.Id, Node.Pins[Dimensions + 2].Id);
	}
	if (!FMath::IsFinite(Lacunarity) || Lacunarity < 1.f)
	{
		return FailAt(TEXT("Noise lacunarity must be finite and at least 1"), Node.Id, Node.Pins[Dimensions + 3].Id);
	}
	return true;
}

bool FVoxelLandGraphCompilePass::CompileNoise(
	const FVoxelLandGraphNodeRecord& Node, EVoxelLandBuiltinNode Kind, uint16& Result)
{
	const bool b3D = Kind == EVoxelLandBuiltinNode::Perlin3 || Kind == EVoxelLandBuiltinNode::Fbm3
				  || Kind == EVoxelLandBuiltinNode::Value3 || Kind == EVoxelLandBuiltinNode::Simplex3;
	const int32 Dimensions = b3D ? 3 : 2;
	if (!CheckBuiltinPins(Node))
	{
		return false;
	}
	uint16 Coordinates[3] = {};
	for (int32 Index = 0; Index < Dimensions; ++Index)
	{
		if (!CompileInput(Node, Index, Coordinates[Index]))
		{
			return false;
		}
	}
	FVoxelLandNoiseConfig Config;
	if (!NoiseSource(Node, Dimensions, EVoxelLandGraphValueType::Float, Config.Frequency)
		|| !NoiseSource(Node, Dimensions + 1, EVoxelLandGraphValueType::Int32, Config.Octaves)
		|| !NoiseSource(Node, Dimensions + 2, EVoxelLandGraphValueType::Float, Config.Gain)
		|| !NoiseSource(Node, Dimensions + 3, EVoxelLandGraphValueType::Float, Config.Lacunarity))
	{
		return false;
	}
	if (!ValidateNoiseDefaults(Node, Dimensions, Config))
	{
		return false;
	}
	FVoxelLandSeedSlot Seed;
	Seed.NodeId = Node.Id;
	Seed.Salt = uint32(Node.SeedSalt);
	Config.SeedSlot = uint32(Draft.Seeds.Add(Seed));
	const uint32 ConfigIndex = uint32(Draft.Noise.Add(Config));
	EVoxelLandOpcode Op;
	switch (Kind)
	{
		case EVoxelLandBuiltinNode::Perlin2:
			Op = EVoxelLandOpcode::Perlin2;
			break;
		case EVoxelLandBuiltinNode::Perlin3:
			Op = EVoxelLandOpcode::Perlin3;
			break;
		case EVoxelLandBuiltinNode::Fbm2:
			Op = EVoxelLandOpcode::Fbm2;
			break;
		case EVoxelLandBuiltinNode::Fbm3:
			Op = EVoxelLandOpcode::Fbm3;
			break;
		case EVoxelLandBuiltinNode::Value2:
			Op = EVoxelLandOpcode::Value2;
			break;
		case EVoxelLandBuiltinNode::Value3:
			Op = EVoxelLandOpcode::Value3;
			break;
		case EVoxelLandBuiltinNode::Simplex2:
			Op = EVoxelLandOpcode::Simplex2;
			break;
		case EVoxelLandBuiltinNode::Simplex3:
			Op = EVoxelLandOpcode::Simplex3;
			break;
		default:
			return Fail(TEXT("Unknown noise operator"));
	}
	return Emit(Op, Coordinates[0], Coordinates[1], Coordinates[2], ConfigIndex, Result);
}

bool FVoxelLandGraphCompilePass::CompileFloatBuiltin(
	const FVoxelLandGraphNodeRecord& Node, EVoxelLandBuiltinNode Kind, uint16& Result)
{
	if (Kind == EVoxelLandBuiltinNode::HeightToDensity)
	{
		uint16 Z = 0, Height = 0;
		return CheckBuiltinPins(Node) && Emit(EVoxelLandOpcode::LoadZ, 0, 0, 0, 0, Z) && CompileInput(Node, 0, Height)
			&& Emit(EVoxelLandOpcode::Subtract, Z, Height, 0, 0, Result);
	}
	if (Kind == EVoxelLandBuiltinNode::Abs || Kind == EVoxelLandBuiltinNode::Negate)
	{
		uint16 A = 0;
		const EVoxelLandOpcode Op =
			Kind == EVoxelLandBuiltinNode::Abs ? EVoxelLandOpcode::Abs : EVoxelLandOpcode::Negate;
		return CheckBuiltinPins(Node) && CompileInput(Node, 0, A) && Emit(Op, A, 0, 0, 0, Result);
	}
	if (Kind == EVoxelLandBuiltinNode::Saturate)
	{
		uint16 A = 0, Zero = 0, One = 0, Lower = 0;
		return CheckBuiltinPins(Node) && CompileInput(Node, 0, A) && Constant(0.f, Zero) && Constant(1.f, One)
			&& Emit(EVoxelLandOpcode::Max, A, Zero, 0, 0, Lower)
			&& Emit(EVoxelLandOpcode::Min, Lower, One, 0, 0, Result);
	}
	if (Kind == EVoxelLandBuiltinNode::Clamp)
	{
		if (!CheckBuiltinPins(Node))
		{
			return false;
		}
		if (!InputLinks.Contains(Node.Pins[1].Id) && !InputLinks.Contains(Node.Pins[2].Id)
			&& Node.Pins[1].FloatDefault > Node.Pins[2].FloatDefault)
		{
			return Fail(TEXT("Clamp minimum exceeds maximum"));
		}
		uint16 A = 0, Lo = 0, Hi = 0;
		return CompileInput(Node, 0, A) && CompileInput(Node, 1, Lo) && CompileInput(Node, 2, Hi)
			&& Emit(EVoxelLandOpcode::Clamp, A, Lo, Hi, 0, Result);
	}
	if (Kind == EVoxelLandBuiltinNode::Lerp)
	{
		uint16 A = 0, B = 0, Alpha = 0, Difference = 0, Scaled = 0;
		return CheckBuiltinPins(Node) && CompileInput(Node, 0, A) && CompileInput(Node, 1, B)
			&& CompileInput(Node, 2, Alpha) && Emit(EVoxelLandOpcode::Subtract, B, A, 0, 0, Difference)
			&& Emit(EVoxelLandOpcode::Multiply, Difference, Alpha, 0, 0, Scaled)
			&& Emit(EVoxelLandOpcode::Add, A, Scaled, 0, 0, Result);
	}
	EVoxelLandOpcode Op;
	switch (Kind)
	{
		case EVoxelLandBuiltinNode::Add:
			Op = EVoxelLandOpcode::Add;
			break;
		case EVoxelLandBuiltinNode::Subtract:
			Op = EVoxelLandOpcode::Subtract;
			break;
		case EVoxelLandBuiltinNode::Multiply:
			Op = EVoxelLandOpcode::Multiply;
			break;
		case EVoxelLandBuiltinNode::Divide:
			Op = EVoxelLandOpcode::Divide;
			break;
		case EVoxelLandBuiltinNode::Min:
			Op = EVoxelLandOpcode::Min;
			break;
		case EVoxelLandBuiltinNode::Max:
			Op = EVoxelLandOpcode::Max;
			break;
		case EVoxelLandBuiltinNode::Union:
			Op = EVoxelLandOpcode::Union;
			break;
		case EVoxelLandBuiltinNode::SubtractShape:
			Op = EVoxelLandOpcode::SubtractShape;
			break;
		case EVoxelLandBuiltinNode::Intersect:
			Op = EVoxelLandOpcode::Intersect;
			break;
		default:
			return Fail(TEXT("Unsupported builtin operator"));
	}
	const bool bVariadic = Kind == EVoxelLandBuiltinNode::Add || Kind == EVoxelLandBuiltinNode::Multiply;
	const int32 InputCount = Node.Pins.Num() - 1;
	if (InputCount < 2 || InputCount > (bVariadic ? 32 : 2))
	{
		return Fail(TEXT("Invalid arithmetic input count"));
	}
	if (!CheckBuiltinPins(Node))
	{
		return false;
	}
	if (Kind == EVoxelLandBuiltinNode::Divide && !InputLinks.Contains(Node.Pins[1].Id)
		&& Node.Pins[1].FloatDefault == 0.f)
	{
		return FailAt(TEXT("Division by zero"), Node.Id, Node.Pins[1].Id);
	}
	uint16 Accumulator = 0;
	if (!CompileInput(Node, 0, Accumulator))
	{
		return false;
	}
	for (int32 Index = 1; Index < InputCount; ++Index)
	{
		uint16 Next = 0;
		if (!CompileInput(Node, Index, Next) || !Emit(Op, Accumulator, Next, 0, 0, Accumulator))
		{
			return false;
		}
	}
	Result = Accumulator;
	return true;
}

bool FVoxelLandGraphCompilePass::CompileTypedBuiltin(
	const FVoxelLandGraphNodeRecord& Node, EVoxelLandBuiltinNode Kind, uint16& Result)
{
	if (Kind == EVoxelLandBuiltinNode::IntConstant || Kind == EVoxelLandBuiltinNode::BoolConstant)
	{
		if (!CheckBuiltinPins(Node))
		{
			return false;
		}
		if (InputLinks.Contains(Node.Pins[0].Id))
		{
			return FailAt(TEXT("Typed constant requires an unconnected value"), Node.Id, Node.Pins[0].Id);
		}
		return CompileInput(Node, 0, Result);
	}
	if (Kind == EVoxelLandBuiltinNode::IntParameter || Kind == EVoxelLandBuiltinNode::BoolParameter)
	{
		const EVoxelLandGraphValueType Type = Kind == EVoxelLandBuiltinNode::IntParameter
												? EVoxelLandGraphValueType::Int32
												: EVoxelLandGraphValueType::Bool;
		return CheckBuiltinPins(Node) && CompileParameter(Node, Type, Result);
	}
	if (Kind == EVoxelLandBuiltinNode::IntToFloat)
	{
		uint16 A = 0;
		return CheckBuiltinPins(Node) && CompileInput(Node, 0, A)
			&& Emit(EVoxelLandOpcode::IntToFloat, A, 0, 0, 0, Result);
	}
	if (Kind == EVoxelLandBuiltinNode::Less || Kind == EVoxelLandBuiltinNode::LessEqual
		|| Kind == EVoxelLandBuiltinNode::Equal)
	{
		const EVoxelLandOpcode Op = Kind == EVoxelLandBuiltinNode::Less      ? EVoxelLandOpcode::Less
								  : Kind == EVoxelLandBuiltinNode::LessEqual ? EVoxelLandOpcode::LessEqual
																			 : EVoxelLandOpcode::Equal;
		uint16 A = 0, B = 0;
		return CheckBuiltinPins(Node) && CompileInput(Node, 0, A) && CompileInput(Node, 1, B)
			&& Emit(Op, A, B, 0, 0, Result);
	}
	if (Kind == EVoxelLandBuiltinNode::BoolAnd || Kind == EVoxelLandBuiltinNode::BoolOr
		|| Kind == EVoxelLandBuiltinNode::BoolNot)
	{
		const bool bUnary = Kind == EVoxelLandBuiltinNode::BoolNot;
		const EVoxelLandOpcode Op = bUnary                                 ? EVoxelLandOpcode::BoolNot
								  : Kind == EVoxelLandBuiltinNode::BoolAnd ? EVoxelLandOpcode::BoolAnd
																		   : EVoxelLandOpcode::BoolOr;
		uint16 A = 0, B = 0;
		return CheckBuiltinPins(Node) && CompileInput(Node, 0, A) && (bUnary || CompileInput(Node, 1, B))
			&& Emit(Op, A, B, 0, 0, Result);
	}
	if (Kind == EVoxelLandBuiltinNode::SelectFloat || Kind == EVoxelLandBuiltinNode::SelectInt
		|| Kind == EVoxelLandBuiltinNode::SelectBool)
	{
		const EVoxelLandOpcode Op = Kind == EVoxelLandBuiltinNode::SelectFloat ? EVoxelLandOpcode::SelectFloat
								  : Kind == EVoxelLandBuiltinNode::SelectInt   ? EVoxelLandOpcode::SelectInt
																			   : EVoxelLandOpcode::SelectBool;
		uint16 A = 0, B = 0, C = 0;
		return CheckBuiltinPins(Node) && CompileInput(Node, 0, A) && CompileInput(Node, 1, B)
			&& CompileInput(Node, 2, C) && Emit(Op, A, B, C, 0, Result);
	}
	if (Kind == EVoxelLandBuiltinNode::Power)
	{
		if (!CheckBuiltinPins(Node))
		{
			return false;
		}
		uint16 A = 0, B = 0;
		return CompileInput(Node, 0, A) && CompileInput(Node, 1, B)
			&& Emit(EVoxelLandOpcode::Power, A, B, 0, 0, Result);
	}
	return Fail(TEXT("Unsupported typed operator"));
}

bool FVoxelLandGraphCompilePass::CompileShape(
	const FVoxelLandGraphNodeRecord& Node, EVoxelLandBuiltinNode Kind, uint16& Result)
{
	if (Kind == EVoxelLandBuiltinNode::Plane || Kind == EVoxelLandBuiltinNode::Sphere
		|| Kind == EVoxelLandBuiltinNode::Box)
	{
		const int32 InputCount = Kind == EVoxelLandBuiltinNode::Plane  ? 7
							   : Kind == EVoxelLandBuiltinNode::Sphere ? 4
																	   : 6;
		if (!CheckBuiltinPins(Node))
		{
			return false;
		}
		if (Kind == EVoxelLandBuiltinNode::Sphere && !InputLinks.Contains(Node.Pins[3].Id)
			&& Node.Pins[3].FloatDefault <= 0.f)
		{
			return FailAt(TEXT("Sphere radius must be positive"), Node.Id, Node.Pins[3].Id);
		}
		if (Kind == EVoxelLandBuiltinNode::Box)
		{
			for (int32 Index = 3; Index < 6; ++Index)
			{
				if (!InputLinks.Contains(Node.Pins[Index].Id) && Node.Pins[Index].FloatDefault <= 0.f)
				{
					return FailAt(TEXT("Box extents must be positive"), Node.Id, Node.Pins[Index].Id);
				}
			}
		}
		if (Kind == EVoxelLandBuiltinNode::Plane && !InputLinks.Contains(Node.Pins[3].Id)
			&& !InputLinks.Contains(Node.Pins[4].Id) && !InputLinks.Contains(Node.Pins[5].Id))
		{
			const double X = Node.Pins[3].FloatDefault, Y = Node.Pins[4].FloatDefault, Z = Node.Pins[5].FloatDefault;
			if (FMath::Abs(X * X + Y * Y + Z * Z - 1.0) > 0.00001)
			{
				return Fail(TEXT("Plane normal must be unit length"));
			}
		}
		uint16 R[7] = {};
		for (int32 Index = 0; Index < InputCount; ++Index)
		{
			if (!CompileInput(Node, Index, R[Index]))
			{
				return false;
			}
		}
		if (Kind == EVoxelLandBuiltinNode::Sphere || Kind == EVoxelLandBuiltinNode::Box)
		{
			for (int32 Index = 3; Index < InputCount; ++Index)
			{
				const float* Extent = KnownConstants.Find(R[Index]);
				if (Extent && *Extent <= 0.f)
				{
					return FailAt(TEXT("Shape radius or extent must be positive"), Node.Id, Node.Pins[Index].Id);
				}
			}
		}
		if (Kind == EVoxelLandBuiltinNode::Plane)
		{
			const float* X = KnownConstants.Find(R[3]);
			const float* Y = KnownConstants.Find(R[4]);
			const float* Z = KnownConstants.Find(R[5]);
			if (X && Y && Z && FMath::Abs(double(*X) * *X + double(*Y) * *Y + double(*Z) * *Z - 1.0) > 0.00001)
			{
				return Fail(TEXT("Plane normal must be unit length"));
			}
		}
		const EVoxelLandOpcode Op = Kind == EVoxelLandBuiltinNode::Plane  ? EVoxelLandOpcode::DynamicPlane
								  : Kind == EVoxelLandBuiltinNode::Sphere ? EVoxelLandOpcode::DynamicSphere
																		  : EVoxelLandOpcode::DynamicBox;
		if (!Emit(Op, R[0], R[1], R[2], R[6], Result))
		{
			return false;
		}
		FVoxelLandInstruction& Instruction = Draft.Instructions.Last();
		Instruction.D = R[3];
		Instruction.E = R[4];
		Instruction.F = R[5];
		return true;
	}
	return Fail(TEXT("Unsupported shape operator"));
}

bool FVoxelLandGraphCompiler::Compile(
	const FVoxelLandGraphSnapshot& Snapshot, FVoxelLandProgramDraft& OutDraft, FString& OutError)
{
	const FVoxelLandOperatorRegistrySnapshot Providers = FVoxelLandOperatorRegistry::Snapshot();
	return Compile(Snapshot, Providers, OutDraft, OutError);
}

bool FVoxelLandGraphCompiler::Compile(const FVoxelLandGraphSnapshot& Snapshot,
	const FVoxelLandOperatorRegistrySnapshot& Providers, FVoxelLandProgramDraft& OutDraft, FString& OutError)
{
	FVoxelLandGraphDiagnostic Diagnostic;
	const bool bSuccess = Compile(Snapshot, Providers, OutDraft, Diagnostic);
	OutError = Diagnostic.NodeId.IsValid()
				 ? FString::Printf(TEXT("[node=%s] %s"), *Diagnostic.NodeId.ToString(), *Diagnostic.Message)
				 : Diagnostic.Message;
	return bSuccess;
}

bool FVoxelLandGraphCompiler::Compile(
	const FVoxelLandGraphSnapshot& Snapshot, FVoxelLandProgramDraft& OutDraft, FVoxelLandGraphDiagnostic& OutDiagnostic)
{
	const FVoxelLandOperatorRegistrySnapshot Providers = FVoxelLandOperatorRegistry::Snapshot();
	return Compile(Snapshot, Providers, OutDraft, OutDiagnostic);
}

bool FVoxelLandGraphCompiler::Compile(const FVoxelLandGraphSnapshot& Snapshot,
	const FVoxelLandOperatorRegistrySnapshot& Providers, FVoxelLandProgramDraft& OutDraft,
	FVoxelLandGraphDiagnostic& OutDiagnostic)
{
	OutDraft = FVoxelLandProgramDraft();
	return FVoxelLandGraphCompilePass::CompileSource(Snapshot, Providers, OutDraft, OutDiagnostic, nullptr, nullptr);
}

bool FVoxelLandGraphCompiler::CompileWithInstructionOrigins(const FVoxelLandGraphSnapshot& Snapshot,
	FVoxelLandProgramDraft& OutDraft, TArray<FVoxelLandGraphNodeOrigin>& OutOrigins,
	FVoxelLandGraphDiagnostic& OutDiagnostic)
{
	OutDraft = {};
	OutOrigins.Reset();
	TArray<FGuid> Instructions;
	TMap<FGuid, FVoxelLandGraphNodeOrigin> Origins;
	if (!FVoxelLandGraphCompilePass::CompileSource(
			Snapshot, FVoxelLandOperatorRegistry::Snapshot(), OutDraft, OutDiagnostic, &Instructions, &Origins))
	{
		return false;
	}
	OutOrigins.Reserve(Instructions.Num());
	for (const FGuid& Id : Instructions)
	{
		const FVoxelLandGraphNodeOrigin* Origin = Origins.Find(Id);
		OutOrigins.Add(Origin ? *Origin : FVoxelLandGraphNodeOrigin{Id, {}});
	}
	return true;
}

bool FVoxelLandGraphCompilePass::CompileSource(const FVoxelLandGraphSnapshot& Snapshot,
	const FVoxelLandOperatorRegistrySnapshot& Providers, FVoxelLandProgramDraft& OutDraft,
	FVoxelLandGraphDiagnostic& OutDiagnostic, TArray<FGuid>* InstructionNodes,
	TMap<FGuid, FVoxelLandGraphNodeOrigin>* NodeOrigins)
{
	OutDiagnostic = FVoxelLandGraphDiagnostic();
	if (Snapshot.GraphRole != EVoxelLandGraphRole::Terrain)
	{
		OutDiagnostic.Message = TEXT("Material graphs cannot compile as terrain outputs.");
		return false;
	}
	if (!FVoxelLandBuiltinCatalog::Validate(OutDiagnostic.Message))
	{
		return false;
	}
	const bool bHasCalls = Snapshot.Nodes.ContainsByPredicate([](const FVoxelLandGraphNodeRecord& Node) {
		return Node.OperatorId == VoxelLandGraphFunctionCallId() || !Node.FunctionPath.IsNull();
	});
	if (bHasCalls || !Snapshot.Functions.IsEmpty())
	{
		FVoxelLandGraphSnapshot Expanded;
		TMap<FGuid, FGuid> OriginCalls;
		TMap<FGuid, FString> OriginDescriptions;
		if (!FVoxelLandGraphFunctionFlatten::Expand(
				Snapshot, Expanded, OutDiagnostic, OriginCalls, OriginDescriptions, NodeOrigins))
		{
			return false;
		}
		FVoxelLandGraphCompilePass Compiler(Expanded, Providers, OutDiagnostic, InstructionNodes);
		if (!Compiler.Run(OutDraft))
		{
			FVoxelLandGraphFunctionFlatten::TranslateDiagnostic(OutDiagnostic, OriginCalls, OriginDescriptions);
			return false;
		}
		return true;
	}
	FVoxelLandGraphCompilePass Compiler(Snapshot, Providers, OutDiagnostic, InstructionNodes);
	return Compiler.Run(OutDraft);
}

bool FVoxelLandGraphCompilePass::CollectWorldOperators(TConstArrayView<FVoxelLandGraphNodeRecord> Nodes,
	const FVoxelLandOperatorRegistrySnapshot& Providers, const FWorldValidationContext& Context,
	TArray<FVoxelLandWorldOperatorCapability>& Declarations)
{
	for (const FVoxelLandGraphNodeRecord& Node : Nodes)
	{
		if (Context.IsCancelled())
		{
			return false;
		}
		if (Node.OperatorId == VoxelLandGraphFunctionCallId())
		{
			continue;
		}
		bool bPresent = false;
		for (const FVoxelLandWorldOperatorCapability& Existing : Declarations)
		{
			if (Context.IsCancelled())
			{
				return false;
			}
			if (Existing.OperatorId == Node.OperatorId && Existing.OperatorVersion == Node.OperatorVersion)
			{
				bPresent = true;
				break;
			}
		}
		if (bPresent)
		{
			continue;
		}
		const auto Provider = Providers.Resolve(Node.OperatorId, Node.OperatorVersion);
		if (!Provider)
		{
			// Built-ins are resolved by the capability catalog. An unknown
			// external ID will be rejected there with its authored node ID.
			continue;
		}
		if (Declarations.Num() == MaxWorldProviders)
		{
			Context.Capability.Code = EVoxelLandWorldCapabilityError::InvalidInput;
			Context.Capability.NodeId = Node.Id;
			Context.Capability.Message = TEXT("Too many referenced providers");
			return false;
		}
		FVoxelLandWorldOperatorCapability Capability;
		Capability.OperatorId = Node.OperatorId;
		Capability.OperatorVersion = Node.OperatorVersion;
		Capability.Requirement = Provider->WorldRequirement();
		Declarations.Add(Capability);
	}
	return true;
}

bool FVoxelLandGraphCompilePass::PrepareWorldOperators(const FVoxelLandGraphSnapshot& Snapshot,
	EVoxelLandWorldKind Kind, const FVoxelLandOperatorRegistrySnapshot& Providers,
	const FWorldValidationContext& Context, TArray<FVoxelLandWorldOperatorCapability>& Declarations,
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe>& DeclarationsLease)
{
	if (Context.IsCancelled())
	{
		return Context.Fail(EVoxelLandWorldCapabilityError::Cancelled, TEXT("World compilation cancelled"));
	}
	// Match the capability validator's limits before inspecting any node. The
	// frozen provider registry is also bounded because Resolve is a linear scan.
	constexpr int32 MaxWorldNodes = 4096;
	constexpr int32 MaxWorldFunctions = 128;
	if ((Kind != EVoxelLandWorldKind::Finite && Kind != EVoxelLandWorldKind::Infinite)
		|| Snapshot.Nodes.Num() > MaxWorldNodes || Snapshot.Functions.Num() > MaxWorldFunctions
		|| Providers.Num() > MaxWorldProviders)
	{
		return Context.Fail(
			EVoxelLandWorldCapabilityError::InvalidInput, TEXT("World compilation input exceeds limits"));
	}
	if (!Context.Budget->TryChargeWork(int64(Snapshot.Functions.Num()) * 4 + 1))
	{
		return Context.Fail(
			EVoxelLandWorldCapabilityError::BudgetExceeded, TEXT("World compilation preflight exceeds budget"));
	}
	int64 TotalNodes = Snapshot.Nodes.Num();
	for (const FVoxelLandGraphFunctionSnapshot& Function : Snapshot.Functions)
	{
		if (Context.IsCancelled())
		{
			return Context.Fail(EVoxelLandWorldCapabilityError::Cancelled, TEXT("World compilation cancelled"));
		}
		if (Function.Nodes.Num() > MaxWorldNodes - TotalNodes)
		{
			return Context.Fail(
				EVoxelLandWorldCapabilityError::InvalidInput, TEXT("World compilation node limit exceeded"));
		}
		TotalNodes += Function.Nodes.Num();
	}
	// Per node this bounds the registry scan, the unique-declaration scan,
	// cancellation polls and fixed bookkeeping. Registry providers expose a
	// requirement captured at registration, rather than invoking provider code.
	const int64 Work = 1 + TotalNodes * (int64(Providers.Num()) * 4 + MaxWorldProviders * 4 + 8);
	if (!Context.Budget->TryChargeWork(Work))
	{
		return Context.Fail(
			EVoxelLandWorldCapabilityError::BudgetExceeded, TEXT("World capability collection exceeds budget"));
	}
	if (!Context.Budget->TryReserveBytes(
			4096 + int64(MaxWorldProviders) * sizeof(FVoxelLandWorldOperatorCapability) * 2, DeclarationsLease))
	{
		return Context.Fail(
			EVoxelLandWorldCapabilityError::BudgetExceeded, TEXT("World capability collection exceeds memory budget"));
	}
	Declarations.Reserve(MaxWorldProviders);
	if (!CollectWorldOperators(Snapshot.Nodes, Providers, Context, Declarations))
	{
		if (Context.Capability.Code == EVoxelLandWorldCapabilityError::None)
		{
			return Context.Fail(EVoxelLandWorldCapabilityError::Cancelled, TEXT("World compilation cancelled"));
		}
		Context.Diagnostic.Message = Context.Capability.Message;
		Context.Diagnostic.NodeId = Context.Capability.NodeId;
		return false;
	}
	for (const FVoxelLandGraphFunctionSnapshot& Function : Snapshot.Functions)
	{
		if (!CollectWorldOperators(Function.Nodes, Providers, Context, Declarations))
		{
			if (Context.Capability.Code == EVoxelLandWorldCapabilityError::None)
			{
				return Context.Fail(EVoxelLandWorldCapabilityError::Cancelled, TEXT("World compilation cancelled"));
			}
			Context.Diagnostic.Message = Context.Capability.Message;
			Context.Diagnostic.NodeId = Context.Capability.NodeId;
			return false;
		}
	}
	return true;
}

bool FVoxelLandGraphCompilePass::QuantizeFixedValue(float Value, int64& Out)
{
	constexpr int64 Q = int64(1) << 24;
	if (!FMath::IsFinite(Value) || FMath::Abs(double(Value)) > 64.0)
	{
		return false;
	}
	const double Scaled = double(Value) * double(Q);
	Out = Scaled < 0 ? -int64(std::floor(-Scaled + 0.5)) : int64(std::floor(Scaled + 0.5));
	return true;
}

bool FVoxelLandGraphCompilePass::ReadFixedConstant(
	const FVoxelLandProgramDraft& Draft, const FVoxelLandValueSource& Source, float& Out)
{
	if (Source.bParameter || !Draft.Constants.IsValidIndex(int32(Source.Index)))
	{
		return false;
	}
	Out = Draft.Constants[Source.Index];
	return true;
}

const TCHAR* FVoxelLandGraphCompilePass::ValidateFixedNoise(
	const FVoxelLandProgramDraft& Draft, const FVoxelLandInstruction& I)
{
	constexpr int64 Q = int64(1) << 24;
	const FVoxelLandNoiseConfig& N = Draft.Noise[I.Imm];
	float F = 0, G = 0, L = 0, O = 0;
	int64 Fq = 0, Gq = 0, Lq = 0;
	if (!ReadFixedConstant(Draft, N.Frequency, F) || !ReadFixedConstant(Draft, N.Gain, G)
		|| !ReadFixedConstant(Draft, N.Lacunarity, L) || !ReadFixedConstant(Draft, N.Octaves, O)
		|| !QuantizeFixedValue(F, Fq) || !QuantizeFixedValue(G, Gq) || !QuantizeFixedValue(L, Lq) || Fq < 1 || Fq > Q
		|| Gq < 0 || Gq > Q || Lq < Q || Lq > 4 * Q || !FMath::IsFinite(O) || O < 1 || O > 16
		|| std::floor(double(O)) != double(O))
	{
		return TEXT("noise parameters must be constant and representable in Q24");
	}
	for (int32 Octave = 1; I.Op == EVoxelLandOpcode::Fbm2 && Octave < int32(O); ++Octave)
	{
		// Positive values, exact ties-away rounding as in fixed lattice.
		const int64 Product = Fq * Lq;
		Fq = (Product + Q / 2) / Q;
		if (Fq < 1 || Fq > Q)
		{
			return TEXT("fBm octave frequency exceeds Q24 limits");
		}
	}
	return nullptr;
}

bool FVoxelLandGraphCompilePass::ValidateFixedLatticeDraft(const FVoxelLandProgramDraft& Draft,
	TConstArrayView<FGuid> InstructionNodes, const TMap<FGuid, FVoxelLandGraphNodeOrigin>& NodeOrigins,
	const FWorldValidationContext& Context)
{
	// The present chunk-local control backend implements a strict Q24 subset.
	// A requirement being local does not imply that this backend implements it.
	// Parameters can be overridden after compilation; until this entry point
	// receives frozen overrides, reject them rather than approve a value the
	// actual fixed-lattice validator would later reject.
	enum class EWorldCompilerRegisterKind : uint8
	{
		Unknown,
		X,
		Y,
		Value
	};
	auto FailFixed = [&](int32 Index, const TCHAR* Message) {
		if (InstructionNodes.IsValidIndex(Index))
		{
			const FGuid& NodeId = InstructionNodes[Index];
			const FVoxelLandGraphNodeOrigin* Origin = NodeOrigins.Find(NodeId);
			Context.Capability.NodeId = Origin ? Origin->NodeId : NodeId;
			if (Origin)
			{
				Context.Capability.FunctionPath = Origin->FunctionPath;
			}
		}
		Context.Capability.Code = EVoxelLandWorldCapabilityError::BackendUnsupported;
		Context.Capability.Message =
			FString::Printf(TEXT("Infinite fixed-lattice backend: %s (instruction %d)"), Message, Index);
		Context.Diagnostic.Message = Context.Capability.Message;
		if (!Context.Capability.FunctionPath.IsEmpty())
		{
			Context.Diagnostic.Message += FString::Printf(TEXT(" (function %s)"), *Context.Capability.FunctionPath);
		}
		Context.Diagnostic.NodeId = Context.Capability.NodeId;
		return false;
	};
	if (Draft.RegisterCount < 1 || Draft.RegisterCount > 8192 || Draft.Instructions.Num() > 8192
		|| Draft.SmoothFilter.bPresent || Draft.ExternalFilterIndex != INDEX_NONE || !Draft.ExternalOperators.IsEmpty())
	{
		return FailFixed(INDEX_NONE, TEXT("filters or external operators are not supported"));
	}
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> FixedCheckLease;
	if (!Context.Budget->TryChargeWork(1 + int64(Draft.Instructions.Num()) * 64)
		|| !Context.Budget->TryReserveBytes(4096 + int64(Draft.RegisterCount) * 2, FixedCheckLease))
	{
		return Context.Fail(EVoxelLandWorldCapabilityError::BudgetExceeded, TEXT("Fixed-lattice check exceeds budget"));
	}
	TArray<EWorldCompilerRegisterKind> Kinds;
	Kinds.Init(EWorldCompilerRegisterKind::Unknown, Draft.RegisterCount);

	for (int32 Index = 0; Index < Draft.Instructions.Num(); ++Index)
	{
		if (Context.IsCancelled())
		{
			return Context.Fail(EVoxelLandWorldCapabilityError::Cancelled, TEXT("World compilation cancelled"));
		}
		const FVoxelLandInstruction& I = Draft.Instructions[Index];
		if (I.Op != EVoxelLandOpcode::StoreDensity && !Kinds.IsValidIndex(I.Dst))
		{
			return FailFixed(Index, TEXT("invalid destination register"));
		}
		auto KindIs = [&](uint16 R, EWorldCompilerRegisterKind K) {
			return Kinds.IsValidIndex(R) && Kinds[R] == K;
		};
		EWorldCompilerRegisterKind Result = EWorldCompilerRegisterKind::Value;
		switch (I.Op)
		{
			case EVoxelLandOpcode::LoadX:
				Result = EWorldCompilerRegisterKind::X;
				break;
			case EVoxelLandOpcode::LoadY:
				Result = EWorldCompilerRegisterKind::Y;
				break;
			case EVoxelLandOpcode::LoadConstant:
			{
				int64 Fixed = 0;
				if (!Draft.Constants.IsValidIndex(int32(I.Imm)) || !QuantizeFixedValue(Draft.Constants[I.Imm], Fixed))
				{
					return FailFixed(Index, TEXT("constant is outside the supported Q24 range"));
				}
				break;
			}
			case EVoxelLandOpcode::Value2:
			case EVoxelLandOpcode::Fbm2:
			{
				if (!KindIs(I.A, EWorldCompilerRegisterKind::X) || !KindIs(I.B, EWorldCompilerRegisterKind::Y)
					|| !Draft.Noise.IsValidIndex(int32(I.Imm)))
				{
					return FailFixed(Index, TEXT("noise requires direct global X/Y inputs"));
				}
				if (const TCHAR* Error = ValidateFixedNoise(Draft, I))
				{
					return FailFixed(Index, Error);
				}
				break;
			}
			case EVoxelLandOpcode::Add:
			case EVoxelLandOpcode::Subtract:
			case EVoxelLandOpcode::Multiply:
			case EVoxelLandOpcode::Min:
			case EVoxelLandOpcode::Max:
				if (!KindIs(I.A, EWorldCompilerRegisterKind::Value) || !KindIs(I.B, EWorldCompilerRegisterKind::Value))
				{
					return FailFixed(Index, TEXT("arithmetic cannot transform coordinates"));
				}
				break;
			case EVoxelLandOpcode::Negate:
			case EVoxelLandOpcode::Abs:
				if (!KindIs(I.A, EWorldCompilerRegisterKind::Value))
				{
					return FailFixed(Index, TEXT("arithmetic cannot transform coordinates"));
				}
				break;
			case EVoxelLandOpcode::StoreDensity:
				if (!KindIs(I.A, EWorldCompilerRegisterKind::Value))
				{
					return FailFixed(Index, TEXT("output must be a scalar value"));
				}
				continue;
			default:
				return FailFixed(Index, TEXT("operator is not implemented by the fixed-lattice backend"));
		}
		Kinds[I.Dst] = Result;
	}
	return true;
}

bool FVoxelLandGraphCompiler::CompileForWorld(const FVoxelLandGraphSnapshot& Snapshot, EVoxelLandWorldKind Kind,
	const FVoxelLandOperatorRegistrySnapshot& Providers, TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget,
	TFunctionRef<bool()> IsCancelled, FVoxelLandProgramDraft& OutDraft,
	FVoxelLandWorldCapabilityDiagnostic& OutCapabilityDiagnostic, FVoxelLandGraphDiagnostic& OutDiagnostic)
{
	FVoxelLandProgramDraft Draft;
	OutCapabilityDiagnostic = FVoxelLandWorldCapabilityDiagnostic();
	OutDiagnostic = FVoxelLandGraphDiagnostic();
	const FVoxelLandGraphCompilePass::FWorldValidationContext Context{
		Budget, IsCancelled, OutCapabilityDiagnostic, OutDiagnostic};
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> DeclarationsLease;
	TArray<FVoxelLandWorldOperatorCapability> Declarations;
	if (!FVoxelLandGraphCompilePass::PrepareWorldOperators(
			Snapshot, Kind, Providers, Context, Declarations, DeclarationsLease))
	{
		return false;
	}
	OutCapabilityDiagnostic = FVoxelLandWorldCapabilities::Validate(Snapshot, Kind, Declarations, Budget, IsCancelled);
	if (OutCapabilityDiagnostic.Code != EVoxelLandWorldCapabilityError::None)
	{
		OutDiagnostic.NodeId = OutCapabilityDiagnostic.NodeId;
		OutDiagnostic.Message = OutCapabilityDiagnostic.FunctionPath.IsEmpty()
								  ? OutCapabilityDiagnostic.Message
								  : FString::Printf(TEXT("%s (function %s)"), *OutCapabilityDiagnostic.Message,
										*OutCapabilityDiagnostic.FunctionPath);
		return false;
	}
	if (IsCancelled())
	{
		return Context.Fail(EVoxelLandWorldCapabilityError::Cancelled, TEXT("World compilation cancelled"));
	}
	TArray<FGuid> InstructionNodes;
	TMap<FGuid, FVoxelLandGraphNodeOrigin> NodeOrigins;
	const bool bHasFixedBackend = Kind == EVoxelLandWorldKind::Infinite;
	if (!FVoxelLandGraphCompilePass::CompileSource(Snapshot, Providers, Draft, OutDiagnostic,
			bHasFixedBackend ? &InstructionNodes : nullptr, bHasFixedBackend ? &NodeOrigins : nullptr))
	{
		return false;
	}
	if (bHasFixedBackend
		&& !FVoxelLandGraphCompilePass::ValidateFixedLatticeDraft(Draft, InstructionNodes, NodeOrigins, Context))
	{
		return false;
	}
	if (IsCancelled())
	{
		return Context.Fail(EVoxelLandWorldCapabilityError::Cancelled, TEXT("World compilation cancelled"));
	}
	OutDraft = MoveTemp(Draft);
	return true;
}
