#include "Graph/VoxelLandGraphFunctionFlatten.h"

#include "Data/Enums/VoxelLandBuiltinNode.h"
#include "Data/Structs/VoxelLandCanonicalWriter.h"
#include "Data/Structs/VoxelLandGraphDiagnostic.h"
#include "Data/Structs/VoxelLandGraphSnapshot.h"
#include "Data/VoxelLandGraphFunctionId.h"
#include "Data/VoxelLandGraphCycle.h"
#include "Misc/SecureHash.h"

namespace
{
constexpr int32 MaxExpandedNodes = 4096;
constexpr int32 MaxExpandedLinks = 16384;
constexpr int32 MaxExpansions = 4096;

FGuid NamespacedId(const FGuid& Call, const FGuid& Inner, uint8 Domain)
{
    FCanonicalWriter Writer;
    Writer.U8(Domain);
    Writer.U32(Call.A); Writer.U32(Call.B); Writer.U32(Call.C); Writer.U32(Call.D);
    Writer.U32(Inner.A); Writer.U32(Inner.B); Writer.U32(Inner.C); Writer.U32(Inner.D);
    uint8 Digest[20];
    FSHA1::HashBuffer(Writer.Bytes.GetData(), Writer.Bytes.Num(), Digest);
    uint32 Word[4] = {};
    for (int32 Index = 0; Index < 16; ++Index)
    {
        Word[Index / 4] |= uint32(Digest[Index]) << (8 * (Index % 4));
    }
    return FGuid(Word[0], Word[1], Word[2], Word[3]);
}

class FFunctionSnapshotValidator
{
public:
    FFunctionSnapshotValidator(const FVoxelLandGraphSnapshot& InSource, FVoxelLandGraphDiagnostic& InDiagnostic)
        : Source(InSource), Diagnostic(InDiagnostic) {}

