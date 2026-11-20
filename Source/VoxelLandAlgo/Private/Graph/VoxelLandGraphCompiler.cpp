#include "Graph/VoxelLandGraphCompiler.h"

#include "Data/Enums/VoxelLandBuiltinNode.h"
#include "Data/Enums/VoxelLandParameterType.h"
#include "Operators/VoxelLandOperator.h"
#include "Generation/VoxelLandPower.h"

namespace
{
constexpr int32 MaxRegisters = 1024;
constexpr int32 MaxInstructions = 8192;

class FCompiler
{
public:
    FCompiler(const FVoxelLandGraphSnapshot& InSource, FString& InError) : Source(InSource), Error(InError)
    {
        Draft.FormatVersion = 2;
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
        Draft.Instructions.Add(Store);
        if (Draft.Instructions.Num() > MaxInstructions)
        {
            return Fail(TEXT("Graph exceeds instruction limit"));
        }
        Out = MoveTemp(Draft);
        return true;
    }

private:
    bool Fail(const TCHAR* Message)
    {
        Error = Message;
        return false;
    }

    bool IndexSource()
    {
        if (Source.SourceVersion != 1 || Source.Nodes.Num() > 4096 || Source.Links.Num() > 16384 || Source.Parameters.Num() > 4096)
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
            if (!Parameter.Id.IsValid() || ParameterById.Contains(Parameter.Id) || Name.IsEmpty() || Names.Contains(Name))
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
                if (!FMath::IsFinite(Parameter.FloatDefault) || (Parameter.bHasRange &&
                    (!FMath::IsFinite(Parameter.FloatMin) || !FMath::IsFinite(Parameter.FloatMax) ||
                     Parameter.FloatMin > Parameter.FloatMax || Parameter.FloatDefault < Parameter.FloatMin ||
                     Parameter.FloatDefault > Parameter.FloatMax)))
                {
                    return Fail(TEXT("Invalid float parameter default or range"));
                }
            }
            else if (Parameter.Type == EVoxelLandGraphValueType::Int32)
            {
                if (Parameter.bHasRange && (Parameter.IntMin > Parameter.IntMax ||
                    Parameter.IntDefault < Parameter.IntMin || Parameter.IntDefault > Parameter.IntMax))
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
            if (!Node.OperatorId.IsValid() || Node.OperatorVersion < 1 || Node.Configuration.Num() > 65536 ||
                ConfigurationBytes > 1048576 || !Node.Id.IsValid() || Nodes.Contains(Node.Id) || Node.Pins.Num() > 64)
            {
                return Fail(TEXT("Invalid or duplicate node ID or pin count"));
            }
            Nodes.Add(Node.Id, &Node);
            for (const FVoxelLandGraphPinRecord& Pin : Node.Pins)
            {
                if (!Pin.Id.IsValid() || Pins.Contains(Pin.Id) || Pin.bInvalidDefault || !FMath::IsFinite(Pin.FloatDefault) ||
                    (Pin.Type != EVoxelLandGraphValueType::Float && Pin.Type != EVoxelLandGraphValueType::Int32 &&
                     Pin.Type != EVoxelLandGraphValueType::Bool))
                {
                    return Fail(TEXT("Invalid or duplicate pin ID or default"));
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
        TMap<FGuid, TArray<FGuid>> Edges;
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
            if (!From || !To || !FromOwner || !ToOwner || *FromOwner != Link.SourceNode ||
                *ToOwner != Link.TargetNode || !(*From)->bOutput || (*To)->bOutput ||
                (*From)->Type != (*To)->Type || InputLinks.Contains(Link.TargetPin))
            {
                return Fail(TEXT("Invalid link or multiple sources for input"));
            }
            InputLinks.Add(Link.TargetPin, Link.SourceNode);
            Edges.FindOrAdd(Link.SourceNode).Add(Link.TargetNode);
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
            if (const TArray<FGuid>* Targets = Edges.Find(Ready[Cursor]))
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
        return Ready.Num() == Source.Nodes.Num() || Fail(TEXT("Graph contains a cycle"));
    }

    bool CompileRoot(uint16& Result)
    {
        const FGuid* UpstreamId = InputLinks.Find(Output->Pins[0].Id);
        const FVoxelLandGraphNodeRecord* const* Upstream = UpstreamId ? Nodes.Find(*UpstreamId) : nullptr;
        if (!Upstream || (*Upstream)->OperatorId != VoxelLandBuiltinNodeId(EVoxelLandBuiltinNode::SmoothDensity))
        {
            return CompileInput(*Output, 0, Result);
        }
        const FVoxelLandGraphNodeRecord& Filter = **Upstream;
        const EVoxelLandGraphValueType Types[] = {EVoxelLandGraphValueType::Float, EVoxelLandGraphValueType::Bool,
            EVoxelLandGraphValueType::Float, EVoxelLandGraphValueType::Float};
        if (!CheckTypedPins(Filter, MakeArrayView(Types)))
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
            return Fail(TEXT("Smooth Density output must feed only Density Output"));
        }
        if (!NoiseSource(Filter, 1, EVoxelLandGraphValueType::Bool, Draft.SmoothFilter.Enabled) ||
            !NoiseSource(Filter, 2, EVoxelLandGraphValueType::Float, Draft.SmoothFilter.Strength))
        {
            return false;
        }
        Draft.SmoothFilter.bPresent = true;
        return CompileInput(Filter, 0, Result);
    }

    bool FindOutput()
    {
        for (const FVoxelLandGraphNodeRecord& Node : Source.Nodes)
        {
            if (Node.OperatorId == VoxelLandBuiltinNodeId(EVoxelLandBuiltinNode::DensityOutput))
            {
                if (Output || !CheckPins(Node, 1, false))
                {
                    return Fail(TEXT("Exactly one valid density output is required"));
                }
                Output = &Node;
            }
        }
        return Output != nullptr || Fail(TEXT("Missing density output"));
    }

    bool CheckPins(const FVoxelLandGraphNodeRecord& Node, int32 Inputs, bool HasOutput)
    {
        if (Node.OperatorVersion != 1 || Node.Configuration.Num() != 0 || Node.Pins.Num() != Inputs + int32(HasOutput))
        {
            return Fail(TEXT("Invalid builtin version, configuration or pin count"));
        }
        for (int32 Index = 0; Index < Node.Pins.Num(); ++Index)
        {
            if (Node.Pins[Index].Type != EVoxelLandGraphValueType::Float || Node.Pins[Index].bOutput != (Index == Inputs && HasOutput))
            {
                return Fail(TEXT("Invalid builtin pin direction or type"));
            }
        }
        return true;
    }

    bool CheckTypedPins(const FVoxelLandGraphNodeRecord& Node, TConstArrayView<EVoxelLandGraphValueType> Types)
    {
        if (Node.OperatorVersion != 1 || Node.Configuration.Num() != 0 || Node.Pins.Num() != Types.Num())
        {
            return Fail(TEXT("Invalid builtin version, configuration or pin count"));
        }
        for (int32 Index = 0; Index < Types.Num(); ++Index)
        {
            if (Node.Pins[Index].Type != Types[Index] || Node.Pins[Index].bOutput != (Index == Types.Num() - 1))
            {
                return Fail(TEXT("Invalid builtin pin direction or type"));
            }
        }
        return true;
    }

    bool Emit(EVoxelLandOpcode Op, uint16 A, uint16 B, uint16 C, uint32 Imm, uint16& Result)
    {
        if (Draft.RegisterCount >= MaxRegisters || Draft.Instructions.Num() >= MaxInstructions)
        {
            return Fail(TEXT("Graph exceeds VM register or instruction limit"));
        }
        const float* KA = KnownConstants.Find(A);
        const float* KB = KnownConstants.Find(B);
        const float* KC = KnownConstants.Find(C);
        if (Op == EVoxelLandOpcode::Divide && KB && *KB == 0.f)
        {
            return Fail(TEXT("Division by a constant zero"));
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
            case EVoxelLandOpcode::Abs: if (bFold) { Folded = FMath::Abs(*KA); } break;
            case EVoxelLandOpcode::Negate: if (bFold) { Folded = -*KA; } break;
            case EVoxelLandOpcode::Add: bFold = bFold && KB; if (bFold) { Folded = *KA + *KB; } break;
            case EVoxelLandOpcode::Subtract: bFold = bFold && KB; if (bFold) { Folded = *KA - *KB; } break;
            case EVoxelLandOpcode::Multiply: bFold = bFold && KB; if (bFold) { Folded = *KA * *KB; } break;
            case EVoxelLandOpcode::Divide: bFold = bFold && KB; if (bFold) { Folded = *KA / *KB; } break;
            case EVoxelLandOpcode::Min:
            case EVoxelLandOpcode::Union: bFold = bFold && KB; if (bFold) { Folded = FMath::Min(*KA, *KB); } break;
            case EVoxelLandOpcode::Max:
            case EVoxelLandOpcode::Intersect: bFold = bFold && KB; if (bFold) { Folded = FMath::Max(*KA, *KB); } break;
            case EVoxelLandOpcode::SubtractShape: bFold = bFold && KB; if (bFold) { Folded = FMath::Max(*KA, -*KB); } break;
            case EVoxelLandOpcode::Clamp: bFold = bFold && KB && KC; if (bFold) { Folded = FMath::Clamp(*KA, *KB, *KC); } break;
            default: bFold = false; break;
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
        Draft.Instructions.Add(Instruction);
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
            P.Type = Type == EVoxelLandGraphValueType::Float ? EVoxelLandParameterType::Float :
                Type == EVoxelLandGraphValueType::Int32 ? EVoxelLandParameterType::Int32 : EVoxelLandParameterType::Bool;
            P.IntDefault = Type == EVoxelLandGraphValueType::Bool ? ((*Parameter)->BoolDefault ? 1 : 0) : (*Parameter)->IntDefault;
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
        const EVoxelLandOpcode Op = Type == EVoxelLandGraphValueType::Float ? EVoxelLandOpcode::LoadFloatParameter :
            Type == EVoxelLandGraphValueType::Int32 ? EVoxelLandOpcode::LoadIntParameter : EVoxelLandOpcode::LoadBoolParameter;
        return Emit(Op, 0, 0, 0, uint32(Slot), Result);
    }

    bool CompileNode(const FVoxelLandGraphNodeRecord& Node, uint16& Result)
    {
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
            if (!Error.StartsWith(TEXT("[node=")))
            {
                Error = FString::Printf(TEXT("[node=%s] %s"), *Node.Id.ToString(), *Error);
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
    bool CompileExternal(const FVoxelLandGraphNodeRecord& Node, uint16& Result);
    bool NoiseSource(const FVoxelLandGraphNodeRecord& Node, int32 Index, EVoxelLandGraphValueType Type,
        FVoxelLandValueSource& Out);

    const FVoxelLandGraphSnapshot& Source;
    FString& Error;
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

bool FCompiler::CompileBuiltin(const FVoxelLandGraphNodeRecord& Node, uint16& Result)
{
    if (Node.OperatorId.A != 0x564c4d33 || Node.OperatorId.B != 1 || Node.OperatorId.C != 0)
    {
        return CompileExternal(Node, Result);
    }
    const EVoxelLandBuiltinNode Kind = EVoxelLandBuiltinNode(Node.OperatorId.D);
    if (Kind == EVoxelLandBuiltinNode::RerouteFloat || Kind == EVoxelLandBuiltinNode::RerouteInt || Kind == EVoxelLandBuiltinNode::RerouteBool)
    {
        const EVoxelLandGraphValueType Type = Kind == EVoxelLandBuiltinNode::RerouteFloat ? EVoxelLandGraphValueType::Float :
            Kind == EVoxelLandBuiltinNode::RerouteInt ? EVoxelLandGraphValueType::Int32 : EVoxelLandGraphValueType::Bool;
        const EVoxelLandGraphValueType Types[] = {Type, Type};
        return CheckTypedPins(Node, MakeArrayView(Types)) && CompileInput(Node, 0, Result);
    }
    if (Kind == EVoxelLandBuiltinNode::PositionX || Kind == EVoxelLandBuiltinNode::PositionY || Kind == EVoxelLandBuiltinNode::PositionZ)
    {
        if (!CheckPins(Node, 0, true))
        {
            return false;
        }
        const EVoxelLandOpcode Op = Kind == EVoxelLandBuiltinNode::PositionX ? EVoxelLandOpcode::LoadX :
            Kind == EVoxelLandBuiltinNode::PositionY ? EVoxelLandOpcode::LoadY : EVoxelLandOpcode::LoadZ;
        return Emit(Op, 0, 0, 0, 0, Result);
    }
    if (Kind == EVoxelLandBuiltinNode::FloatParameter)
    {
        return CheckPins(Node, 0, true) && CompileParameter(Node, EVoxelLandGraphValueType::Float, Result);
    }
    if (Kind == EVoxelLandBuiltinNode::FloatConstant)
    {
        if (!CheckPins(Node, 1, true) || InputLinks.Contains(Node.Pins[0].Id))
        {
            return Fail(TEXT("Float constant requires one unconnected value pin"));
        }
        return Constant(Node.Pins[0].FloatDefault, Result);
    }
    if (uint32(Kind) >= uint32(EVoxelLandBuiltinNode::IntConstant) && uint32(Kind) <= uint32(EVoxelLandBuiltinNode::Power))
    {
        return CompileTypedBuiltin(Node, Kind, Result);
    }
    if (Kind == EVoxelLandBuiltinNode::Plane || Kind == EVoxelLandBuiltinNode::Sphere || Kind == EVoxelLandBuiltinNode::Box)
    {
        return CompileShape(Node, Kind, Result);
    }
    if (uint32(Kind) >= uint32(EVoxelLandBuiltinNode::Perlin2) && uint32(Kind) <= uint32(EVoxelLandBuiltinNode::Simplex3))
    {
        return CompileNoise(Node, Kind, Result);
    }
    return CompileFloatBuiltin(Node, Kind, Result);
}

bool FCompiler::CompileExternal(const FVoxelLandGraphNodeRecord& Node, uint16& Result)
{
    TSharedPtr<const IVoxelLandOperator, ESPMode::ThreadSafe> Provider =
        FVoxelLandOperatorRegistry::Resolve(Node.OperatorId, Node.OperatorVersion);
    if (!Provider)
    {
        return Fail(TEXT("Missing external operator provider or incompatible version"));
    }
    const TArray<EVoxelLandGraphValueType> Inputs = Provider->InputTypes();
    if (Inputs.Num() < 1 || Inputs.Num() > 3 || Provider->MaximumWorkerBytes() > 1048576 ||
        Node.Configuration.Num() > 65536 || Node.Pins.Num() != Inputs.Num() + 1)
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
        if (Node.Pins[Index].bOutput || Node.Pins[Index].Type != Inputs[Index] || !CompileInput(Node, Index, R[Index]))
        {
            return Fail(TEXT("External operator input signature mismatch"));
        }
    }
    if (!Node.Pins.Last().bOutput || Node.Pins.Last().Type != Provider->OutputType())
    {
        return Fail(TEXT("External operator output signature mismatch"));
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

bool FCompiler::NoiseSource(const FVoxelLandGraphNodeRecord& Node, int32 Index,
    EVoxelLandGraphValueType Type, FVoxelLandValueSource& Out)
{
    const FVoxelLandGraphPinRecord& Pin = Node.Pins[Index];
    if (const FGuid* UpstreamId = InputLinks.Find(Pin.Id))
    {
        const FVoxelLandGraphNodeRecord* const* Upstream = Nodes.Find(*UpstreamId);
        const EVoxelLandBuiltinNode Expected = Type == EVoxelLandGraphValueType::Float ?
            EVoxelLandBuiltinNode::FloatParameter : Type == EVoxelLandGraphValueType::Bool ?
            EVoxelLandBuiltinNode::BoolParameter : EVoxelLandBuiltinNode::IntParameter;
        if (!Upstream || (*Upstream)->OperatorId != VoxelLandBuiltinNodeId(Expected))
        {
            return Fail(TEXT("Uniform settings must be unconnected defaults or direct parameter references"));
        }
        const EVoxelLandGraphValueType Types[] = {Type};
        int32 Slot = INDEX_NONE;
        if (!CheckTypedPins(**Upstream, MakeArrayView(Types)) || !FindOrAddParameter(**Upstream, Type, Slot))
        {
            return false;
        }
        Out.bParameter = true;
        Out.Index = uint32(Slot);
        return true;
    }
    const float Value = Type == EVoxelLandGraphValueType::Float ? Pin.FloatDefault :
        Type == EVoxelLandGraphValueType::Bool ? (Pin.BoolDefault ? 1.f : 0.f) : float(Pin.IntDefault);
    if (!FMath::IsFinite(Value))
    {
        return Fail(TEXT("Invalid noise setting"));
    }
    Out.Index = uint32(Draft.Constants.Add(Value));
    return true;
}

bool FCompiler::CompileNoise(const FVoxelLandGraphNodeRecord& Node, EVoxelLandBuiltinNode Kind, uint16& Result)
{
    const bool b3D = Kind == EVoxelLandBuiltinNode::Perlin3 || Kind == EVoxelLandBuiltinNode::Fbm3 ||
        Kind == EVoxelLandBuiltinNode::Value3 || Kind == EVoxelLandBuiltinNode::Simplex3;
    const int32 Dimensions = b3D ? 3 : 2;
    TArray<EVoxelLandGraphValueType> Types;
    for (int32 Index = 0; Index < Dimensions + 1; ++Index)
    {
        Types.Add(EVoxelLandGraphValueType::Float);
    }
    Types.Add(EVoxelLandGraphValueType::Int32);
    Types.Add(EVoxelLandGraphValueType::Float);
    Types.Add(EVoxelLandGraphValueType::Float);
    Types.Add(EVoxelLandGraphValueType::Float);
    if (!CheckTypedPins(Node, Types))
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
    if (!NoiseSource(Node, Dimensions, EVoxelLandGraphValueType::Float, Config.Frequency) ||
        !NoiseSource(Node, Dimensions + 1, EVoxelLandGraphValueType::Int32, Config.Octaves) ||
        !NoiseSource(Node, Dimensions + 2, EVoxelLandGraphValueType::Float, Config.Gain) ||
        !NoiseSource(Node, Dimensions + 3, EVoxelLandGraphValueType::Float, Config.Lacunarity))
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
        case EVoxelLandBuiltinNode::Perlin2: Op = EVoxelLandOpcode::Perlin2; break;
        case EVoxelLandBuiltinNode::Perlin3: Op = EVoxelLandOpcode::Perlin3; break;
        case EVoxelLandBuiltinNode::Fbm2: Op = EVoxelLandOpcode::Fbm2; break;
        case EVoxelLandBuiltinNode::Fbm3: Op = EVoxelLandOpcode::Fbm3; break;
        case EVoxelLandBuiltinNode::Value2: Op = EVoxelLandOpcode::Value2; break;
        case EVoxelLandBuiltinNode::Value3: Op = EVoxelLandOpcode::Value3; break;
        case EVoxelLandBuiltinNode::Simplex2: Op = EVoxelLandOpcode::Simplex2; break;
        case EVoxelLandBuiltinNode::Simplex3: Op = EVoxelLandOpcode::Simplex3; break;
        default: return Fail(TEXT("Unknown noise operator"));
    }
    return Emit(Op, Coordinates[0], Coordinates[1], Coordinates[2], ConfigIndex, Result);
}

bool FCompiler::CompileFloatBuiltin(const FVoxelLandGraphNodeRecord& Node, EVoxelLandBuiltinNode Kind, uint16& Result)
{
    if (Kind == EVoxelLandBuiltinNode::HeightToDensity)
    {
        uint16 Z = 0, Height = 0;
        return CheckPins(Node, 1, true) && Emit(EVoxelLandOpcode::LoadZ, 0, 0, 0, 0, Z) &&
            CompileInput(Node, 0, Height) && Emit(EVoxelLandOpcode::Subtract, Z, Height, 0, 0, Result);
    }
    if (Kind == EVoxelLandBuiltinNode::Abs || Kind == EVoxelLandBuiltinNode::Negate)
    {
        uint16 A = 0;
        const EVoxelLandOpcode Op = Kind == EVoxelLandBuiltinNode::Abs ? EVoxelLandOpcode::Abs : EVoxelLandOpcode::Negate;
        return CheckPins(Node, 1, true) && CompileInput(Node, 0, A) && Emit(Op, A, 0, 0, 0, Result);
    }
    if (Kind == EVoxelLandBuiltinNode::Saturate)
    {
        uint16 A = 0, Zero = 0, One = 0, Lower = 0;
        return CheckPins(Node, 1, true) && CompileInput(Node, 0, A) && Constant(0.f, Zero) && Constant(1.f, One) &&
            Emit(EVoxelLandOpcode::Max, A, Zero, 0, 0, Lower) && Emit(EVoxelLandOpcode::Min, Lower, One, 0, 0, Result);
    }
    if (Kind == EVoxelLandBuiltinNode::Clamp)
    {
        if (!CheckPins(Node, 3, true) ||
            (!InputLinks.Contains(Node.Pins[1].Id) && !InputLinks.Contains(Node.Pins[2].Id) &&
             Node.Pins[1].FloatDefault > Node.Pins[2].FloatDefault))
        {
            return Fail(TEXT("Clamp minimum exceeds maximum"));
        }
        uint16 A = 0, Lo = 0, Hi = 0;
        return CompileInput(Node, 0, A) && CompileInput(Node, 1, Lo) && CompileInput(Node, 2, Hi) &&
            Emit(EVoxelLandOpcode::Clamp, A, Lo, Hi, 0, Result);
    }
    if (Kind == EVoxelLandBuiltinNode::Lerp)
    {
        uint16 A = 0, B = 0, Alpha = 0, Difference = 0, Scaled = 0;
        return CheckPins(Node, 3, true) && CompileInput(Node, 0, A) && CompileInput(Node, 1, B) &&
            CompileInput(Node, 2, Alpha) && Emit(EVoxelLandOpcode::Subtract, B, A, 0, 0, Difference) &&
            Emit(EVoxelLandOpcode::Multiply, Difference, Alpha, 0, 0, Scaled) &&
            Emit(EVoxelLandOpcode::Add, A, Scaled, 0, 0, Result);
    }
    EVoxelLandOpcode Op;
    switch (Kind)
    {
        case EVoxelLandBuiltinNode::Add: Op = EVoxelLandOpcode::Add; break;
        case EVoxelLandBuiltinNode::Subtract: Op = EVoxelLandOpcode::Subtract; break;
        case EVoxelLandBuiltinNode::Multiply: Op = EVoxelLandOpcode::Multiply; break;
        case EVoxelLandBuiltinNode::Divide: Op = EVoxelLandOpcode::Divide; break;
        case EVoxelLandBuiltinNode::Min: Op = EVoxelLandOpcode::Min; break;
        case EVoxelLandBuiltinNode::Max: Op = EVoxelLandOpcode::Max; break;
        case EVoxelLandBuiltinNode::Union: Op = EVoxelLandOpcode::Union; break;
        case EVoxelLandBuiltinNode::SubtractShape: Op = EVoxelLandOpcode::SubtractShape; break;
        case EVoxelLandBuiltinNode::Intersect: Op = EVoxelLandOpcode::Intersect; break;
        default: return Fail(TEXT("Unsupported builtin operator"));
    }
    const bool bVariadic = Kind == EVoxelLandBuiltinNode::Add || Kind == EVoxelLandBuiltinNode::Multiply;
    const int32 InputCount = Node.Pins.Num() - 1;
    if (InputCount < 2 || InputCount > (bVariadic ? 32 : 2) || !CheckPins(Node, InputCount, true))
    {
        return Fail(TEXT("Invalid arithmetic input count"));
    }
    if (Kind == EVoxelLandBuiltinNode::Divide && !InputLinks.Contains(Node.Pins[1].Id) && Node.Pins[1].FloatDefault == 0.f)
    {
        return Fail(TEXT("Division by zero"));
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

bool FCompiler::CompileTypedBuiltin(const FVoxelLandGraphNodeRecord& Node, EVoxelLandBuiltinNode Kind, uint16& Result)
{
    if (Kind == EVoxelLandBuiltinNode::IntConstant || Kind == EVoxelLandBuiltinNode::BoolConstant)
    {
        const EVoxelLandGraphValueType Type = Kind == EVoxelLandBuiltinNode::IntConstant ?
            EVoxelLandGraphValueType::Int32 : EVoxelLandGraphValueType::Bool;
        const EVoxelLandGraphValueType Types[] = {Type, Type};
        if (!CheckTypedPins(Node, MakeArrayView(Types)) || InputLinks.Contains(Node.Pins[0].Id))
        {
            return Fail(TEXT("Typed constant requires an unconnected value"));
        }
        return CompileInput(Node, 0, Result);
    }
    if (Kind == EVoxelLandBuiltinNode::IntParameter || Kind == EVoxelLandBuiltinNode::BoolParameter)
    {
        const EVoxelLandGraphValueType Type = Kind == EVoxelLandBuiltinNode::IntParameter ?
            EVoxelLandGraphValueType::Int32 : EVoxelLandGraphValueType::Bool;
        const EVoxelLandGraphValueType Types[] = {Type};
        return CheckTypedPins(Node, MakeArrayView(Types)) && CompileParameter(Node, Type, Result);
    }
    if (Kind == EVoxelLandBuiltinNode::IntToFloat)
    {
        const EVoxelLandGraphValueType Types[] = {EVoxelLandGraphValueType::Int32, EVoxelLandGraphValueType::Float};
        uint16 A = 0;
        return CheckTypedPins(Node, MakeArrayView(Types)) && CompileInput(Node, 0, A) &&
            Emit(EVoxelLandOpcode::IntToFloat, A, 0, 0, 0, Result);
    }
    if (Kind == EVoxelLandBuiltinNode::Less || Kind == EVoxelLandBuiltinNode::LessEqual || Kind == EVoxelLandBuiltinNode::Equal)
    {
        const EVoxelLandGraphValueType Types[] = {EVoxelLandGraphValueType::Float, EVoxelLandGraphValueType::Float,
            EVoxelLandGraphValueType::Bool};
        const EVoxelLandOpcode Op = Kind == EVoxelLandBuiltinNode::Less ? EVoxelLandOpcode::Less :
            Kind == EVoxelLandBuiltinNode::LessEqual ? EVoxelLandOpcode::LessEqual : EVoxelLandOpcode::Equal;
        uint16 A = 0, B = 0;
        return CheckTypedPins(Node, MakeArrayView(Types)) && CompileInput(Node, 0, A) && CompileInput(Node, 1, B) &&
            Emit(Op, A, B, 0, 0, Result);
    }
    if (Kind == EVoxelLandBuiltinNode::BoolAnd || Kind == EVoxelLandBuiltinNode::BoolOr || Kind == EVoxelLandBuiltinNode::BoolNot)
    {
        const bool bUnary = Kind == EVoxelLandBuiltinNode::BoolNot;
        const EVoxelLandGraphValueType Unary[] = {EVoxelLandGraphValueType::Bool, EVoxelLandGraphValueType::Bool};
        const EVoxelLandGraphValueType Binary[] = {EVoxelLandGraphValueType::Bool, EVoxelLandGraphValueType::Bool,
            EVoxelLandGraphValueType::Bool};
        const EVoxelLandOpcode Op = bUnary ? EVoxelLandOpcode::BoolNot :
            Kind == EVoxelLandBuiltinNode::BoolAnd ? EVoxelLandOpcode::BoolAnd : EVoxelLandOpcode::BoolOr;
        uint16 A = 0, B = 0;
        return CheckTypedPins(Node, bUnary ? MakeArrayView(Unary) : MakeArrayView(Binary)) &&
            CompileInput(Node, 0, A) && (bUnary || CompileInput(Node, 1, B)) && Emit(Op, A, B, 0, 0, Result);
    }
    if (Kind == EVoxelLandBuiltinNode::SelectFloat || Kind == EVoxelLandBuiltinNode::SelectInt || Kind == EVoxelLandBuiltinNode::SelectBool)
    {
        const EVoxelLandGraphValueType Type = Kind == EVoxelLandBuiltinNode::SelectFloat ? EVoxelLandGraphValueType::Float :
            Kind == EVoxelLandBuiltinNode::SelectInt ? EVoxelLandGraphValueType::Int32 : EVoxelLandGraphValueType::Bool;
        const EVoxelLandGraphValueType Types[] = {EVoxelLandGraphValueType::Bool, Type, Type, Type};
        const EVoxelLandOpcode Op = Kind == EVoxelLandBuiltinNode::SelectFloat ? EVoxelLandOpcode::SelectFloat :
            Kind == EVoxelLandBuiltinNode::SelectInt ? EVoxelLandOpcode::SelectInt : EVoxelLandOpcode::SelectBool;
        uint16 A = 0, B = 0, C = 0;
        return CheckTypedPins(Node, MakeArrayView(Types)) && CompileInput(Node, 0, A) && CompileInput(Node, 1, B) &&
            CompileInput(Node, 2, C) && Emit(Op, A, B, C, 0, Result);
    }
    if (Kind == EVoxelLandBuiltinNode::Power)
    {
        if (!CheckPins(Node, 2, true))
        {
            return false;
        }
        uint16 A = 0, B = 0;
        return CompileInput(Node, 0, A) && CompileInput(Node, 1, B) &&
            Emit(EVoxelLandOpcode::Power, A, B, 0, 0, Result);
    }
    return Fail(TEXT("Unsupported typed operator"));
}

bool FCompiler::CompileShape(const FVoxelLandGraphNodeRecord& Node, EVoxelLandBuiltinNode Kind, uint16& Result)
{
    if (Kind == EVoxelLandBuiltinNode::Plane || Kind == EVoxelLandBuiltinNode::Sphere || Kind == EVoxelLandBuiltinNode::Box)
    {
        const int32 InputCount = Kind == EVoxelLandBuiltinNode::Plane ? 7 : Kind == EVoxelLandBuiltinNode::Sphere ? 4 : 6;
        if (!CheckPins(Node, InputCount, true))
        {
            return false;
        }
        if (Kind == EVoxelLandBuiltinNode::Sphere && !InputLinks.Contains(Node.Pins[3].Id) && Node.Pins[3].FloatDefault <= 0.f)
        {
            return Fail(TEXT("Sphere radius must be positive"));
        }
        if (Kind == EVoxelLandBuiltinNode::Box)
        {
            for (int32 Index = 3; Index < 6; ++Index)
            {
                if (!InputLinks.Contains(Node.Pins[Index].Id) && Node.Pins[Index].FloatDefault <= 0.f)
                {
                    return Fail(TEXT("Box extents must be positive"));
                }
            }
        }
        if (Kind == EVoxelLandBuiltinNode::Plane && !InputLinks.Contains(Node.Pins[3].Id) &&
            !InputLinks.Contains(Node.Pins[4].Id) && !InputLinks.Contains(Node.Pins[5].Id))
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
                    return Fail(TEXT("Shape radius or extent must be positive"));
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
        const EVoxelLandOpcode Op = Kind == EVoxelLandBuiltinNode::Plane ? EVoxelLandOpcode::DynamicPlane :
            Kind == EVoxelLandBuiltinNode::Sphere ? EVoxelLandOpcode::DynamicSphere : EVoxelLandOpcode::DynamicBox;
        if (!Emit(Op, R[0], R[1], R[2], R[6], Result))
        {
            return false;
        }
        FVoxelLandInstruction& Instruction = Draft.Instructions.Last();
        Instruction.D = R[3]; Instruction.E = R[4]; Instruction.F = R[5];
        return true;
    }
    return Fail(TEXT("Unsupported shape operator"));
}

}

bool FVoxelLandGraphCompiler::Compile(const FVoxelLandGraphSnapshot& Snapshot, FVoxelLandProgramDraft& OutDraft, FString& OutError)
{
    OutDraft = FVoxelLandProgramDraft();
    OutError.Reset();
    FCompiler Compiler(Snapshot, OutError);
    return Compiler.Run(OutDraft);
}
