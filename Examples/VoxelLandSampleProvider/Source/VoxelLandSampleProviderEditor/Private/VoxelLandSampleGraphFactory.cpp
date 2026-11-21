#include "VoxelLandSampleGraphFactory.h"
#include "Data/Assets/VoxelLandTerrainGraph.h"
#include "Data/Enums/VoxelLandBuiltinNode.h"
#include "Graph/VoxelLandGraphCompiler.h"
#include "Misc/PackageName.h"
#include "Operators/VoxelLandOperator.h"
#include "UObject/Package.h"
#include "VoxelLandSampleProviderId.h"

#define LOCTEXT_NAMESPACE "VoxelLandSampleGraphFactory"

DEFINE_LOG_CATEGORY_STATIC(LogVoxelLandSampleGraphFactory, Log, All);

namespace
{
FVoxelLandGraphPinRecord MakePin(const TCHAR* Name, bool bOutput)
{
    FVoxelLandGraphPinRecord Pin;
    Pin.Id = FGuid::NewGuid();
    Pin.Name = Name;
    Pin.Type = EVoxelLandGraphValueType::Float;
    Pin.bOutput = bOutput;
    return Pin;
}

void AddLink(UVoxelLandTerrainGraph& Graph, int32 From, int32 To)
{
    FVoxelLandGraphLinkRecord& Link = Graph.Links.AddDefaulted_GetRef();
    Link.SourceNode = Graph.Nodes[From].Id;
    Link.SourcePin = Graph.Nodes[From].Pins.Last().Id;
    Link.TargetNode = Graph.Nodes[To].Id;
    Link.TargetPin = Graph.Nodes[To].Pins[0].Id;
}

void PopulateSample(UVoxelLandTerrainGraph& Graph)
{
    FVoxelLandGraphNodeRecord Position;
    Position.Id = FGuid::NewGuid();
    Position.OperatorId = VoxelLandBuiltinNodeId(EVoxelLandBuiltinNode::PositionZ);
    Position.Pins.Add(MakePin(TEXT("Result"), true));
    Graph.Nodes.Add(MoveTemp(Position));

    FVoxelLandGraphNodeRecord Double;
    Double.Id = FGuid::NewGuid();
    Double.OperatorId = VoxelLandSampleDoubleId();
    Double.Pins.Add(MakePin(TEXT("Value"), false));
    Double.Pins.Add(MakePin(TEXT("Result"), true));
    Graph.Nodes.Add(MoveTemp(Double));

    FVoxelLandGraphNodeRecord Output;
    Output.Id = FGuid::NewGuid();
    Output.OperatorId = VoxelLandBuiltinNodeId(EVoxelLandBuiltinNode::DensityOutput);
    Output.Pins.Add(MakePin(TEXT("Density"), false));
    Graph.Nodes.Add(MoveTemp(Output));
    AddLink(Graph, 0, 1);
    AddLink(Graph, 1, 2);
}

bool ValidateSample(UVoxelLandTerrainGraph& Graph, FString& Error)
{
    FVoxelLandGraphSnapshot Snapshot;
    FVoxelLandProgramDraft Draft;
    return Graph.CaptureSnapshot(Snapshot, Error) && FVoxelLandGraphCompiler::Compile(Snapshot, Draft, Error);
}
}

UVoxelLandSampleGraphFactory::UVoxelLandSampleGraphFactory()
{
    SupportedClass = UVoxelLandTerrainGraph::StaticClass();
    bCreateNew = true;
    bEditAfterNew = true;
}

FText UVoxelLandSampleGraphFactory::GetDisplayName() const
{
    return LOCTEXT("SampleGraphName", "VoxelLand Sample Provider Graph");
}

UObject* UVoxelLandSampleGraphFactory::FactoryCreateNew(UClass* InClass, UObject* Parent, FName Name,
    EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
    (void)Context;
    (void)Warn;
    if (InClass != UVoxelLandTerrainGraph::StaticClass() || !Parent || Name.IsNone() ||
        FindObjectFast<UObject>(Parent, Name) || FPackageName::DoesPackageExist(Parent->GetOutermost()->GetName()))
    {
        UE_LOG(LogVoxelLandSampleGraphFactory, Error, TEXT("Sample graph destination is invalid or already exists"));
        return nullptr;
    }
    if (!FVoxelLandOperatorRegistry::Resolve(VoxelLandSampleDoubleId(), 1))
    {
        UE_LOG(LogVoxelLandSampleGraphFactory, Error, TEXT("Sample provider version 1 is not registered"));
        return nullptr;
    }
    UVoxelLandTerrainGraph* Candidate = NewObject<UVoxelLandTerrainGraph>(GetTransientPackage());
    PopulateSample(*Candidate);
    FString Error;
    if (!ValidateSample(*Candidate, Error))
    {
        UE_LOG(LogVoxelLandSampleGraphFactory, Error, TEXT("Sample graph cannot compile: %s"), *Error);
        return nullptr;
    }
    UVoxelLandTerrainGraph* Graph = NewObject<UVoxelLandTerrainGraph>(Parent, InClass, Name, Flags | RF_Transactional);
    Graph->Nodes = MoveTemp(Candidate->Nodes);
    Graph->Links = MoveTemp(Candidate->Links);
    return Graph;
}

#undef LOCTEXT_NAMESPACE