    bool Validate()
    {
        if (Source.Functions.Num() > 128 || !CheckBudget(Source.Nodes, Source.Links))
        {
            return Fail(TEXT("Captured function graph exceeds size limits."));
        }
        for (const FVoxelLandGraphFunctionSnapshot& Function : Source.Functions)
        {
            if (Function.AssetPath.IsEmpty() || Function.AssetPath.Len() > 2048 || Functions.Contains(Function.AssetPath))
            {
                return Fail(TEXT("Captured function dependency has an invalid or duplicate path."));
            }
            Functions.Add(Function.AssetPath, &Function);
        }
        int32 Height = 0;
        if (!VisitNodes(Source.Nodes, 0, FGuid(), FString(), Height))
        {
            return false;
        }
        if (Heights.Num() != Functions.Num())
        {
            return Fail(TEXT("Captured graph contains an unreferenced function dependency."));
        }
        return true;
    }

private:
    bool Fail(const TCHAR* Message)
    {
        Diagnostic.Message = Message;
        return false;
    }
    bool FailAt(const FGuid& RootCall, const FString& Scope, const FGuid& Node, const TCHAR* Message)
    {
        Diagnostic.NodeId = RootCall.IsValid() ? RootCall : Node;
        Diagnostic.Message = Scope.IsEmpty() ? FString(Message) :
            FString::Printf(TEXT("%s (function %s, node %s)"), Message, *Scope, *Node.ToString());
        return false;
    }
    bool CheckBudget(const TArray<FVoxelLandGraphNodeRecord>& Nodes, const TArray<FVoxelLandGraphLinkRecord>& Links)
    {
        NodeCount += Nodes.Num();
        LinkCount += Links.Num();
        if (NodeCount > MaxExpandedNodes || LinkCount > MaxExpandedLinks) { return false; }
        for (const FVoxelLandGraphNodeRecord& Node : Nodes)
        {
            ConfigBytes += Node.Configuration.Num();
            if (Node.Pins.Num() > 64 || Node.Configuration.Num() > 65536 || ConfigBytes > 1048576 ||
                Node.FunctionPath.ToString().Len() > 2048) { return false; }
            for (const FVoxelLandGraphPinRecord& Pin : Node.Pins)
            {
                if (Pin.Name.Len() > 128) { return false; }
            }
        }
        return true;
    }
    bool VisitNodes(const TArray<FVoxelLandGraphNodeRecord>& Nodes, int32 Depth, const FGuid& RootCall,
        const FString& Scope, int32& OutHeight)
    {
        OutHeight = 0;
        for (const FVoxelLandGraphNodeRecord& Node : Nodes)
        {
            const FGuid Call = RootCall.IsValid() ? RootCall : Node.Id;
            if (Node.OperatorId != VoxelLandGraphFunctionCallId())
            {
                if (!Node.FunctionPath.IsNull()) { return FailAt(Call, Scope, Node.Id, TEXT("Non-function node contains a function path.")); }
                continue;
            }
            const FString Path = Node.FunctionPath.ToString();
            if (Path.IsEmpty()) { return FailAt(Call, Scope, Node.Id, TEXT("Function call is missing its dependency path.")); }
            int32 Height = 0;
            if (!VisitFunction(Path, Depth + 1, Call, Scope, Node.Id, Height)) { return false; }
            OutHeight = FMath::Max(OutHeight, Height);
        }
        return true;
    }
    bool VisitFunction(const FString& Path, int32 Depth, const FGuid& RootCall, const FString& Scope,
        const FGuid& Node, int32& OutHeight)
    {
        if (Depth > 16 || Visiting.Contains(Path))
        {
            return FailAt(RootCall, Scope, Node, TEXT("Function dependency cycle or nesting limit exceeded."));
        }
        if (const int32* Cached = Heights.Find(Path))
        {
            OutHeight = *Cached;
            return Depth + *Cached - 1 <= 16 || FailAt(RootCall, Scope, Node, TEXT("Function nesting limit exceeded."));
        }
        const FVoxelLandGraphFunctionSnapshot* const* Function = Functions.Find(Path);
        if (!Function) { return FailAt(RootCall, Scope, Node, TEXT("A captured function dependency is missing.")); }
        if ((*Function)->SourceVersion != 1 || (*Function)->Inputs.Num() > 63 ||
            !CheckBudget((*Function)->Nodes, (*Function)->Links))
        {
            return FailAt(RootCall, Path, Node, TEXT("Captured function dependency is invalid or exceeds size limits."));
        }
        for (const FVoxelLandGraphParameter& Input : (*Function)->Inputs)
        {
            if (Input.Name.Len() > 128 || Input.Tooltip.ToString().Len() > 4096)
            {
                return FailAt(RootCall, Path, Node, TEXT("Captured function interface text exceeds size limits."));
            }
        }
        Visiting.Add(Path);
        int32 ChildHeight = 0;
        if (!VisitNodes((*Function)->Nodes, Depth, RootCall, Path, ChildHeight)) { return false; }
        Visiting.Remove(Path);
        OutHeight = ChildHeight + 1;
        Heights.Add(Path, OutHeight);
        return true;
    }
    const FVoxelLandGraphSnapshot& Source;
    FVoxelLandGraphDiagnostic& Diagnostic;
    TMap<FString, const FVoxelLandGraphFunctionSnapshot*> Functions;
    TMap<FString, int32> Heights;
    TSet<FString> Visiting;
    int64 NodeCount = 0;
    int64 LinkCount = 0;
    int64 ConfigBytes = 0;
};

class FFunctionExpander
{
public:
    FFunctionExpander(FVoxelLandGraphSnapshot& InGraph, FVoxelLandGraphDiagnostic& InDiagnostic,
        TMap<FGuid, FGuid>& InOriginCalls, TMap<FGuid, FString>& InOriginDescriptions)
        : Graph(InGraph), Diagnostic(InDiagnostic), OriginCalls(InOriginCalls), OriginDescriptions(InOriginDescriptions) {}

