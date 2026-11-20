#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Async/Async.h"
#include "Data/Assets/VoxelLandTerrainGraph.h"
#include "Data/Enums/VoxelLandBuiltinNode.h"
#include "Generation/VoxelLandGenerator.h"
#include "Graph/VoxelLandProgramCodec.h"
#include "Operators/VoxelLandOperator.h"
#include "Operators/VoxelLandSquareOperator.h"
#include "Operators/VoxelLandExampleFilterOperator.h"
#include "Misc/CommandLine.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace
{
FVoxelLandGraphNodeRecord ProviderProbeNode(const FGuid& Operator, uint32 Index, int32 Inputs, bool bOutput)
{
    FVoxelLandGraphNodeRecord Node;
    Node.Id = FGuid(0x6b31f501, 1, 0, Index);
    Node.OperatorId = Operator;
    Node.OperatorVersion = 1;
    Node.SeedSalt = int32(Index * 17);
    for (int32 I = 0; I < Inputs + int32(bOutput); ++I)
    {
        FVoxelLandGraphPinRecord Pin;
        Pin.Id = FGuid(0x6b31f502, 1, Index, I + 1);
        Pin.Name = I == Inputs && bOutput ? TEXT("Result") : TEXT("Density");
        Pin.bOutput = I == Inputs && bOutput;
        Node.Pins.Add(Pin);
    }
    return Node;
}
void ProviderProbeLink(UVoxelLandTerrainGraph& Graph, int32 From, int32 To)
{
    FVoxelLandGraphLinkRecord& Link = Graph.Links.AddDefaulted_GetRef();
    Link.SourceNode = Graph.Nodes[From].Id;
    Link.SourcePin = Graph.Nodes[From].Pins.Last().Id;
    Link.TargetNode = Graph.Nodes[To].Id;
    Link.TargetPin = Graph.Nodes[To].Pins[0].Id;
}
bool ProviderProbeRoot(FString& Root)
{
    if (!FParse::Value(FCommandLine::Get(), TEXT("VoxelLandProviderFixtureRoot="), Root) ||
        !Root.StartsWith(TEXT("/Game/__VoxelLandM3ProviderProbe_")) || !FPackageName::IsValidLongPackageName(Root))
    {
        return false;
    }
    return true;
}
bool ProviderProbeCreateGraph(const FString& PackagePath, const FGuid& Operator, FString& Error)
{
    UPackage* Package = CreatePackage(*PackagePath);
    UVoxelLandTerrainGraph* Graph = NewObject<UVoxelLandTerrainGraph>(Package, *FPackageName::GetLongPackageAssetName(PackagePath), RF_Public | RF_Standalone);
    Graph->Nodes.Add(ProviderProbeNode(VoxelLandBuiltinNodeId(EVoxelLandBuiltinNode::PositionX), 1, 0, true));
    Graph->Nodes.Add(ProviderProbeNode(Operator, 2, 1, true));
    Graph->Nodes.Add(ProviderProbeNode(VoxelLandBuiltinNodeId(EVoxelLandBuiltinNode::DensityOutput), 3, 1, false));
    ProviderProbeLink(*Graph, 0, 1);
    ProviderProbeLink(*Graph, 1, 2);
    const FString Filename = FPackageName::LongPackageNameToFilename(PackagePath, FPackageName::GetAssetPackageExtension());
    if (!IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true))
    {
        Error = TEXT("Could not create isolated fixture directory");
        return false;
    }
    FSavePackageArgs Args;
    Args.TopLevelFlags = RF_Public | RF_Standalone;
    if (!UPackage::SavePackage(Package, Graph, *Filename, Args))
    {
        Error = FString::Printf(TEXT("Failed to save fixture %s"), *PackagePath);
        return false;
    }
    return true;
}
bool ProviderProbeCheck(const FVoxelLandGraphSnapshot& Source, const FVoxelLandSerializedProgram& Payload,
    EVoxelLandOperatorStage ExpectedStage, FString& Error)
{
    TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Program;
    if (!FVoxelLandProgramCodec::Load(Source, Payload, Program, Error)) { return false; }
    const FVoxelLandProgramDraft& Draft = Program->GetData();
    if (Draft.ExternalOperators.Num() != 1 || Draft.ExternalOperators[0].Stage != ExpectedStage)
    {
        Error = TEXT("Cooked provider stage mismatch");
        return false;
    }
    FVoxelLandGeneratorSettings Settings;
    Settings.Program = Program;
    Settings.Profile.VoxelSizeCm = 1024.f;
    TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Generator;
    if (FVoxelLandError Failure = FVoxelLandGenerator::Create(Settings, Generator)) { Error = Failure.Message; return false; }
    TUniquePtr<FVoxelLandWorkerContext> Worker;
    if (FVoxelLandError Failure = Generator->CreateWorker(Worker)) { Error = Failure.Message; return false; }
    FVoxelLandChunkKey Key;
    TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe> Chunk;
    if (FVoxelLandError Failure = Generator->GenerateChunk(Key, *Worker, Chunk)) { Error = Failure.Message; return false; }
    if (!Chunk || Chunk->Density.Num() != FVoxelLandChunkData::SampleCount) { Error = TEXT("Provider chunk incomplete"); return false; }
    const int16 FirstExpected = ExpectedStage == EVoxelLandOperatorStage::Sample ? 32767 : -1024;
    const int16 ThirdExpected = ExpectedStage == EVoxelLandOperatorStage::Sample ? 32767 : 1024;
    if (Chunk->Density[0] != FirstExpected || Chunk->Density[1] != 0 || Chunk->Density[2] != ThirdExpected)
    {
        Error = TEXT("Cooked provider generated unexpected reference samples");
        return false;
    }
    TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Missing;
    const FVoxelLandOperatorRegistrySnapshot EmptyProviders;
    FString MissingError;
    if (FVoxelLandProgramCodec::Load(Source, Payload, EmptyProviders, Missing, MissingError) ||
        !MissingError.Contains(TEXT("Missing, incompatible")))
    {
        Error = FString::Printf(TEXT("Missing provider was not clearly rejected: %s"), *MissingError);
        return false;
    }
    FVoxelLandSerializedProgram Incompatible = Payload;
    ++Incompatible.ExternalOperators[0].Version;
    if (FVoxelLandProgramCodec::Load(Source, Incompatible, Missing, MissingError) ||
        !MissingError.Contains(TEXT("Missing, incompatible")))
    {
        Error = FString::Printf(TEXT("Incompatible provider was not clearly rejected: %s"), *MissingError);
        return false;
    }
    return true;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandProviderCookCreate, "VoxelLand.ProviderCook.Create",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVoxelLandProviderCookCreate::RunTest(const FString& Parameters)
{
    FString Root;
    if (!ProviderProbeRoot(Root))
    {
        AddInfo(TEXT("Provider fixture creation skipped: no valid opt-in fixture root."));
        return true;
    }
    const FString Sample = Root / TEXT("DA_SampleProvider");
    const FString Filter = Root / TEXT("DA_FilterProvider");
    for (const FString& Path : {Sample, Filter})
    {
        if (FindPackage(nullptr, *Path) || FPackageName::DoesPackageExist(Path))
        {
            AddError(FString::Printf(TEXT("Refusing to overwrite fixture package %s"), *Path));
            return false;
        }
    }
    FString Error;
    if (!ProviderProbeCreateGraph(Sample, VoxelLandSquareOperatorId(), Error) ||
        !ProviderProbeCreateGraph(Filter, VoxelLandExampleFilterOperatorId(), Error))
    {
        AddError(Error);
        return false;
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandProviderCookProbe, "VoxelLand.ProviderCook.CookedProbe",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVoxelLandProviderCookProbe::RunTest(const FString& Parameters)
{
    FString Root, CookedContent;
    if (!ProviderProbeRoot(Root) || !FParse::Value(FCommandLine::Get(), TEXT("VoxelLandProviderCookedContent="), CookedContent))
    {
        AddInfo(TEXT("Cooked provider probe skipped: supply fixture root and cooked content directory."));
        return true;
    }
    FPaths::NormalizeDirectoryName(CookedContent);
    FPackageName::RegisterMountPoint(TEXT("/Game/"), CookedContent + TEXT("/"));
    TGuardValue<int32> AllowCooked(GAllowCookedDataInEditorBuilds, 1);
    bool bSuccess = true;
    for (int32 Index = 0; Index < 2; ++Index)
    {
        const FString Name = Index == 0 ? TEXT("DA_SampleProvider") : TEXT("DA_FilterProvider");
        const FString ObjectPath = Root / Name + TEXT(".") + Name;
        UVoxelLandTerrainGraph* Graph = LoadObject<UVoxelLandTerrainGraph>(nullptr, *ObjectPath);
        if (!Graph || !Graph->GetOutermost()->HasAnyPackageFlags(PKG_FilterEditorOnly) || Graph->EditorGraph)
        {
            AddError(FString::Printf(TEXT("Could not load cooked-only graph %s"), *ObjectPath));
            bSuccess = false;
            continue;
        }
        FVoxelLandGraphSnapshot Snapshot;
        FString Error;
        if (!Graph->CaptureSnapshot(Snapshot, Error)) { AddError(Error); bSuccess = false; continue; }
        const FVoxelLandSerializedProgram Payload = Graph->CompiledProgram;
        const bool bChecked = Async(EAsyncExecution::ThreadPool, [Snapshot, Payload, Index, &Error]()
        {
            return ProviderProbeCheck(Snapshot, Payload, Index == 0 ? EVoxelLandOperatorStage::Sample : EVoxelLandOperatorStage::OneCellFilter, Error);
        }).Get();
        if (!bChecked) { AddError(FString::Printf(TEXT("Cooked provider %s failed: %s"), *Name, *Error)); bSuccess = false; }
        else { AddInfo(FString::Printf(TEXT("Cooked %s loaded, generated reference samples, and rejected missing/incompatible providers"), *Name)); }
    }
    FPackageName::UnRegisterMountPoint(TEXT("/Game/"), CookedContent + TEXT("/"));
    return bSuccess;
}
#endif