    bool Run()
    {
        for (int32 Iteration = 0; Iteration < MaxExpansions; ++Iteration)
        {
            int32 Index = INDEX_NONE;
            for (int32 Candidate = 0; Candidate < Graph.Nodes.Num(); ++Candidate)
            {
                if (Graph.Nodes[Candidate].OperatorId == VoxelLandGraphFunctionCallId() &&
                    (Index == INDEX_NONE || Graph.Nodes[Candidate].Id.ToString() < Graph.Nodes[Index].Id.ToString()))
                {
                    Index = Candidate;
                }
            }
            if (Index == INDEX_NONE)
            {
                return true;
            }
            if (!ExpandCall(Index))
            {
                return false;
            }
        }
        Diagnostic.Message = TEXT("Function expansion exceeds its limit.");
        return false;
    }

private:
    bool Fail(const FGuid& Node, const TCHAR* Message)
    {
        Diagnostic.NodeId = Node;
        Diagnostic.Message = Message;
        return false;
    }

    bool ValidateInterface(const FVoxelLandGraphNodeRecord& Call, const FVoxelLandGraphFunctionSnapshot& Function,
        TMap<FGuid, FVoxelLandGraphPinRecord>& Inputs, FVoxelLandGraphPinRecord& Output)
    {
        if (Call.OperatorVersion != 1 || !Call.Configuration.IsEmpty() || Call.ParameterId.IsValid() ||
            Call.SeedSalt != 0 || Call.FunctionPath.IsNull() || Function.SourceVersion != 1 || Function.Inputs.Num() > 63 ||
            Call.Pins.Num() != Function.Inputs.Num() + 1 ||
            (Function.OutputType != EVoxelLandGraphValueType::Float && Function.OutputType != EVoxelLandGraphValueType::Int32 &&
             Function.OutputType != EVoxelLandGraphValueType::Bool))
        {
            return Fail(Call.Id, TEXT("Function call interface or version is incompatible."));
        }
        TMap<FGuid, EVoxelLandGraphValueType> Declared;
        TSet<FString> Names;
        for (const FVoxelLandGraphParameter& Input : Function.Inputs)
        {
            const FString Name = Input.Name.ToLower();
            if (!Input.Id.IsValid() || Declared.Contains(Input.Id) || Name.IsEmpty() || Names.Contains(Name) ||
                (Input.Type != EVoxelLandGraphValueType::Float && Input.Type != EVoxelLandGraphValueType::Int32 &&
                 Input.Type != EVoxelLandGraphValueType::Bool) || !FMath::IsFinite(Input.FloatDefault) ||
                (Input.Type == EVoxelLandGraphValueType::Bool && Input.bHasRange) ||
                (Input.Type == EVoxelLandGraphValueType::Int32 && Input.bHasRange &&
                 (Input.IntMin > Input.IntMax || Input.IntDefault < Input.IntMin || Input.IntDefault > Input.IntMax)) ||
                (Input.Type == EVoxelLandGraphValueType::Float && Input.bHasRange &&
                 (!FMath::IsFinite(Input.FloatMin) || !FMath::IsFinite(Input.FloatMax) || Input.FloatMin > Input.FloatMax ||
                  Input.FloatDefault < Input.FloatMin || Input.FloatDefault > Input.FloatMax)))
            {
                return Fail(Call.Id, TEXT("Function input IDs must be valid and unique."));
            }
            for (int32 Index = 0; Index < Name.Len(); ++Index)
            {
                const TCHAR C = Name[Index];
                if (!((C >= 'a' && C <= 'z') || C == '_' || (Index > 0 && C >= '0' && C <= '9')))
                {
                    return Fail(Call.Id, TEXT("Function input names must be ASCII identifiers."));
                }
            }
            Names.Add(Name);
            Declared.Add(Input.Id, Input.Type);
        }
        int32 Outputs = 0;
        for (const FVoxelLandGraphPinRecord& Pin : Call.Pins)
        {
            if (!Pin.Id.IsValid() || Pin.bInvalidDefault || !FMath::IsFinite(Pin.FloatDefault))
            {
                return Fail(Call.Id, TEXT("Function call contains an invalid pin default or ID."));
            }
            if (Pin.bOutput)
            {
                ++Outputs;
                Output = Pin;
                if (Pin.Type != Function.OutputType)
                {
                    return Fail(Call.Id, TEXT("Function output type changed; repair this call node."));
                }
            }
            else
            {
                const EVoxelLandGraphValueType* Type = Declared.Find(Pin.InterfaceId);
                if (!Type || *Type != Pin.Type || Inputs.Contains(Pin.InterfaceId))
                {
                    return Fail(Call.Id, TEXT("Function inputs changed; repair this call node."));
                }
                Inputs.Add(Pin.InterfaceId, Pin);
            }
        }
        if (Outputs != 1 || Inputs.Num() != Declared.Num())
        {
            return Fail(Call.Id, TEXT("Function call must have its complete typed interface."));
        }
        return true;
    }

    bool ValidateLinks(const TArray<FVoxelLandGraphNodeRecord>& Nodes, const TArray<FVoxelLandGraphLinkRecord>& Links,
        const FGuid& CallId)
    {
        TMap<FGuid, FGuid> Owners;
        TMap<FGuid, const FVoxelLandGraphPinRecord*> Pins;
        TSet<FGuid> NodeIds;
        for (const FVoxelLandGraphNodeRecord& Node : Nodes)
        {
            if (!Node.Id.IsValid() || NodeIds.Contains(Node.Id))
            {
                return Fail(CallId, TEXT("Function graph contains invalid or duplicate node IDs."));
            }
            NodeIds.Add(Node.Id);
            for (const FVoxelLandGraphPinRecord& Pin : Node.Pins)
            {
                if (!Pin.Id.IsValid() || Pins.Contains(Pin.Id))
                {
                    return Fail(CallId, TEXT("Function graph contains invalid or duplicate pin IDs."));
                }
                Pins.Add(Pin.Id, &Pin);
                Owners.Add(Pin.Id, Node.Id);
            }
        }
        TSet<FGuid> Connected;
        for (const FVoxelLandGraphLinkRecord& Link : Links)
        {
            const FVoxelLandGraphPinRecord* const* From = Pins.Find(Link.SourcePin);
            const FVoxelLandGraphPinRecord* const* To = Pins.Find(Link.TargetPin);
            if (!From || !To || Owners.FindRef(Link.SourcePin) != Link.SourceNode ||
                Owners.FindRef(Link.TargetPin) != Link.TargetNode || !(*From)->bOutput || (*To)->bOutput ||
                (*From)->Type != (*To)->Type || Connected.Contains(Link.TargetPin))
            {
                return Fail(CallId, TEXT("Function graph has an invalid, mismatched, or duplicate link."));
            }
            Connected.Add(Link.TargetPin);
        }
        FGuid CycleNode, CyclePin;
        if (FVoxelLandGraphCycle::Find(Nodes, Links, CycleNode, CyclePin))
        {
            return Fail(CallId, TEXT("Function graph contains a cycle."));
        }
        return true;
    }

    bool ExpandCall(int32 CallIndex)
    {
        const FVoxelLandGraphNodeRecord Call = Graph.Nodes[CallIndex];
        const FVoxelLandGraphFunctionSnapshot* Function = Graph.Functions.FindByPredicate([&Call](const FVoxelLandGraphFunctionSnapshot& Candidate)
        {
            return Candidate.AssetPath == Call.FunctionPath.ToString();
        });
        if (!Function)
        {
            return Fail(Call.Id, TEXT("Function dependency was not captured or is unavailable."));
        }
        if (!ValidateLinks(Graph.Nodes, Graph.Links, Call.Id) || !ValidateLinks(Function->Nodes, Function->Links, Call.Id))
        {
            return false;
        }
        TMap<FGuid, FVoxelLandGraphPinRecord> Inputs;
        FVoxelLandGraphPinRecord CallOutput;
        if (!ValidateInterface(Call, *Function, Inputs, CallOutput))
        {
            return false;
        }
        if (Graph.Nodes.Num() + Function->Nodes.Num() > MaxExpandedNodes || Graph.Links.Num() + Function->Links.Num() > MaxExpandedLinks)
        {
            return Fail(Call.Id, TEXT("Expanded function graph exceeds size limits."));
        }
        int64 ExpandedConfigBytes = 0;
        for (const FVoxelLandGraphNodeRecord& Node : Graph.Nodes) { ExpandedConfigBytes += Node.Configuration.Num(); }
        for (const FVoxelLandGraphNodeRecord& Node : Function->Nodes) { ExpandedConfigBytes += Node.Configuration.Num(); }
        if (ExpandedConfigBytes > 1048576)
        {
            return Fail(Call.Id, TEXT("Expanded function configuration exceeds its size limit."));
        }
        TSet<FGuid> ExistingIds;
        for (const FVoxelLandGraphNodeRecord& Node : Graph.Nodes)
        {
            ExistingIds.Add(Node.Id);
            for (const FVoxelLandGraphPinRecord& Pin : Node.Pins) { ExistingIds.Add(Pin.Id); }
        }
        TMap<FGuid, FGuid> NodeIds;
        TMap<FGuid, FGuid> PinIds;
        if (!BuildIdMaps(Call.Id, *Function, ExistingIds, NodeIds, PinIds))
        {
            return false;
        }
        const FGuid* MappedOutputNode = NodeIds.Find(Function->OutputNode);
        const FGuid* MappedOutputPin = PinIds.Find(Function->OutputPin);
        if (!MappedOutputNode || !MappedOutputPin || !HasFunctionOutput(*Function))
        {
            return Fail(Call.Id, TEXT("Function output is missing or has an incompatible type."));
        }
        TMap<FGuid, FGuid> Aliases;
        Aliases.Add(CallOutput.Id, *MappedOutputPin);
        TSet<FGuid> RemovedNodes;
        RemovedNodes.Add(Call.Id);
        TArray<FVoxelLandGraphNodeRecord> Added;
        if (!CopyFunctionNodes(Call, *Function, Inputs, NodeIds, PinIds, Aliases, RemovedNodes, Added))
        {
            return false;
        }
        TArray<FVoxelLandGraphLinkRecord> Links = Graph.Links;
        for (const FVoxelLandGraphLinkRecord& Link : Function->Links)
        {
            const FGuid* SourceNode = NodeIds.Find(Link.SourceNode);
            const FGuid* SourcePin = PinIds.Find(Link.SourcePin);
            const FGuid* TargetNode = NodeIds.Find(Link.TargetNode);
            const FGuid* TargetPin = PinIds.Find(Link.TargetPin);
            if (!SourceNode || !SourcePin || !TargetNode || !TargetPin)
            {
                return Fail(Call.Id, TEXT("Function contains a link to a missing node or pin."));
            }
            FVoxelLandGraphLinkRecord Mapped = Link;
            Mapped.SourceNode = *SourceNode; Mapped.SourcePin = *SourcePin;
            Mapped.TargetNode = *TargetNode; Mapped.TargetPin = *TargetPin;
            Links.Add(MoveTemp(Mapped));
        }
        Graph.Nodes.RemoveAt(CallIndex);
        Graph.Nodes.Append(MoveTemp(Added));
        return RewriteLinks(Call, Inputs, RemovedNodes, Aliases, Links);
    }

    bool BuildIdMaps(const FGuid& CallId, const FVoxelLandGraphFunctionSnapshot& Function, TSet<FGuid>& Existing,
        TMap<FGuid, FGuid>& Nodes, TMap<FGuid, FGuid>& Pins)
    {
        const FGuid* ParentCall = OriginCalls.Find(CallId);
        const FGuid RootCall = ParentCall ? *ParentCall : CallId;
        for (const FVoxelLandGraphNodeRecord& Node : Function.Nodes)
        {
            const FGuid Id = NamespacedId(CallId, Node.Id, 1);
            if (!Node.Id.IsValid() || Nodes.Contains(Node.Id) || !Id.IsValid() || Existing.Contains(Id))
            {
                return Fail(CallId, TEXT("Invalid function node ID or namespaced ID collision."));
            }
            Nodes.Add(Node.Id, Id);
            Existing.Add(Id);
            OriginCalls.Add(Id, RootCall);
            OriginDescriptions.Add(Id, FString::Printf(TEXT("function %s, node %s"), *Function.AssetPath, *Node.Id.ToString()));
            for (const FVoxelLandGraphPinRecord& Pin : Node.Pins)
            {
                const FGuid PinId = NamespacedId(CallId, Pin.Id, 2);
                if (!Pin.Id.IsValid() || Pins.Contains(Pin.Id) || !PinId.IsValid() || Existing.Contains(PinId))
                {
                    return Fail(CallId, TEXT("Invalid function pin ID or namespaced ID collision."));
                }
                Pins.Add(Pin.Id, PinId);
                Existing.Add(PinId);
                OriginCalls.Add(PinId, RootCall);
                OriginDescriptions.Add(PinId, FString::Printf(TEXT("function %s, node %s, pin %s"),
                    *Function.AssetPath, *Node.Id.ToString(), *Pin.Id.ToString()));
            }
        }
        return true;
    }

    bool HasFunctionOutput(const FVoxelLandGraphFunctionSnapshot& Function) const
    {
        for (const FVoxelLandGraphNodeRecord& Node : Function.Nodes)
        {
            if (Node.Id == Function.OutputNode)
            {
                for (const FVoxelLandGraphPinRecord& Pin : Node.Pins)
                {
                    if (Pin.Id == Function.OutputPin && Pin.bOutput && Pin.Type == Function.OutputType)
                    {
                        return true;
                    }
                }
            }
        }
        return false;
    }

    bool CopyFunctionNodes(const FVoxelLandGraphNodeRecord& Call, const FVoxelLandGraphFunctionSnapshot& Function,
        const TMap<FGuid, FVoxelLandGraphPinRecord>& Inputs, const TMap<FGuid, FGuid>& NodeIds,
        const TMap<FGuid, FGuid>& PinIds, TMap<FGuid, FGuid>& Aliases, TSet<FGuid>& RemovedNodes,
        TArray<FVoxelLandGraphNodeRecord>& Added)
    {
        for (const FVoxelLandGraphNodeRecord& Original : Function.Nodes)
        {
            FVoxelLandGraphNodeRecord Node = Original;
            Node.Id = NodeIds.FindChecked(Original.Id);
            for (FVoxelLandGraphPinRecord& Pin : Node.Pins)
            {
                Pin.Id = PinIds.FindChecked(Pin.Id);
            }
            const bool bFloat = Node.OperatorId == VoxelLandBuiltinNodeId(EVoxelLandBuiltinNode::FloatParameter);
            const bool bInt = Node.OperatorId == VoxelLandBuiltinNodeId(EVoxelLandBuiltinNode::IntParameter);
            const bool bBool = Node.OperatorId == VoxelLandBuiltinNodeId(EVoxelLandBuiltinNode::BoolParameter);
            if (bFloat || bInt || bBool)
            {
                const FVoxelLandGraphPinRecord* Input = Inputs.Find(Node.ParameterId);
                const EVoxelLandGraphValueType Type = bFloat ? EVoxelLandGraphValueType::Float :
                    bInt ? EVoxelLandGraphValueType::Int32 : EVoxelLandGraphValueType::Bool;
                if (!Input || Input->Type != Type || Node.OperatorVersion != 1 || !Node.Configuration.IsEmpty() ||
                    !Node.FunctionPath.IsNull() || Node.SeedSalt != 0 || Node.Pins.Num() != 1 || !Node.Pins[0].bOutput ||
                    Node.Pins[0].Type != Type || Node.Pins[0].bInvalidDefault || !FMath::IsFinite(Node.Pins[0].FloatDefault))
                {
                    return Fail(Call.Id, TEXT("Function input reference has a missing or mismatched declaration."));
                }
                Aliases.Add(Node.Pins[0].Id, Input->Id);
                RemovedNodes.Add(Node.Id);
            }
            else
            {
                Added.Add(MoveTemp(Node));
            }
        }
        return true;
    }

    bool RewriteLinks(const FVoxelLandGraphNodeRecord& Call, const TMap<FGuid, FVoxelLandGraphPinRecord>& Inputs,
        const TSet<FGuid>& RemovedNodes, const TMap<FGuid, FGuid>& Aliases, const TArray<FVoxelLandGraphLinkRecord>& Links)
    {
        TMap<FGuid, FVoxelLandGraphLinkRecord> Incoming;
        TMap<FGuid, FVoxelLandGraphPinRecord> Defaults;
        for (const auto& Entry : Inputs) { Defaults.Add(Entry.Value.Id, Entry.Value); }
        for (const FVoxelLandGraphLinkRecord& Link : Links)
        {
            if (Incoming.Contains(Link.TargetPin))
            {
                return Fail(Call.Id, TEXT("Function contains an input with multiple sources."));
            }
            Incoming.Add(Link.TargetPin, Link);
        }
        TMap<FGuid, FGuid> Owners;
        TMap<FGuid, FVoxelLandGraphPinRecord*> Pins;
        for (FVoxelLandGraphNodeRecord& Node : Graph.Nodes)
        {
            for (FVoxelLandGraphPinRecord& Pin : Node.Pins)
            {
                Owners.Add(Pin.Id, Node.Id);
                Pins.Add(Pin.Id, &Pin);
            }
        }
        TArray<FVoxelLandGraphLinkRecord> Rewritten;
        for (const FVoxelLandGraphLinkRecord& Original : Links)
        {
            if (RemovedNodes.Contains(Original.TargetNode)) { continue; }
            FVoxelLandGraphPinRecord** Target = Pins.Find(Original.TargetPin);
            if (!Target || Owners.FindRef(Original.TargetPin) != Original.TargetNode)
            {
                return Fail(Call.Id, TEXT("Function link has an invalid target."));
            }
            FGuid SourcePin = Original.SourcePin;
            const FVoxelLandGraphPinRecord* Default = nullptr;
            if (!ResolveSource(Call.Id, SourcePin, Aliases, Incoming, Defaults, Default))
            {
                return false;
            }
            if (Default)
            {
                if ((*Target)->Type != Default->Type)
                {
                    return Fail(Call.Id, TEXT("Function default type does not match its consumer."));
                }
                (*Target)->FloatDefault = Default->FloatDefault;
                (*Target)->IntDefault = Default->IntDefault;
                (*Target)->BoolDefault = Default->BoolDefault;
                (*Target)->bInvalidDefault = Default->bInvalidDefault;
                continue;
            }
            FVoxelLandGraphPinRecord** Source = Pins.Find(SourcePin);
            if (!Source || !(*Source)->bOutput || (*Target)->bOutput || (*Source)->Type != (*Target)->Type)
            {
                return Fail(Call.Id, TEXT("Function link has an invalid or mismatched source."));
            }
            FVoxelLandGraphLinkRecord Link = Original;
            Link.SourcePin = SourcePin;
            Link.SourceNode = Owners.FindRef(SourcePin);
            Rewritten.Add(MoveTemp(Link));
        }
        Graph.Links = MoveTemp(Rewritten);
        return true;
    }

    bool ResolveSource(const FGuid& CallId, FGuid& SourcePin, const TMap<FGuid, FGuid>& Aliases,
        const TMap<FGuid, FVoxelLandGraphLinkRecord>& Incoming, const TMap<FGuid, FVoxelLandGraphPinRecord>& Defaults,
        const FVoxelLandGraphPinRecord*& OutDefault)
    {
        for (int32 Depth = 0; Depth < MaxExpandedNodes; ++Depth)
        {
            if (const FGuid* Alias = Aliases.Find(SourcePin))
            {
                SourcePin = *Alias;
                continue;
            }
            if (const FVoxelLandGraphPinRecord* Default = Defaults.Find(SourcePin))
            {
                if (const FVoxelLandGraphLinkRecord* Source = Incoming.Find(SourcePin))
                {
                    SourcePin = Source->SourcePin;
                    continue;
                }
                OutDefault = Default;
            }
            return true;
        }
        return Fail(CallId, TEXT("Cyclic function input substitution detected."));
    }

    FVoxelLandGraphSnapshot& Graph;
    FVoxelLandGraphDiagnostic& Diagnostic;
    TMap<FGuid, FGuid>& OriginCalls;
    TMap<FGuid, FString>& OriginDescriptions;
};
}

bool FVoxelLandGraphFunctionFlatten::ValidateSource(const FVoxelLandGraphSnapshot& Source, FVoxelLandGraphDiagnostic& Diagnostic)
{
    Diagnostic = FVoxelLandGraphDiagnostic();
    return FFunctionSnapshotValidator(Source, Diagnostic).Validate();
}
bool FVoxelLandGraphFunctionFlatten::Expand(const FVoxelLandGraphSnapshot& Source, FVoxelLandGraphSnapshot& OutGraph,
    FVoxelLandGraphDiagnostic& Diagnostic)
{
    TMap<FGuid, FGuid> OriginCalls;
    TMap<FGuid, FString> OriginDescriptions;
    return Expand(Source, OutGraph, Diagnostic, OriginCalls, OriginDescriptions);
}
bool FVoxelLandGraphFunctionFlatten::Expand(const FVoxelLandGraphSnapshot& Source, FVoxelLandGraphSnapshot& OutGraph,
    FVoxelLandGraphDiagnostic& Diagnostic, TMap<FGuid, FGuid>& OriginCalls, TMap<FGuid, FString>& OriginDescriptions)
{
    OutGraph = FVoxelLandGraphSnapshot();
    Diagnostic = FVoxelLandGraphDiagnostic();
    OriginCalls.Reset();
    OriginDescriptions.Reset();
    if (!ValidateSource(Source, Diagnostic))
    {
        return false;
    }
    FVoxelLandGraphSnapshot Copy = Source;
    if (!FFunctionExpander(Copy, Diagnostic, OriginCalls, OriginDescriptions).Run())
    {
        TranslateDiagnostic(Diagnostic, OriginCalls, OriginDescriptions);
        return false;
    }
    Copy.Functions.Reset();
    OutGraph = MoveTemp(Copy);
    return true;
}
void FVoxelLandGraphFunctionFlatten::TranslateDiagnostic(FVoxelLandGraphDiagnostic& Diagnostic,
    const TMap<FGuid, FGuid>& OriginCalls, const TMap<FGuid, FString>& OriginDescriptions)
{
    const FGuid Key = OriginCalls.Contains(Diagnostic.PinId) ? Diagnostic.PinId : Diagnostic.NodeId;
    const FGuid* Call = OriginCalls.Find(Key);
    const FString* Context = OriginDescriptions.Find(Key);
    if (Call)
    {
        if (Context) { Diagnostic.Message += FString::Printf(TEXT(" (%s)"), **Context); }
        Diagnostic.NodeId = *Call;
        Diagnostic.PinId.Invalidate();
    }
}
