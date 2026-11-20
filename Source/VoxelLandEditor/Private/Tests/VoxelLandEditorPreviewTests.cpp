#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Async/Async.h"
#include "UObject/PackageReload.h"
#include "UObject/SavePackage.h"
#include "Data/Structs/VoxelLandReloadTestBarrier.h"
#include "UObject/Linker.h"
#include "UObject/UObjectHash.h"
#include "UObject/GarbageCollection.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Misc/PackageName.h"
#include "HAL/FileManager.h"
#include "AssetToolsModule.h"
#include "VoxelLandAssetFactories.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "World/VoxelLandWorld.h"
#include "Editor.h"
#include "Tests/AutomationEditorCommon.h"
#include "Misc/CommandLine.h"
#include "ScopedTransaction.h"
#include "EdGraph/EdGraph.h"
#include "Graph/VoxelLandEditorNode.h"
#include "Graph/VoxelLandEditorFloat.h"
#include "Graph/VoxelLandFloatTypeInterface.h"
#include "Graph/VoxelLandWorldSettingsDetails.h"
#include "Data/Assets/VoxelLandWorldSettings.h"
#include "PropertyEditorModule.h"
#include "IDetailsView.h"
#include "Data/Assets/VoxelLandMaterialSetup.h"
#include "Materials/Material.h"
#include "ISinglePropertyView.h"
#include <cmath>
#include "Graph/VoxelLandEditorSchema.h"
#include "Data/Enums/VoxelLandBuiltinNode.h"
#include "Components/StaticMeshComponent.h"
#include "Data/Assets/VoxelLandTerrainGraph.h"
#include "Graph/VoxelLandGraphToolkit.h"
#include "Graph/VoxelLandPreviewScene.h"
#include "UObject/Package.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandEditorPayloadDirtyTest, "VoxelLand.M3.Editor.CompiledPayloadDirty",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVoxelLandEditorPayloadDirtyTest::RunTest(const FString& Parameters)
{
    UPackage* Package = CreatePackage(TEXT("/Temp/VoxelLand/CompiledPayloadDirty"));
    UVoxelLandTerrainGraph* Graph = NewObject<UVoxelLandTerrainGraph>(Package, NAME_None, RF_Public | RF_Standalone);
    Graph->CompiledProgram.ProgramVersion = 2;
    Graph->CompiledProgram.Constants.Add(1.0f);
    Package->SetDirtyFlag(false);
    FVoxelLandSerializedProgram Same = Graph->CompiledProgram;
    TestFalse(TEXT("Equivalent payload is not replaced"), FVoxelLandGraphToolkit::StoreCompiledProgram(*Graph, MoveTemp(Same)));
    TestFalse(TEXT("Equivalent payload leaves package clean"), Package->IsDirty());
    FVoxelLandSerializedProgram Changed = Graph->CompiledProgram;
    Changed.Constants[0] = 2.0f;
    TestTrue(TEXT("Changed nested payload is replaced"), FVoxelLandGraphToolkit::StoreCompiledProgram(*Graph, MoveTemp(Changed)));
    TestTrue(TEXT("Changed payload marks package dirty"), Package->IsDirty());
    TestEqual(TEXT("Changed payload is stored"), Graph->CompiledProgram.Constants[0], 2.0f);
    Package->SetDirtyFlag(false);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandEditorPreviewFloorTest, "VoxelLand.M3.Editor.PreviewFloor",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVoxelLandEditorPreviewFloorTest::RunTest(const FString& Parameters)
{
    FVoxelLandPreviewScene Scene;
    const UStaticMeshComponent* Floor = Scene.GetFloorMeshComponent();
    TestNotNull(TEXT("Asset viewer floor component exists"), Floor);
    if (!Floor)
    {
        return false;
    }
    TestFalse(TEXT("Asset viewer floor is absent from the preview world"), Floor->IsRegistered());
    TestFalse(TEXT("Asset viewer environment backdrop is absent from the preview world"), Scene.IsBackdropRegistered());
    if (FPreviewSceneProfile* Profile = Scene.GetCurrentProfile())
    {
        Scene.UpdateScene(*Profile);
        TestFalse(TEXT("Profile refresh cannot re-register the removed floor"), Floor->IsRegistered());
        TestFalse(TEXT("Profile refresh cannot re-register the removed backdrop"), Scene.IsBackdropRegistered());
    }
    else
    {
        AddError(TEXT("Advanced preview profile is unavailable."));
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandEditorUndoPinsTest, "VoxelLand.M3.Editor.UndoTypedPins",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVoxelLandEditorUndoPinsTest::RunTest(const FString& Parameters)
{
    UVoxelLandTerrainGraph* Asset = NewObject<UVoxelLandTerrainGraph>(GetTransientPackage(), NAME_None, RF_Transactional);
    TSharedRef<FVoxelLandGraphToolkit> Toolkit = MakeShared<FVoxelLandGraphToolkit>();
    Toolkit->InitializeForTests(Asset);
    Toolkit->CreatePanelForTests();
    UEdGraph* Graph = Asset->EditorGraph;
    UVoxelLandEditorNode* Node = NewObject<UVoxelLandEditorNode>(Graph, NAME_None, RF_Transactional);
    Node->Kind = int32(EVoxelLandBuiltinNode::SmoothDensity);
    Node->CreateNewGuid();
    Node->AllocateDefaultPins();
    {
        const FScopedTransaction Transaction(NSLOCTEXT("VoxelLandTests", "AddTestNode", "Add test node"));
        Graph->Modify();
        Graph->AddNode(Node, false, false);
        Graph->NotifyGraphChanged();
    }
    UEdGraphPin* Strength = Node->FindPin(TEXT("Strength"));
    UEdGraphPin* Enabled = Node->FindPin(TEXT("Enabled"));
    if (!TestNotNull(TEXT("Float pin exists"), Strength) || !TestNotNull(TEXT("Boolean pin exists"), Enabled))
    {
        Toolkit->Stop();
        return false;
    }
    const UEdGraphSchema* Schema = Graph->GetSchema();
    Schema->TrySetDefaultValue(*Strength, TEXT("0.25"));
    TestEqual(TEXT("Float default edited"), Strength->DefaultValue, FString(TEXT("0.25")));
    const FVoxelLandGraphNodeRecord* EditedRecord = Asset->Nodes.FindByPredicate([Node](const FVoxelLandGraphNodeRecord& Record) { return Record.Id == Node->NodeGuid; });
    TestNotNull(TEXT("Edited node mirrored to asset"), EditedRecord);
    if (EditedRecord)
    {
        const FVoxelLandGraphPinRecord* Pin = EditedRecord->Pins.FindByPredicate([](const FVoxelLandGraphPinRecord& Record) { return Record.Name == TEXT("Strength"); });
        TestTrue(TEXT("Edited float mirrored"), Pin && Pin->FloatDefault == 0.25f);
    }
    const uint64 EditedRevision = Toolkit->GetRevisionForTests();
    TestTrue(TEXT("Undo succeeds"), GEditor->UndoTransaction());
    TestEqual(TEXT("Undo restores float default"), Strength->DefaultValue, FString(TEXT("1")));
    TestTrue(TEXT("Undo invalidates semantic revision"), Toolkit->GetRevisionForTests() > EditedRevision);
    const FVoxelLandGraphNodeRecord* UndoRecord = Asset->Nodes.FindByPredicate([Node](const FVoxelLandGraphNodeRecord& Record) { return Record.Id == Node->NodeGuid; });
    const FVoxelLandGraphPinRecord* UndoPin = UndoRecord ? UndoRecord->Pins.FindByPredicate([](const FVoxelLandGraphPinRecord& Record) { return Record.Name == TEXT("Strength"); }) : nullptr;
    TestTrue(TEXT("Undo restores mirrored float"), UndoPin && UndoPin->FloatDefault == 1.0f);
    TestTrue(TEXT("Redo succeeds"), GEditor->RedoTransaction());
    TestEqual(TEXT("Redo restores float default"), Strength->DefaultValue, FString(TEXT("0.25")));
    FVoxelLandFloatTypeInterface FloatInterface;
    for (float Expected : {1.0e-7f, 1.0e20f, std::nextafter(1.0f, 2.0f)})
    {
        const FString Display = FloatInterface.ToString(Expected);
        const TOptional<float> ParsedDisplay = FloatInterface.FromString(Display, 0.f);
        TestTrue(TEXT("Float widget displays and parses binary32"), ParsedDisplay.IsSet() && ParsedDisplay.GetValue() == Expected);
        Schema->TrySetDefaultValue(*Strength, Display);
        float Parsed = 0.0f;
        TestTrue(TEXT("Float pin round-trips binary32"), LexTryParseString(Parsed, *Strength->DefaultValue) && Parsed == Expected);
    }
    Schema->TrySetDefaultValue(*Strength, TEXT("0.25"));
    Schema->TrySetDefaultValue(*Enabled, TEXT("false"));
    TestEqual(TEXT("Boolean default edited"), Enabled->DefaultValue, FString(TEXT("false")));
    Schema->TrySetDefaultValue(*Strength, TEXT("nan"));
    TestEqual(TEXT("Nonfinite float rejected"), Strength->DefaultValue, FString(TEXT("0.25")));
    UVoxelLandEditorNode* Noise = NewObject<UVoxelLandEditorNode>(Graph, NAME_None, RF_Transactional);
    Noise->Kind = int32(EVoxelLandBuiltinNode::Fbm2);
    Noise->CreateNewGuid();
    Noise->AllocateDefaultPins();
    Graph->AddNode(Noise, false, false);
    Graph->NotifyGraphChanged();
    UEdGraphPin* Octaves = Noise->FindPin(TEXT("Octaves"));
    if (TestNotNull(TEXT("Integer pin exists"), Octaves))
    {
        Schema->TrySetDefaultValue(*Octaves, TEXT("7"));
        TestEqual(TEXT("Integer default edited"), Octaves->DefaultValue, FString(TEXT("7")));
        Schema->TrySetDefaultValue(*Octaves, TEXT("invalid"));
        TestEqual(TEXT("Invalid integer rejected"), Octaves->DefaultValue, FString(TEXT("7")));
        TestTrue(TEXT("Integer undo succeeds"), GEditor->UndoTransaction());
        TestEqual(TEXT("Integer undo restores default"), Octaves->DefaultValue, FString(TEXT("3")));
    }
    const int32 BeforeTopologyUndo = Toolkit->GetPanelRefreshCountForTests();
    UVoxelLandEditorNode* Extra = NewObject<UVoxelLandEditorNode>(Graph, NAME_None, RF_Transactional);
    {
        const FScopedTransaction Transaction(NSLOCTEXT("VoxelLandTests", "AddTopology", "Add topology"));
        Graph->Modify();
        Extra->Kind = int32(EVoxelLandBuiltinNode::Add);
        Extra->CreateNewGuid();
        Extra->AllocateDefaultPins();
        Graph->AddNode(Extra, false, false);
        Graph->NotifyGraphChanged();
    }
    const int32 NodeCount = Graph->Nodes.Num();
    TestTrue(TEXT("Topology undo succeeds"), GEditor->UndoTransaction());
    TestEqual(TEXT("Undo removes node"), Graph->Nodes.Num(), NodeCount - 1);
    TestEqual(TEXT("Undo refreshes graph panel once"), Toolkit->GetPanelRefreshCountForTests(), BeforeTopologyUndo + 1);
    TestTrue(TEXT("Topology redo succeeds"), GEditor->RedoTransaction());
    TestEqual(TEXT("Redo restores node"), Graph->Nodes.Num(), NodeCount);
    TestEqual(TEXT("Redo refreshes graph panel once"), Toolkit->GetPanelRefreshCountForTests(), BeforeTopologyUndo + 2);
    const int32 InitialPins = Extra->Pins.Num();
    Extra->AddVariadicInput();
    const FGuid AddedPin = Extra->Pins[InitialPins - 1]->PinId;
    TestEqual(TEXT("Variadic input added"), Extra->Pins.Num(), InitialPins + 1);
    const int32 BeforePinUndo = Toolkit->GetPanelRefreshCountForTests();
    TestTrue(TEXT("Pin topology undo succeeds"), GEditor->UndoTransaction());
    TestEqual(TEXT("Undo removes variadic input"), Extra->Pins.Num(), InitialPins);
    TestEqual(TEXT("Pin undo refreshes graph panel once"), Toolkit->GetPanelRefreshCountForTests(), BeforePinUndo + 1);
    TestTrue(TEXT("Pin topology redo succeeds"), GEditor->RedoTransaction());
    TestEqual(TEXT("Redo restores variadic input"), Extra->Pins.Num(), InitialPins + 1);
    TestTrue(TEXT("Redo preserves pin identity"), Extra->Pins.ContainsByPredicate([AddedPin](const UEdGraphPin* Pin) { return Pin && Pin->PinId == AddedPin; }));
    const uint64 BeforeMove = Toolkit->GetRevisionForTests();
    {
        const FScopedTransaction Transaction(NSLOCTEXT("VoxelLandTests", "MoveTestNode", "Move test node"));
        Node->Modify();
        Node->NodePosX += 32;
        Graph->NotifyGraphChanged();
    }
    TestTrue(TEXT("Layout undo succeeds"), GEditor->UndoTransaction());
    TestEqual(TEXT("Layout undo does not recompile"), Toolkit->GetRevisionForTests(), BeforeMove);
    Toolkit->Stop();
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandGraphEditingWorkflowTest, "VoxelLand.M3.Editor.GraphEditingWorkflow",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVoxelLandGraphEditingWorkflowTest::RunTest(const FString& Parameters)
{
    UVoxelLandTerrainGraph* Asset = NewObject<UVoxelLandTerrainGraph>(GetTransientPackage(), NAME_None, RF_Transactional);
    TSharedRef<FVoxelLandGraphToolkit> Toolkit = MakeShared<FVoxelLandGraphToolkit>();
    Toolkit->InitializeForTests(Asset);
    Toolkit->CreatePanelForTests();
    UEdGraph* Graph = Asset->EditorGraph;
    UVoxelLandEditorNode* Source = NewObject<UVoxelLandEditorNode>(Graph, NAME_None, RF_Transactional);
    Source->Kind = int32(EVoxelLandBuiltinNode::PositionX); Source->CreateNewGuid(); Source->AllocateDefaultPins();
    Graph->AddNode(Source, false, false);
    UVoxelLandEditorNode* Add = NewObject<UVoxelLandEditorNode>(Graph, NAME_None, RF_Transactional);
    Add->Kind = int32(EVoxelLandBuiltinNode::Add); Add->CreateNewGuid(); Add->AllocateDefaultPins();
    Graph->AddNode(Add, false, false); Graph->NotifyGraphChanged();
    UEdGraphPin* Output = Source->Pins.Last();
    UEdGraphPin* Input = Add->Pins[0];
    {
        const FScopedTransaction Transaction(NSLOCTEXT("VoxelLandTests", "ConnectWorkflow", "Connect workflow"));
        Graph->Modify(); Source->Modify(); Add->Modify(); Output->Modify(); Input->Modify();
        TestTrue(TEXT("Connect pins through schema"), Graph->GetSchema()->TryCreateConnection(Output, Input));
        Graph->NotifyGraphChanged();
    }
    TestTrue(TEXT("Connection established"), Input->LinkedTo.Contains(Output));
    TestTrue(TEXT("Connection undo succeeds"), GEditor->UndoTransaction());
    TestFalse(TEXT("Undo removes link"), Input->LinkedTo.Contains(Output));
    TestTrue(TEXT("Connection redo succeeds"), GEditor->RedoTransaction());
    TestTrue(TEXT("Redo restores link"), Input->LinkedTo.Contains(Output));
    Add->AddVariadicInput();
    UEdGraphPin* Extra = Add->Pins[2];
    const FGuid InputId = Input->PinId;
    const FGuid ExtraId = Extra->PinId;
    Add->MoveVariadicInput(InputId, 1);
    TestTrue(TEXT("Move retains pin identity and wire"), Add->Pins[1] == Input && Input->PinId == InputId && Input->LinkedTo.Contains(Output));
    Add->RemoveVariadicInput(ExtraId);
    TestFalse(TEXT("Removed input ID disappears"), Add->Pins.ContainsByPredicate([ExtraId](const UEdGraphPin* Pin) { return Pin && Pin->PinId == ExtraId; }));
    TestTrue(TEXT("Remaining connection survives removal"), Input->LinkedTo.Contains(Output));
    FString OldClipboard;
    FPlatformApplicationMisc::ClipboardPaste(OldClipboard);
    const int32 Before = Graph->Nodes.Num();
    Toolkit->CopyPasteNodesForTests({Source, Add});
    FPlatformApplicationMisc::ClipboardCopy(*OldClipboard);
    TestEqual(TEXT("Copy and paste creates two nodes"), Graph->Nodes.Num(), Before + 2);
    UVoxelLandEditorNode* NewSource = nullptr;
    UVoxelLandEditorNode* NewAdd = nullptr;
    for (UEdGraphNode* Candidate : Graph->Nodes)
    {
        UVoxelLandEditorNode* Node = Cast<UVoxelLandEditorNode>(Candidate);
        if (!Node || Node == Source || Node == Add) { continue; }
        if (Node->Kind == int32(EVoxelLandBuiltinNode::PositionX)) { NewSource = Node; }
        if (Node->Kind == int32(EVoxelLandBuiltinNode::Add)) { NewAdd = Node; }
    }
    if (TestNotNull(TEXT("Copied source"), NewSource) && TestNotNull(TEXT("Copied add"), NewAdd))
    {
        TestTrue(TEXT("Copied node IDs are fresh"), NewSource->NodeGuid != Source->NodeGuid && NewAdd->NodeGuid != Add->NodeGuid);
        TSet<FGuid> OldPinIds;
        for (UEdGraphPin* Pin : Source->Pins) { OldPinIds.Add(Pin->PinId); }
        for (UEdGraphPin* Pin : Add->Pins) { OldPinIds.Add(Pin->PinId); }
        for (UEdGraphPin* Pin : NewSource->Pins) { TestFalse(TEXT("Copied source pin ID is fresh"), OldPinIds.Contains(Pin->PinId)); }
        for (UEdGraphPin* Pin : NewAdd->Pins) { TestFalse(TEXT("Copied add pin ID is fresh"), OldPinIds.Contains(Pin->PinId)); }
        TestTrue(TEXT("Copied internal link preserved"), NewAdd->Pins[1]->LinkedTo.Contains(NewSource->Pins.Last()));
    }
    Toolkit->Stop();
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandWorldSettingsOverridesTest, "VoxelLand.M3.Editor.NamedOverrides",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVoxelLandWorldSettingsOverridesTest::RunTest(const FString& Parameters)
{
    UVoxelLandTerrainGraph* Graph = NewObject<UVoxelLandTerrainGraph>(GetTransientPackage(), NAME_None, RF_Transactional);
    FVoxelLandGraphParameter& P = Graph->Parameters.AddDefaulted_GetRef();
    P.Id = FGuid::NewGuid(); P.Name = TEXT("height"); P.FloatDefault = 0.5f; P.bHasRange = true; P.FloatMin = 0.f; P.FloatMax = 1.f;
    const FGuid Id = P.Id;
    UVoxelLandWorldSettings* Settings = NewObject<UVoxelLandWorldSettings>(GetTransientPackage(), NAME_None, RF_Transactional);
    Settings->Graph = Graph;
    FPropertyEditorModule& Module = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
    const TSharedPtr<ISinglePropertyView> View = Module.CreateSingleProperty(Settings, GET_MEMBER_NAME_CHECKED(UVoxelLandWorldSettings, Overrides), FSinglePropertyParams());
    if (!TestTrue(TEXT("Override property view created"), View.IsValid())) { return false; }
    TSharedPtr<IPropertyHandle> Handle = View->GetPropertyHandle();
    FVoxelLandWorldSettingsDetails::SetEnabledForTests(Settings, Id, Handle, true);
    TestEqual(TEXT("Enabled override is created"), Settings->Overrides.Num(), 1);
    if (Settings->Overrides.Num() != 1) { return false; }
    TestTrue(TEXT("Override uses parameter GUID and matching type"), Settings->Overrides[0].ParameterId == Id && Settings->Overrides[0].Type == EVoxelLandGraphValueType::Float);
    TestEqual(TEXT("Override starts at parameter default"), Settings->Overrides[0].FloatValue, 0.5f);
    FVoxelLandWorldSettingsDetails::SetFloatForTests(Settings, Id, Handle, 0.75f);
    TestEqual(TEXT("Float override updated"), Settings->Overrides[0].FloatValue, 0.75f);
    FVoxelLandWorldSettingsDetails::SetFloatForTests(Settings, Id, Handle, 2.f);
    TestEqual(TEXT("Out-of-range edit rejected"), Settings->Overrides[0].FloatValue, 0.75f);
    Graph->Parameters[0].Name = TEXT("renamed");
    TestEqual(TEXT("Rename retains GUID-backed override"), Settings->Overrides[0].ParameterId, Id);
    Settings->Overrides[0].FloatValue = 2.f;
    TestFalse(TEXT("Invalid persisted range is diagnosed"), FVoxelLandWorldSettingsDetails::DiagnosticForTests(Settings, Id).IsEmpty());
    Settings->Overrides[0].Type = EVoxelLandGraphValueType::Bool;
    TestFalse(TEXT("Type mismatch is diagnosed"), FVoxelLandWorldSettingsDetails::DiagnosticForTests(Settings, Id).IsEmpty());
    FVoxelLandWorldSettingsDetails::SetEnabledForTests(Settings, Id, Handle, false);
    TestEqual(TEXT("Disabling removes override"), Settings->Overrides.Num(), 0);
    Graph->Parameters[0].Type = EVoxelLandGraphValueType::Int32;
    Graph->Parameters[0].IntDefault = 3; Graph->Parameters[0].IntMin = 1; Graph->Parameters[0].IntMax = 5;
    FVoxelLandWorldSettingsDetails::SetEnabledForTests(Settings, Id, Handle, true);
    FVoxelLandWorldSettingsDetails::SetIntForTests(Settings, Id, Handle, 4);
    TestEqual(TEXT("Integer callback sets matching typed value"), Settings->Overrides[0].IntValue, 4);
    FVoxelLandWorldSettingsDetails::SetIntForTests(Settings, Id, Handle, 6);
    TestEqual(TEXT("Integer range is enforced"), Settings->Overrides[0].IntValue, 4);
    FVoxelLandWorldSettingsDetails::SetFloatForTests(Settings, Id, Handle, 0.25f);
    TestEqual(TEXT("Stale float callback cannot modify integer override"), Settings->Overrides[0].FloatValue, 0.5f);
    FVoxelLandWorldSettingsDetails::SetEnabledForTests(Settings, Id, Handle, false);
    Graph->Parameters[0].Type = EVoxelLandGraphValueType::Bool; Graph->Parameters[0].bHasRange = false;
    FVoxelLandWorldSettingsDetails::SetEnabledForTests(Settings, Id, Handle, true);
    FVoxelLandWorldSettingsDetails::SetBoolForTests(Settings, Id, Handle, true);
    TestTrue(TEXT("Boolean callback sets matching typed value"), Settings->Overrides[0].BoolValue);
    FVoxelLandWorldSettingsDetails::SetIntForTests(Settings, Id, Handle, 2);
    TestEqual(TEXT("Stale integer callback cannot modify bool override"), Settings->Overrides[0].IntValue, 3);
    Settings->Graph = nullptr;
    TestTrue(TEXT("Null graph exposes raw overrides for repair"), FVoxelLandWorldSettingsDetails::NeedsRawOverridesForTests(Settings));
    Settings->Graph = Graph;
    TestFalse(TEXT("Valid override does not require raw repair"), FVoxelLandWorldSettingsDetails::NeedsRawOverridesForTests(Settings));
    const FVoxelLandParameterOverride Duplicate = Settings->Overrides[0];
    Settings->Overrides.Add(Duplicate);
    TestTrue(TEXT("Duplicate exposes raw overrides"), FVoxelLandWorldSettingsDetails::NeedsRawOverridesForTests(Settings));
    Settings->Overrides.RemoveAt(1);
    TSharedRef<IDetailsView> Details = Module.CreateDetailView(FDetailsViewArgs());
    Details->SetObject(Settings);
    const int32 BeforeChange = FVoxelLandWorldSettingsDetails::GetRefreshNotificationsForTests();
    {
        const FScopedTransaction Transaction(NSLOCTEXT("VoxelLandTests", "RenameExternal", "Rename external parameter"));
        Graph->Modify();
        Graph->Parameters[0].Name = TEXT("external_rename");
        Graph->PostEditChange();
    }
    TestTrue(TEXT("External rename requests details refresh"), FVoxelLandWorldSettingsDetails::GetRefreshNotificationsForTests() > BeforeChange);
    const int32 BeforeUndo = FVoxelLandWorldSettingsDetails::GetRefreshNotificationsForTests();
    TestTrue(TEXT("External declaration undo succeeds"), GEditor->UndoTransaction());
    TestTrue(TEXT("External undo requests details refresh"), FVoxelLandWorldSettingsDetails::GetRefreshNotificationsForTests() > BeforeUndo);
    int32 BeforeTypeChange = FVoxelLandWorldSettingsDetails::GetRefreshNotificationsForTests();
    Graph->Parameters[0].Type = EVoxelLandGraphValueType::Int32;
    Graph->PostEditChange();
    TestTrue(TEXT("External type change requests details refresh"), FVoxelLandWorldSettingsDetails::GetRefreshNotificationsForTests() > BeforeTypeChange);
    TestFalse(TEXT("External type change is diagnosed"), FVoxelLandWorldSettingsDetails::DiagnosticForTests(Settings, Id).IsEmpty());
    const int32 BeforeRemoval = FVoxelLandWorldSettingsDetails::GetRefreshNotificationsForTests();
    Graph->Parameters.Reset();
    Graph->PostEditChange();
    TestTrue(TEXT("External removal requests details refresh"), FVoxelLandWorldSettingsDetails::GetRefreshNotificationsForTests() > BeforeRemoval);
    TestTrue(TEXT("Removed declaration exposes raw repair"), FVoxelLandWorldSettingsDetails::NeedsRawOverridesForTests(Settings));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandPreviewSettingsUndoTest, "VoxelLand.M3.Editor.PreviewSettingsUndo",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVoxelLandPreviewSettingsUndoTest::RunTest(const FString& Parameters)
{
    UVoxelLandTerrainGraph* Asset = NewObject<UVoxelLandTerrainGraph>(GetTransientPackage(), NAME_None, RF_Transactional);
    TSharedRef<FVoxelLandGraphToolkit> Toolkit = MakeShared<FVoxelLandGraphToolkit>();
    Toolkit->InitializeForTests(Asset);
    UVoxelLandWorldSettings* Settings = Toolkit->GetPreviewSettingsForTests();
    Toolkit->SetPreviewRadiusForTests(2, true);
    TestEqual(TEXT("Radius control updates settings"), Settings->HorizontalRadius, 2);
    TestEqual(TEXT("Radius control updates preview"), Toolkit->GetPreviewHorizontalRadiusForTests(), 2);
    Toolkit->SetPreviewSeedForTests(9876);
    TestEqual(TEXT("Seed edit preserves radius"), Toolkit->GetPreviewHorizontalRadiusForTests(), 2);
    TestTrue(TEXT("Seed control undo succeeds"), GEditor->UndoTransaction());
    TestEqual(TEXT("Seed undo preserves radius"), Toolkit->GetPreviewHorizontalRadiusForTests(), 2);
    TestTrue(TEXT("Radius control undo succeeds"), GEditor->UndoTransaction());
    TestEqual(TEXT("Radius undo restores both settings and preview"), Toolkit->GetPreviewHorizontalRadiusForTests(), 1);
    TestEqual(TEXT("Radius undo restores settings"), Settings->HorizontalRadius, 1);
    const uint64 Initial = Toolkit->GetPreviewRevisionForTests();
    {
        const FScopedTransaction Transaction(NSLOCTEXT("VoxelLandTests", "ChangePreviewSeed", "Change preview seed"));
        Settings->Modify(); Settings->Seed += 1;
        Toolkit->SynchronizePreviewForTests();
    }
    const uint64 Changed = Toolkit->GetPreviewRevisionForTests();
    TestTrue(TEXT("Seed edit invalidates preview"), Changed > Initial);
    TestTrue(TEXT("Seed undo succeeds"), GEditor->UndoTransaction());
    const uint64 Undone = Toolkit->GetPreviewRevisionForTests();
    TestTrue(TEXT("Seed undo invalidates preview"), Undone > Changed);
    TestTrue(TEXT("Seed redo succeeds"), GEditor->RedoTransaction());
    TestTrue(TEXT("Seed redo invalidates preview"), Toolkit->GetPreviewRevisionForTests() > Undone);
    UVoxelLandMaterialSetup* Material = NewObject<UVoxelLandMaterialSetup>(GetTransientPackage(), NAME_None, RF_Transient | RF_Transactional);
    const uint64 BeforeMaterial = Toolkit->GetPreviewRevisionForTests();
    const int32 BeforeMaterialApply = Toolkit->GetMaterialApplyCountForTests();
    {
        const FScopedTransaction Transaction(NSLOCTEXT("VoxelLandTests", "ChangePreviewMaterial", "Change preview material"));
        Settings->Modify(); Settings->MaterialSetup = Material;
        Toolkit->SynchronizePreviewForTests();
    }
    TestEqual(TEXT("Material change does not regenerate density"), Toolkit->GetPreviewRevisionForTests(), BeforeMaterial);
    TestEqual(TEXT("Material change applies material"), Toolkit->GetMaterialApplyCountForTests(), BeforeMaterialApply + 1);
    TestTrue(TEXT("Material undo succeeds"), GEditor->UndoTransaction());
    TestEqual(TEXT("Material undo does not regenerate density"), Toolkit->GetPreviewRevisionForTests(), BeforeMaterial);
    TestEqual(TEXT("Material undo reapplies material"), Toolkit->GetMaterialApplyCountForTests(), BeforeMaterialApply + 2);
    TestTrue(TEXT("Material redo succeeds"), GEditor->RedoTransaction());
    TestEqual(TEXT("Material redo does not regenerate density"), Toolkit->GetPreviewRevisionForTests(), BeforeMaterial);
    TestEqual(TEXT("Material redo reapplies material"), Toolkit->GetMaterialApplyCountForTests(), BeforeMaterialApply + 3);
    int32 BeforeSetupEdit = Toolkit->GetMaterialApplyCountForTests();
    {
        const FScopedTransaction Transaction(NSLOCTEXT("VoxelLandTests", "EditSetupMaterial", "Edit setup material"));
        Material->Modify(); Material->Material = UMaterial::GetDefaultMaterial(MD_Surface); Material->PostEditChange();
    }
    TestTrue(TEXT("Setup material edit is observed"), Toolkit->GetMaterialApplyCountForTests() > BeforeSetupEdit);
    TestEqual(TEXT("Setup material edit updates picker"), Toolkit->GetMaterialPickerPathForTests(), UMaterial::GetDefaultMaterial(MD_Surface)->GetPathName());
    TestEqual(TEXT("Setup material edit does not regenerate density"), Toolkit->GetPreviewRevisionForTests(), BeforeMaterial);
    BeforeSetupEdit = Toolkit->GetMaterialApplyCountForTests();
    TestTrue(TEXT("Setup material undo succeeds"), GEditor->UndoTransaction());
    TestTrue(TEXT("Setup material undo reapplies"), Toolkit->GetMaterialApplyCountForTests() > BeforeSetupEdit);
    TestTrue(TEXT("Setup material undo clears picker"), Toolkit->GetMaterialPickerPathForTests().IsEmpty());
    TestEqual(TEXT("Setup material undo does not regenerate density"), Toolkit->GetPreviewRevisionForTests(), BeforeMaterial);
    BeforeSetupEdit = Toolkit->GetMaterialApplyCountForTests();
    {
        const FScopedTransaction Transaction(NSLOCTEXT("VoxelLandTests", "EditSetupVersion", "Edit setup version"));
        Material->Modify(); Material->FormatVersion = 2; Material->PostEditChange();
    }
    TestTrue(TEXT("Setup version edit is observed"), Toolkit->GetMaterialApplyCountForTests() > BeforeSetupEdit);
    TestEqual(TEXT("Setup version edit does not regenerate density"), Toolkit->GetPreviewRevisionForTests(), BeforeMaterial);
    BeforeSetupEdit = Toolkit->GetMaterialApplyCountForTests();
    TestTrue(TEXT("Setup version undo succeeds"), GEditor->UndoTransaction());
    TestTrue(TEXT("Setup version undo reapplies"), Toolkit->GetMaterialApplyCountForTests() > BeforeSetupEdit);
    TestEqual(TEXT("Setup version undo does not regenerate density"), Toolkit->GetPreviewRevisionForTests(), BeforeMaterial);
    uint64 BeforeDensity = Toolkit->GetPreviewRevisionForTests();
    {
        const FScopedTransaction Transaction(NSLOCTEXT("VoxelLandTests", "ChangePreviewBounds", "Change preview bounds"));
        Settings->Modify(); Settings->VoxelSizeCm = 125.f; Settings->HorizontalRadius = 2;
        Toolkit->SynchronizePreviewForTests();
    }
    TestTrue(TEXT("Voxel size and radius change regenerate"), Toolkit->GetPreviewRevisionForTests() > BeforeDensity);
    BeforeDensity = Toolkit->GetPreviewRevisionForTests();
    TestTrue(TEXT("Density settings undo succeeds"), GEditor->UndoTransaction());
    TestTrue(TEXT("Density settings undo regenerates"), Toolkit->GetPreviewRevisionForTests() > BeforeDensity);
    BeforeDensity = Toolkit->GetPreviewRevisionForTests();
    {
        const FScopedTransaction Transaction(NSLOCTEXT("VoxelLandTests", "ChangePreviewOverride", "Change preview override"));
        Settings->Modify();
        FVoxelLandParameterOverride& Override = Settings->Overrides.AddDefaulted_GetRef();
        Override.ParameterId = FGuid::NewGuid(); Override.FloatValue = 1.f;
        Toolkit->SynchronizePreviewForTests();
    }
    TestTrue(TEXT("Override change regenerates"), Toolkit->GetPreviewRevisionForTests() > BeforeDensity);
    BeforeDensity = Toolkit->GetPreviewRevisionForTests();
    TestTrue(TEXT("Override undo succeeds"), GEditor->UndoTransaction());
    TestTrue(TEXT("Override undo regenerates"), Toolkit->GetPreviewRevisionForTests() > BeforeDensity);
    Toolkit->Stop();
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandPreviewPickersTest, "VoxelLand.M3.Editor.PreviewPickers",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVoxelLandPreviewPickersTest::RunTest(const FString& Parameters)
{
    UVoxelLandTerrainGraph* Graph = NewObject<UVoxelLandTerrainGraph>(GetTransientPackage(), NAME_None, RF_Transactional);
    TSharedRef<FVoxelLandGraphToolkit> Toolkit = MakeShared<FVoxelLandGraphToolkit>();
    Toolkit->InitializeForTests(Graph);
    TestTrue(TEXT("No selected material initially"), Toolkit->GetMaterialPickerPathForTests().IsEmpty());
    TestTrue(TEXT("No selected settings initially"), Toolkit->GetSettingsPickerPathForTests().IsEmpty());
    UMaterialInterface* Material = UMaterial::GetDefaultMaterial(MD_Surface);
    const uint64 BeforeMaterial = Toolkit->GetPreviewRevisionForTests();
    Toolkit->SetMaterialPickerForTests(Material);
    TestEqual(TEXT("Material picker displays selected material"), Toolkit->GetMaterialPickerPathForTests(), Material->GetPathName());
    TestEqual(TEXT("Material selection does not regenerate density"), Toolkit->GetPreviewRevisionForTests(), BeforeMaterial);
    UMaterial* Unsupported = NewObject<UMaterial>(GetTransientPackage(), NAME_None, RF_Transient);
    Unsupported->MaterialDomain = MD_PostProcess;
    Toolkit->SetMaterialPickerForTests(Unsupported);
    TestEqual(TEXT("Unsupported selection remains visible"), Toolkit->GetMaterialPickerPathForTests(), Unsupported->GetPathName());
    TestTrue(TEXT("Unsupported material uses surface fallback"), Toolkit->GetEffectiveMaterialForTests() == Material);
    Toolkit->SetMaterialPickerForTests(nullptr);
    TestTrue(TEXT("Clearing material clears selected path"), Toolkit->GetMaterialPickerPathForTests().IsEmpty());
    TestTrue(TEXT("Clearing material uses surface fallback"), Toolkit->GetEffectiveMaterialForTests() == Material);
    TestEqual(TEXT("Clearing material does not regenerate density"), Toolkit->GetPreviewRevisionForTests(), BeforeMaterial);
    UVoxelLandWorldSettings* Source = NewObject<UVoxelLandWorldSettings>(GetTransientPackage(), NAME_None, RF_Transient);
    Source->Graph = Graph; Source->Seed = 5678;
    Toolkit->SetSettingsPickerForTests(Source);
    TestEqual(TEXT("Settings picker displays original asset"), Toolkit->GetSettingsPickerPathForTests(), Source->GetPathName());
    TestTrue(TEXT("Preview settings use separate editable copy"), Toolkit->GetPreviewSettingsForTests() != Source && Toolkit->GetPreviewSettingsForTests()->Seed == 5678);
    Toolkit->SetSettingsPickerForTests(nullptr);
    TestTrue(TEXT("Clearing settings clears selected asset"), Toolkit->GetSettingsPickerPathForTests().IsEmpty());
    TestTrue(TEXT("Clearing settings restores defaults for current graph"), Toolkit->GetPreviewSettingsForTests()->Graph == Graph && Toolkit->GetPreviewSettingsForTests()->Seed == 1234);
    Toolkit->Stop();
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandDiagnosticFocusTest, "VoxelLand.M3.Editor.DiagnosticFocus",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVoxelLandDiagnosticFocusTest::RunTest(const FString& Parameters)
{
    UVoxelLandTerrainGraph* Asset = NewObject<UVoxelLandTerrainGraph>(GetTransientPackage(), NAME_None, RF_Transactional);
    TSharedRef<FVoxelLandGraphToolkit> Toolkit = MakeShared<FVoxelLandGraphToolkit>();
    Toolkit->InitializeForTests(Asset);
    UEdGraphNode* Expected = Asset->EditorGraph->Nodes[0];
    UEdGraphPin* ExpectedPin = Expected->Pins[0];
    ExpectedPin->DefaultValue = TEXT("not-a-number");
    Asset->EditorGraph->NotifyGraphChanged();
    TestTrue(TEXT("Invalid editor literal records node and pin"), Toolkit->GetDiagnosticForTests().NodeId == Expected->NodeGuid &&
        Toolkit->GetDiagnosticForTests().PinId == ExpectedPin->PinId && !Toolkit->GetDiagnosticForTests().Message.IsEmpty());
    UEdGraphNode* CapturedNode = nullptr; UEdGraphPin* CapturedPin = nullptr;
    TestTrue(TEXT("Invalid literal resolves to exact editor pin"), Toolkit->ResolveDiagnosticForTests(CapturedNode, CapturedPin) &&
        CapturedNode == Expected && CapturedPin == ExpectedPin);
    FVoxelLandGraphDiagnostic Diagnostic; Diagnostic.NodeId = Expected->NodeGuid; Diagnostic.PinId = ExpectedPin->PinId;
    Toolkit->SetDiagnosticForTests(Diagnostic);
    UEdGraphNode* Node = nullptr; UEdGraphPin* Pin = nullptr;
    TestTrue(TEXT("Diagnostic resolves"), Toolkit->ResolveDiagnosticForTests(Node, Pin));
    TestTrue(TEXT("Diagnostic selects exact node and pin"), Node == Expected && Pin == ExpectedPin);
    Diagnostic.PinId = FGuid::NewGuid(); Toolkit->SetDiagnosticForTests(Diagnostic);
    TestTrue(TEXT("Missing pin falls back to existing node"), Toolkit->ResolveDiagnosticForTests(Node, Pin) && Node == Expected && !Pin);
    Diagnostic.NodeId = FGuid::NewGuid(); Toolkit->SetDiagnosticForTests(Diagnostic);
    TestFalse(TEXT("Unknown node cannot focus"), Toolkit->ResolveDiagnosticForTests(Node, Pin));
    Toolkit->Stop();
    return true;
}
namespace
{
class FVoxelLandNoiseFocusCommand final : public IAutomationLatentCommand
{
public:
    FVoxelLandNoiseFocusCommand(FAutomationTestBase* InTest, TSharedRef<FVoxelLandGraphToolkit> InToolkit,
        const FGuid& InNode, const FGuid& InPin)
        : Test(InTest), Toolkit(MoveTemp(InToolkit)), NodeId(InNode), PinId(InPin), Deadline(FPlatformTime::Seconds() + 10.0) {}
    virtual bool Update() override
    {
        Toolkit->PollJobsForTests();
        if (Toolkit->HasCompileWorkForTests())
        {
            if (FPlatformTime::Seconds() <= Deadline) { return false; }
            Test->AddError(TEXT("Noise diagnostic compilation timed out"));
            Toolkit->Stop();
            return true;
        }
        const FVoxelLandGraphDiagnostic& Diagnostic = Toolkit->GetDiagnosticForTests();
        Test->TestTrue(TEXT("Async noise error identifies node and frequency pin"),
            Diagnostic.NodeId == NodeId && Diagnostic.PinId == PinId && Diagnostic.Message.Contains(TEXT("frequency")));
        UEdGraphNode* Node = nullptr; UEdGraphPin* Pin = nullptr;
        Test->TestTrue(TEXT("Noise diagnostic resolves its actual graph pin"),
            Toolkit->ResolveDiagnosticForTests(Node, Pin) && Node && Pin && Node->NodeGuid == NodeId && Pin->PinId == PinId);
        Toolkit->FocusErrorForTests();
        Test->TestTrue(TEXT("Invalid noise leaves preview stale"), Toolkit->GetStatusForTests().Contains(TEXT("stale")));
        Toolkit->Stop();
        return true;
    }
private:
    FAutomationTestBase* Test;
    TSharedRef<FVoxelLandGraphToolkit> Toolkit;
    FGuid NodeId;
    FGuid PinId;
    double Deadline;
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandNoiseFocusTest, "VoxelLand.M3.Editor.NoiseDiagnosticFocus",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVoxelLandNoiseFocusTest::RunTest(const FString& Parameters)
{
    UVoxelLandTerrainGraph* Source = LoadObject<UVoxelLandTerrainGraph>(nullptr, TEXT("/VoxelLand/Examples/DA_M3_Hills.DA_M3_Hills"));
    if (!Source) { AddError(TEXT("Example graph unavailable for noise diagnostic")); return false; }
    UVoxelLandTerrainGraph* Asset = DuplicateObject<UVoxelLandTerrainGraph>(Source, GetTransientPackage());
    TSharedRef<FVoxelLandGraphToolkit> Toolkit = MakeShared<FVoxelLandGraphToolkit>();
    Toolkit->InitializeForTests(Asset);
    Toolkit->CreatePanelForTests();
    UVoxelLandEditorNode* Noise = nullptr;
    for (UEdGraphNode* Candidate : Asset->EditorGraph->Nodes)
    {
        UVoxelLandEditorNode* Node = Cast<UVoxelLandEditorNode>(Candidate);
        if (Node && Node->Kind == int32(EVoxelLandBuiltinNode::Fbm2)) { Noise = Node; break; }
    }
    if (!TestNotNull(TEXT("fBm 2D node exists"), Noise)) { Toolkit->Stop(); return false; }
    UEdGraphPin* Frequency = Noise->FindPin(TEXT("Frequency"));
    if (!TestNotNull(TEXT("Frequency pin exists"), Frequency)) { Toolkit->Stop(); return false; }
    Asset->EditorGraph->GetSchema()->TrySetDefaultValue(*Frequency, TEXT("-1"));
    Toolkit->RequestCompileForTests();
    ADD_LATENT_AUTOMATION_COMMAND(FVoxelLandNoiseFocusCommand(this, Toolkit, Noise->NodeGuid, Frequency->PinId));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandReloadLifecycleTest, "VoxelLand.M3.Editor.ReloadLifecycle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVoxelLandReloadLifecycleTest::RunTest(const FString& Parameters)
{
    UPackage* Package = CreatePackage(TEXT("/Temp/VoxelLand/ReloadLifecycle"));
    UVoxelLandTerrainGraph* Asset = NewObject<UVoxelLandTerrainGraph>(Package, NAME_None, RF_Transactional);
    TSharedRef<FVoxelLandGraphToolkit> Toolkit = MakeShared<FVoxelLandGraphToolkit>();
    Toolkit->InitializeForTests(Asset);
    TSharedRef<TAtomic<bool>, ESPMode::ThreadSafe> Completed = MakeShared<TAtomic<bool>, ESPMode::ThreadSafe>(false);
    Toolkit->AddWorkerForTests(Async(EAsyncExecution::ThreadPool, [Completed]() { FPlatformProcess::Sleep(0.02f); Completed->Store(true); }));
    const uint64 BeforeReload = Toolkit->GetPreviewRevisionForTests();
    FPackageReloadedEvent Event(Package, nullptr, TMap<UObject*, UObject*>());
    Toolkit->PackageReloadForTests(EPackageReloadPhase::PrePackageLoad, &Event);
    TestTrue(TEXT("Reload pauses and joins background work"), Toolkit->IsReloadPausedForTests() && Completed->Load());
    TestTrue(TEXT("Reload invalidates prior preview"), Toolkit->GetPreviewRevisionForTests() > BeforeReload);
    Toolkit->PackageReloadForTests(EPackageReloadPhase::PostBatchPostGC, nullptr);
    TestFalse(TEXT("Failed or completed reload resumes"), Toolkit->IsReloadPausedForTests());
    UVoxelLandTerrainGraph* Replacement = NewObject<UVoxelLandTerrainGraph>(GetTransientPackage(), NAME_None, RF_Transactional);
    TMap<UObject*, UObject*> Replacements; Replacements.Add(Asset, Replacement);
    Toolkit->OnReplacedForTests(Replacements);
    TestTrue(TEXT("Replaced graph stops editor and drains work"), Toolkit->IsStoppedForTests());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandRealPackageReloadTest, "VoxelLand.M3.Editor.RealPackageReload",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVoxelLandRealPackageReloadTest::RunTest(const FString& Parameters)
{
    if (!FApp::CanEverRender())
    {
        AddWarning(TEXT("Real package reload test requires a native window and was not exercised with NullRHI"));
        return true;
    }
    const FString Unique = FGuid::NewGuid().ToString(EGuidFormats::Digits);
    const FString GraphName = TEXT("/Game/__VoxelLandReloadGraph_") + Unique;
    const FString SetupName = TEXT("/Game/__VoxelLandReloadSetup_") + Unique;
    const FString GraphFile = FPackageName::LongPackageNameToFilename(GraphName, FPackageName::GetAssetPackageExtension());
    const FString SetupFile = FPackageName::LongPackageNameToFilename(SetupName, FPackageName::GetAssetPackageExtension());
    if (IFileManager::Get().FileExists(*GraphFile) || IFileManager::Get().FileExists(*SetupFile) || FindPackage(nullptr, *GraphName) || FindPackage(nullptr, *SetupName))
    {
        AddError(TEXT("Unique reload fixture path already exists"));
        return false;
    }
    UVoxelLandTerrainGraph* Source = LoadObject<UVoxelLandTerrainGraph>(nullptr, TEXT("/VoxelLand/Examples/DA_M3_Hills.DA_M3_Hills"));
    if (!Source)
    {
        AddError(TEXT("Example graph unavailable for isolated reload fixture"));
        return false;
    }
    UPackage* GraphPackage = CreatePackage(*GraphName);
    UVoxelLandTerrainGraph* Asset = DuplicateObject<UVoxelLandTerrainGraph>(Source, GraphPackage, *FPackageName::GetLongPackageAssetName(GraphName));
    Asset->SetFlags(RF_Public | RF_Standalone);
    UPackage* SetupPackage = CreatePackage(*SetupName);
    UVoxelLandMaterialSetup* Setup = NewObject<UVoxelLandMaterialSetup>(SetupPackage, *FPackageName::GetLongPackageAssetName(SetupName), RF_Public | RF_Standalone);
    FSavePackageArgs Args;
    Args.TopLevelFlags = RF_Public | RF_Standalone;
    const bool bGraphSaved = UPackage::SavePackage(GraphPackage, Asset, *GraphFile, Args);
    const bool bSetupSaved = UPackage::SavePackage(SetupPackage, Setup, *SetupFile, Args);
    if (TestTrue(TEXT("Unique fixture packages saved"), bGraphSaved && bSetupSaved))
    {
        TSharedRef<FVoxelLandGraphToolkit> Toolkit = MakeShared<FVoxelLandGraphToolkit>();
        Toolkit->InitializeForTests(Asset);
        Toolkit->GetPreviewSettingsForTests()->MaterialSetup = Setup;
        Toolkit->SynchronizePreviewForTests();
        const int32 BeforeMaterial = Toolkit->GetPreviewStartCountForTests();
        UPackage* ReloadedSetup = ReloadPackage(SetupPackage, LOAD_None);
        TestNotNull(TEXT("Unreal reloaded material setup package"), ReloadedSetup);
        TestFalse(TEXT("Material reload keeps editor available"), Toolkit->IsStoppedForTests());
        TestTrue(TEXT("Material setup reference repointed to reloaded package"),
            Toolkit->GetPreviewSettingsForTests()->MaterialSetup &&
            Toolkit->GetPreviewSettingsForTests()->MaterialSetup->GetOutermost() == ReloadedSetup);
        TestFalse(TEXT("Material reload completes pause"), Toolkit->IsReloadPausedForTests());
        TestEqual(TEXT("Material-only reload does not start density generation"), Toolkit->GetPreviewStartCountForTests(), BeforeMaterial);
        Toolkit->RequestCompileForTests();
        TestTrue(TEXT("Compile result pending before graph reload"), Toolkit->HasCompileWorkForTests());
        TSharedRef<FVoxelLandReloadTestBarrier, ESPMode::ThreadSafe> PreviewBarrier = MakeShared<FVoxelLandReloadTestBarrier, ESPMode::ThreadSafe>();
        Toolkit->SetPreviewReloadBarrierForTests(PreviewBarrier);
        Toolkit->StartPreviewForTests();
        UVoxelLandWorldSettings* SourceSettings = LoadObject<UVoxelLandWorldSettings>(nullptr, TEXT("/VoxelLand/Examples/DA_M3_WorldSettings.DA_M3_WorldSettings"));
        UVoxelLandWorldSettings* Settings = SourceSettings ? DuplicateObject<UVoxelLandWorldSettings>(SourceSettings, GetTransientPackage()) : nullptr;
        UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
        AVoxelLandWorld* Actor = nullptr;
        bool bWorldRegistered = false;
        TSharedRef<FVoxelLandReloadTestBarrier, ESPMode::ThreadSafe> InitBarrier = MakeShared<FVoxelLandReloadTestBarrier, ESPMode::ThreadSafe>();
        if (TestNotNull(TEXT("Example settings for reload"), Settings) && TestNotNull(TEXT("Test world"), World))
        {
            Settings->Graph = Asset;
            World->AddToRoot();
            bWorldRegistered = true;
            FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
            Context.SetCurrentWorld(World);
            World->InitializeActorsForPlay(FURL());
            Actor = World->SpawnActor<AVoxelLandWorld>();
            if (TestNotNull(TEXT("Terrain actor"), Actor))
            {
                Actor->bDataOnly = true;
                Actor->DispatchBeginPlay();
                Actor->SetInitReloadBarrierForTests(InitBarrier);
                TestTrue(TEXT("World settings initialization starts"), Actor->InitializeFromSettings(Settings));
            }
        }
        const double JobsDeadline = FPlatformTime::Seconds() + 5.0;
        while ((!PreviewBarrier->Started.Load() || !InitBarrier->Started.Load()) && FPlatformTime::Seconds() < JobsDeadline)
        {
            FPlatformProcess::Sleep(0.001f);
        }
        TestTrue(TEXT("Actual preview worker reached its generation checkpoint"), PreviewBarrier->Started.Load());
        TestTrue(TEXT("Actual settings worker reached its initialization checkpoint"), InitBarrier->Started.Load());
        TSharedRef<TAtomic<bool>, ESPMode::ThreadSafe> Started = MakeShared<TAtomic<bool>, ESPMode::ThreadSafe>(false);
        TSharedRef<TAtomic<bool>, ESPMode::ThreadSafe> Draining = MakeShared<TAtomic<bool>, ESPMode::ThreadSafe>(false);
        TSharedRef<TAtomic<bool>, ESPMode::ThreadSafe> Release = MakeShared<TAtomic<bool>, ESPMode::ThreadSafe>(false);
        TSharedRef<TAtomic<bool>, ESPMode::ThreadSafe> ReloadReturned = MakeShared<TAtomic<bool>, ESPMode::ThreadSafe>(false);
        TSharedRef<TAtomic<bool>, ESPMode::ThreadSafe> ObservedWait = MakeShared<TAtomic<bool>, ESPMode::ThreadSafe>(false);
        TSharedRef<TAtomic<bool>, ESPMode::ThreadSafe> TimedOut = MakeShared<TAtomic<bool>, ESPMode::ThreadSafe>(false);
        TSharedRef<TAtomic<bool>, ESPMode::ThreadSafe> Completed = MakeShared<TAtomic<bool>, ESPMode::ThreadSafe>(false);
        Toolkit->SetReloadDrainingForTests([Draining]() { Draining->Store(true); });
        Toolkit->AddWorkerForTests(Async(EAsyncExecution::ThreadPool, [Started, Release, TimedOut, Completed]()
        {
            Started->Store(true);
            const double Deadline = FPlatformTime::Seconds() + 10.0;
            while (!Release->Load() && FPlatformTime::Seconds() < Deadline)
            {
                FPlatformProcess::Sleep(0.001f);
            }
            if (!Release->Load()) { TimedOut->Store(true); }
            Completed->Store(true);
        }));
        const double StartDeadline = FPlatformTime::Seconds() + 5.0;
        while (!Started->Load() && FPlatformTime::Seconds() < StartDeadline)
        {
            FPlatformProcess::Sleep(0.001f);
        }
        TestTrue(TEXT("Tracked worker started before reload"), Started->Load());
        TFuture<void> Coordinator = Async(EAsyncExecution::Thread, [Draining, Release, ReloadReturned, ObservedWait, TimedOut]()
        {
            const double Deadline = FPlatformTime::Seconds() + 5.0;
            while (!Draining->Load() && FPlatformTime::Seconds() < Deadline)
            {
                FPlatformProcess::Sleep(0.001f);
            }
            if (Draining->Load())
            {
                FPlatformProcess::Sleep(0.05f);
                ObservedWait->Store(!ReloadReturned->Load());
            }
            else { TimedOut->Store(true); }
            Release->Store(true);
        });
        const uint64 BeforeGraph = Toolkit->GetPreviewRevisionForTests();
        UPackage* ReloadedGraph = ReloadPackage(GraphPackage, LOAD_None);
        ReloadReturned->Store(true);
        Coordinator.Wait();
        TestNotNull(TEXT("Unreal reloaded graph package"), ReloadedGraph);
        TestTrue(TEXT("Reload waited for the held worker before returning"), ObservedWait->Load());
        TestTrue(TEXT("Held worker completed before reload returned"), Completed->Load());
        TestFalse(TEXT("Worker and reload synchronization did not time out"), TimedOut->Load());
        TestTrue(TEXT("Graph reload invalidates stale results"), Toolkit->GetPreviewRevisionForTests() > BeforeGraph);
        TestTrue(TEXT("Graph replacement intentionally closes editor"), Toolkit->IsStoppedForTests());
        TestFalse(TEXT("Pending compile cannot publish after reload"), Toolkit->HasCompileWorkForTests());
        TestTrue(TEXT("Preview worker observed cancellation"), PreviewBarrier->SawCancellation.Load());
        TestFalse(TEXT("Preview worker did not time out"), PreviewBarrier->TimedOut.Load());
        TestTrue(TEXT("Settings worker observed cancellation"), InitBarrier->SawCancellation.Load());
        TestFalse(TEXT("Settings worker did not time out"), InitBarrier->TimedOut.Load());
        if (Actor)
        {
            TestFalse(TEXT("Reload cancels settings initialization"), Actor->bInitializing);
            TestFalse(TEXT("Cancelled settings cannot publish a world state"), Actor->HasWorldStateForTests());
            TestTrue(TEXT("Settings cancellation reported"), Actor->LastInitializationError.Contains(TEXT("cancelled")));
            Actor->Shutdown();
        }
        if (World)
        {
            World->DestroyWorld(false);
            if (bWorldRegistered)
            {
                GEngine->DestroyWorldContext(World);
                World->RemoveFromRoot();
            }
        }
        Toolkit->Stop();
        if (ReloadedGraph)
        {
            UObject* NewAsset = FindObject<UObject>(ReloadedGraph, *FPackageName::GetLongPackageAssetName(GraphName));
            if (TestNotNull(TEXT("Reloaded graph asset exists"), NewAsset))
            {
                UAssetEditorSubsystem* Subsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
                TestNotNull(TEXT("Unreal reopened an editor for the replacement graph"), Subsystem->FindEditorForAsset(NewAsset, false));
                Subsystem->CloseAllEditorsForAsset(NewAsset);
            }
            ResetLoaders(ReloadedGraph);
        }
        if (ReloadedSetup)
        {
            ResetLoaders(ReloadedSetup);
        }
    }
    if (IFileManager::Get().FileExists(*GraphFile))
    {
        TestTrue(TEXT("Remove unique graph fixture"), IFileManager::Get().Delete(*GraphFile));
    }
    if (IFileManager::Get().FileExists(*SetupFile))
    {
        TestTrue(TEXT("Remove unique setup fixture"), IFileManager::Get().Delete(*SetupFile));
    }
    return true;
}
namespace
{
class FVoxelLandPreviewTimingCommand final : public IAutomationLatentCommand
{
public:
    FVoxelLandPreviewTimingCommand(FAutomationTestBase* InTest, TSharedRef<FVoxelLandGraphToolkit> InToolkit)
        : Test(InTest), Toolkit(MoveTemp(InToolkit)), Deadline(FPlatformTime::Seconds() + 60.0)
    {
    }
    virtual bool Update() override
    {
        if (FPlatformTime::Seconds() > Deadline)
        {
            Test->AddError(FString::Printf(TEXT("Preview timing timed out at phase %d: %s"), Phase, *Toolkit->GetStatusForTests()));
            Toolkit->Stop();
            return true;
        }
        if (Phase == 1 || Phase == 3)
        {
            if (!Toolkit->HasPreviewFenceForTests())
            {
                return false;
            }
            if (Phase == 1)
            {
                Toolkit->SetPreviewSeedForTests(2901);
                Test->TestFalse(TEXT("Superseding edit discards an outstanding timing fence"), Toolkit->HasPreviewFenceForTests());
                Test->TestFalse(TEXT("Superseded result is not reported ready"), Toolkit->GetStatusForTests().StartsWith(TEXT("Ready: render commands complete:")));
                Phase = 2;
                return false;
            }
            Toolkit->Stop();
            Test->TestFalse(TEXT("Stopping discards an outstanding timing fence"), Toolkit->HasPreviewFenceForTests());
            Test->TestFalse(TEXT("Stopped preview cannot be framed"), Toolkit->FramePreviewForTests());
            Test->TestFalse(TEXT("Stopped preview has no terrain bounds"), Toolkit->GetPreviewBoundsForTests().IsValid != 0);
            return true;
        }
        if (!Toolkit->GetStatusForTests().StartsWith(TEXT("Ready: render commands complete:")))
        {
            return false;
        }
        if (Phase == 0)
        {
            const FBox Published = Toolkit->GetPreviewBoundsForTests();
            const FBox Components = Toolkit->GetComponentBoundsForTests();
            Test->TestTrue(TEXT("Published terrain has valid world bounds"), Published.IsValid && Components.IsValid);
            Test->TestTrue(TEXT("Frame bounds match all published components"), Published.Min.Equals(Components.Min) && Published.Max.Equals(Components.Max));
            Test->TestTrue(TEXT("Published terrain can be framed"), Toolkit->FramePreviewForTests());
            Toolkit->SetPreviewSeedForTests(2900);
            const FBox StaleBounds = Toolkit->GetPreviewBoundsForTests();
            Test->TestTrue(TEXT("Stale displayed terrain retains framing bounds"), StaleBounds.IsValid && StaleBounds.Min.Equals(Published.Min) && StaleBounds.Max.Equals(Published.Max));
            Test->TestTrue(TEXT("Stale displayed terrain remains frameable"), Toolkit->FramePreviewForTests());
            Phase = 1;
            return false;
        }
        if (Sample > 0)
        {
            Test->AddInfo(FString::Printf(TEXT("Preview timing sample %d: %s"), Sample, *Toolkit->GetStatusForTests()));
        }
        if (Sample == 20)
        {
            Toolkit->SetPreviewSeedForTests(4000);
            Phase = 3;
            return false;
        }
        ++Sample;
        Toolkit->SetPreviewSeedForTests(3000 + Sample);
        return false;
    }
private:
    FAutomationTestBase* Test;
    TSharedRef<FVoxelLandGraphToolkit> Toolkit;
    double Deadline;
    int32 Sample = 0;
    int32 Phase = 0;
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandPreviewTimingTest, "VoxelLand.M3.Editor.PreviewTiming",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVoxelLandPreviewTimingTest::RunTest(const FString& Parameters)
{
    if (!FApp::CanEverRender())
    {
        AddWarning(TEXT("Preview timing requires a native render path and was not exercised with NullRHI"));
        return true;
    }
    UVoxelLandTerrainGraph* Source = LoadObject<UVoxelLandTerrainGraph>(nullptr, TEXT("/VoxelLand/Examples/DA_M3_Hills.DA_M3_Hills"));
    if (!Source)
    {
        AddError(TEXT("Example graph unavailable for preview timing"));
        return false;
    }
    UVoxelLandTerrainGraph* Graph = DuplicateObject<UVoxelLandTerrainGraph>(Source, GetTransientPackage());
    TSharedRef<FVoxelLandGraphToolkit> Toolkit = MakeShared<FVoxelLandGraphToolkit>();
    Toolkit->InitializeForTests(Graph);
    Toolkit->InitializePreviewForTests();
    TestFalse(TEXT("Empty preview cannot be framed"), Toolkit->FramePreviewForTests());
    Toolkit->RequestCompileForTests();
    ADD_LATENT_AUTOMATION_COMMAND(FVoxelLandPreviewTimingCommand(this, Toolkit));
    return true;
}
namespace
{
class FVoxelLandAssetCleanupCheck final : public IAutomationLatentCommand
{
public:
    FVoxelLandAssetCleanupCheck(FAutomationTestBase* InTest, TArray<FString> InPackages)
        : Test(InTest), Packages(MoveTemp(InPackages)), CheckAfter(FPlatformTime::Seconds() + 1.0) {}
    virtual bool Update() override
    {
        if (FPlatformTime::Seconds() < CheckAfter)
        {
            return false;
        }
        IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
        for (const FString& PackageName : Packages)
        {
            const FString ObjectPath = PackageName + TEXT(".") + FPackageName::GetLongPackageAssetName(PackageName);
            Test->TestNull(TEXT("Fixture package remains unloaded after watcher processing"), FindPackage(nullptr, *PackageName));
            Test->TestFalse(TEXT("Fixture asset remains unregistered after watcher processing"),
                Registry.GetAssetByObjectPath(FSoftObjectPath(ObjectPath)).IsValid());
        }
        return true;
    }
private:
    FAutomationTestBase* Test;
    TArray<FString> Packages;
    double CheckAfter;
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandAssetWorkflowTest, "VoxelLand.M3.Editor.AssetWorkflow",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVoxelLandAssetWorkflowTest::RunTest(const FString& Parameters)
{
    const FString Unique = FGuid::NewGuid().ToString(EGuidFormats::Digits);
    const TArray<FString> Sources = {
        TEXT("/VoxelLand/Examples/DA_M3_Hills.DA_M3_Hills"),
        TEXT("/VoxelLand/Examples/DA_M3_WorldSettings.DA_M3_WorldSettings"),
        TEXT("/VoxelLand/Examples/DA_M3_MaterialSetup.DA_M3_MaterialSetup") };
    TArray<FString> Files;
    TArray<FString> PackageNames;
    for (int32 Index = 0; Index < Sources.Num(); ++Index)
    {
        for (const TCHAR* Suffix : {TEXT("Original"), TEXT("Duplicate"), TEXT("Renamed"), TEXT("Factory")})
        {
            const FString Name = FString::Printf(TEXT("/Game/__VoxelLandAsset_%s_%d_%s"), *Unique, Index, Suffix);
            const FString File = FPackageName::LongPackageNameToFilename(Name, FPackageName::GetAssetPackageExtension());
            if (IFileManager::Get().FileExists(*File) || FindPackage(nullptr, *Name))
            {
                AddError(TEXT("Unique asset fixture already exists"));
                return false;
            }
            Files.Add(File);
            PackageNames.Add(Name);
        }
    }
    IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
    for (int32 Index = 0; Index < Sources.Num(); ++Index)
    {
        UObject* Source = LoadObject<UObject>(nullptr, *Sources[Index]);
        if (!TestNotNull(TEXT("Example source asset"), Source)) { continue; }
        const FString Base = FString::Printf(TEXT("__VoxelLandAsset_%s_%d_"), *Unique, Index);
        const FString OriginalName = Base + TEXT("Original");
        UFactory* Factory = nullptr;
        if (Index == 0) { Factory = NewObject<UVoxelLandTerrainGraphFactory>(); }
        else if (Index == 1) { Factory = NewObject<UVoxelLandWorldSettingsFactory>(); }
        else { Factory = NewObject<UVoxelLandMaterialSetupFactory>(); }
        UObject* Created = AssetTools.CreateAsset(Base + TEXT("Factory"), TEXT("/Game"), Source->GetClass(), Factory);
        TestTrue(TEXT("AssetTools factory creates requested transactional asset"),
            Created && Created->GetClass() == Source->GetClass() && Created->HasAnyFlags(RF_Transactional));
        const FString DuplicateName = Base + TEXT("Duplicate");
        const FString RenamedName = Base + TEXT("Renamed");
        UPackage* OriginalPackage = CreatePackage(*(TEXT("/Game/") + OriginalName));
        UObject* Original = DuplicateObject<UObject>(Source, OriginalPackage, *OriginalName);
        Original->SetFlags(RF_Public | RF_Standalone | RF_Transactional);
        FAssetRegistryModule::AssetCreated(Original);
        FSavePackageArgs Args; Args.TopLevelFlags = RF_Public | RF_Standalone;
        if (!TestTrue(TEXT("New asset saves"), UPackage::SavePackage(OriginalPackage, Original, *Files[Index * 4], Args))) { continue; }
        TestFalse(TEXT("Saved package is clean"), OriginalPackage->IsDirty());
        if (UVoxelLandTerrainGraph* Graph = Cast<UVoxelLandTerrainGraph>(Original))
        {
            if (TestTrue(TEXT("Graph has a node for layout edit"), Graph->EditorGraph && !Graph->EditorGraph->Nodes.IsEmpty()))
            {
                UEdGraphNode* Node = Graph->EditorGraph->Nodes[0];
                Node->SetFlags(RF_Transactional);
                Node->Modify();
                Node->NodePosX += 32;
                Graph->EditorGraph->NotifyGraphChanged();
                TestTrue(TEXT("Layout edit dirties saved graph package"), OriginalPackage->IsDirty());
                TestTrue(TEXT("Edited graph saves"), UPackage::SavePackage(OriginalPackage, Original, *Files[Index * 4], Args));
                TestFalse(TEXT("Resaved graph is clean"), OriginalPackage->IsDirty());
            }
        }
        UObject* Duplicate = AssetTools.DuplicateAsset(DuplicateName, TEXT("/Game"), Original);
        if (!TestNotNull(TEXT("AssetTools duplicates asset"), Duplicate)) { continue; }
        TArray<FAssetRenameData> Rename;
        Rename.Emplace(Duplicate, TEXT("/Game"), RenamedName);
        if (!TestTrue(TEXT("AssetTools renames duplicate"), AssetTools.RenameAssets(Rename))) { continue; }
        TestEqual(TEXT("Renamed asset has requested name"), Duplicate->GetName(), RenamedName);
        UPackage* RenamedPackage = Duplicate->GetOutermost();
        TestTrue(TEXT("Renamed asset saves"), UPackage::SavePackage(RenamedPackage, Duplicate, *Files[Index * 4 + 2], Args));
        TArray<FGuid> NodeIds;
        TArray<FGuid> PinIds;
        TArray<FIntPoint> Layout;
        TArray<FString> Links;
        int32 LinkCount = 0;
        if (UVoxelLandTerrainGraph* Graph = Cast<UVoxelLandTerrainGraph>(Duplicate))
        {
            if (TestNotNull(TEXT("Graph has editor representation"), Graph->EditorGraph.Get()))
            {
                for (const UEdGraphNode* Node : Graph->EditorGraph->Nodes)
                {
                    NodeIds.Add(Node->NodeGuid); Layout.Emplace(Node->NodePosX, Node->NodePosY);
                    for (const UEdGraphPin* Pin : Node->Pins)
                    {
                        PinIds.Add(Pin->PinId); LinkCount += Pin->LinkedTo.Num();
                        for (const UEdGraphPin* Linked : Pin->LinkedTo)
                        {
                            Links.Add(Pin->PinId.ToString() + TEXT(":") + Linked->PinId.ToString());
                        }
                    }
                }
            }
        }
        UPackage* Reloaded = ReloadPackage(RenamedPackage, LOAD_None);
        if (TestNotNull(TEXT("Renamed package reopens from disk"), Reloaded))
        {
            UObject* Reopened = FindObject<UObject>(Reloaded, *RenamedName);
            TestTrue(TEXT("Reopened type preserved"), Reopened && Reopened->GetClass() == Source->GetClass());
            if (UVoxelLandTerrainGraph* Graph = Cast<UVoxelLandTerrainGraph>(Reopened))
            {
                TArray<FGuid> ReopenedNodes; TArray<FGuid> ReopenedPins; TArray<FIntPoint> ReopenedLayout;
                TArray<FString> ReopenedLinks; int32 ReopenedLinkCount = 0;
                if (TestNotNull(TEXT("Reopened editor graph"), Graph->EditorGraph.Get()))
                {
                    for (const UEdGraphNode* Node : Graph->EditorGraph->Nodes)
                    {
                        ReopenedNodes.Add(Node->NodeGuid); ReopenedLayout.Emplace(Node->NodePosX, Node->NodePosY);
                        for (const UEdGraphPin* Pin : Node->Pins)
                        {
                            ReopenedPins.Add(Pin->PinId); ReopenedLinkCount += Pin->LinkedTo.Num();
                            for (const UEdGraphPin* Linked : Pin->LinkedTo)
                            {
                                ReopenedLinks.Add(Pin->PinId.ToString() + TEXT(":") + Linked->PinId.ToString());
                            }
                        }
                    }
                    TestTrue(TEXT("Node IDs and layout survive save and reload"), NodeIds == ReopenedNodes && Layout == ReopenedLayout);
                    TestTrue(TEXT("Pin IDs and exact links survive save and reload"), PinIds == ReopenedPins && LinkCount == ReopenedLinkCount && LinkCount > 0 && Links == ReopenedLinks);
                }
            }
            if (const UVoxelLandWorldSettings* Settings = Cast<UVoxelLandWorldSettings>(Reopened))
            {
                const UVoxelLandWorldSettings* Expected = CastChecked<UVoxelLandWorldSettings>(Source);
                TestTrue(TEXT("World settings values and graph reference survive reopen"),
                    Settings->Seed == Expected->Seed && Settings->VoxelSizeCm == Expected->VoxelSizeCm &&
                    Settings->Graph == Expected->Graph && Settings->MaterialSetup == Expected->MaterialSetup);
            }
            if (const UVoxelLandMaterialSetup* Setup = Cast<UVoxelLandMaterialSetup>(Reopened))
            {
                const UVoxelLandMaterialSetup* Expected = CastChecked<UVoxelLandMaterialSetup>(Source);
                TestTrue(TEXT("Material setup version and reference survive reopen"),
                    Setup->FormatVersion == Expected->FormatVersion && Setup->Material == Expected->Material);
            }
            ResetLoaders(Reloaded);
        }
        UPackage* ReloadedOriginal = ReloadPackage(OriginalPackage, LOAD_None);
        if (TestNotNull(TEXT("Original package reopens from disk"), ReloadedOriginal))
        {
            UObject* ReopenedOriginal = FindObject<UObject>(ReloadedOriginal, *OriginalName);
            TestTrue(TEXT("Original reopened type preserved"), ReopenedOriginal && ReopenedOriginal->GetClass() == Source->GetClass());
            ResetLoaders(ReloadedOriginal);
        }
    }
    IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
    Registry.WaitForCompletion();
    for (const FString& PackageName : PackageNames)
    {
        if (UPackage* Package = FindPackage(nullptr, *PackageName))
        {
            ResetLoaders(Package);
        }
    }
    for (const FString& File : Files)
    {
        if (IFileManager::Get().FileExists(*File))
        {
            TestTrue(TEXT("Remove unique asset fixture file"), IFileManager::Get().Delete(*File));
        }
    }
    for (const FString& PackageName : PackageNames)
    {
        UPackage* Package = FindPackage(nullptr, *PackageName);
        if (!Package)
        {
            continue;
        }
        TArray<UObject*> Objects;
        ForEachObjectWithPackage(Package, [&Objects](UObject* Object)
        {
            Objects.Add(Object);
            return true;
        });
        for (UObject* Object : Objects)
        {
            if (Object->IsAsset())
            {
                FAssetRegistryModule::AssetDeleted(Object);
            }
            Object->ClearFlags(RF_Public | RF_Standalone);
            Object->MarkAsGarbage();
        }
        Package->SetDirtyFlag(false);
        Package->ClearFlags(RF_Public | RF_Standalone);
        Package->MarkAsGarbage();
    }
    Registry.ScanModifiedAssetFiles(Files);
    Registry.WaitForCompletion();
    CollectGarbage(RF_NoFlags);
    for (const FString& PackageName : PackageNames)
    {
        TestNull(TEXT("Unique fixture package unloaded"), FindPackage(nullptr, *PackageName));
        const FString ObjectPath = PackageName + TEXT(".") + FPackageName::GetLongPackageAssetName(PackageName);
        TestFalse(TEXT("Unique fixture asset unregistered"),
            Registry.GetAssetByObjectPath(FSoftObjectPath(ObjectPath)).IsValid());
    }
    ADD_LATENT_AUTOMATION_COMMAND(FVoxelLandAssetCleanupCheck(this, PackageNames));
    return true;
}
namespace
{
class FVoxelLandActivePreviewCommand final : public IAutomationLatentCommand
{
public:
    FVoxelLandActivePreviewCommand(FAutomationTestBase* InTest, TSharedRef<FVoxelLandGraphToolkit> InToolkit,
        TSharedRef<FVoxelLandReloadTestBarrier, ESPMode::ThreadSafe> InBarrier)
        : Test(InTest), Toolkit(MoveTemp(InToolkit)), Barrier(MoveTemp(InBarrier)), Deadline(FPlatformTime::Seconds() + 40.0) {}
    virtual bool Update() override
    {
        if (FPlatformTime::Seconds() > Deadline || Barrier->TimedOut.Load())
        {
            Test->AddError(FString::Printf(TEXT("Active preview phase %d timed out: %s"), Phase, *Toolkit->GetStatusForTests()));
            Toolkit->Stop();
            return true;
        }
        if (Phase == 0 && Barrier->Started.Load())
        {
            Test->TestEqual(TEXT("No initial preview has published"), Toolkit->GetPreviewPublishCountForTests(), 0);
            Toolkit->SetPreviewSeedForTests(4321);
            Test->TestTrue(TEXT("Superseded preview is labeled stale"), Toolkit->GetStatusForTests().Contains(TEXT("stale")));
            Phase = 1;
        }
        else if (Phase == 1 && Toolkit->GetStatusForTests().StartsWith(TEXT("Ready: render commands complete:")))
        {
            Test->TestTrue(TEXT("Superseded preview observed cancellation"), Barrier->SawCancellation.Load());
            Test->TestEqual(TEXT("Only the replacement seed published"), Toolkit->GetPreviewPublishCountForTests(), 1);
            Test->TestEqual(TEXT("Published preview uses the newest seed"), Toolkit->GetPublishedSeedForTests(), int64(4321));
            Barrier = MakeShared<FVoxelLandReloadTestBarrier, ESPMode::ThreadSafe>();
            Toolkit->SetPreviewReloadBarrierForTests(Barrier);
            Toolkit->SetPreviewSeedForTests(4322);
            Phase = 2;
        }
        else if (Phase == 2 && Barrier->Started.Load())
        {
            Toolkit->BeginPIEForTests();
            Test->TestTrue(TEXT("PIE transition cancels active preview"), Barrier->SawCancellation.Load());
            Test->TestEqual(TEXT("PIE transition publishes no stale result"), Toolkit->GetPreviewPublishCountForTests(), 1);
            Test->TestTrue(TEXT("PIE marks retained preview stale"), Toolkit->GetStatusForTests().Contains(TEXT("Stale")));
            Toolkit->EndPIEForTests();
            Phase = 3;
        }
        else if (Phase == 3 && Toolkit->GetStatusForTests().StartsWith(TEXT("Ready: render commands complete:")))
        {
            Test->TestEqual(TEXT("Post-PIE preview publishes once"), Toolkit->GetPreviewPublishCountForTests(), 2);
            Test->TestEqual(TEXT("Post-PIE preview uses current seed"), Toolkit->GetPublishedSeedForTests(), int64(4322));
            Barrier = MakeShared<FVoxelLandReloadTestBarrier, ESPMode::ThreadSafe>();
            Toolkit->SetPreviewReloadBarrierForTests(Barrier);
            Toolkit->SetPreviewSeedForTests(4323);
            Phase = 4;
        }
        else if (Phase == 4 && Barrier->Started.Load())
        {
            Toolkit->Stop();
            Test->TestTrue(TEXT("Close cancels active preview"), Barrier->SawCancellation.Load());
            Test->TestEqual(TEXT("Close publishes no stale result"), Toolkit->GetPreviewPublishCountForTests(), 2);
            Test->TestEqual(TEXT("Close retains last published seed"), Toolkit->GetPublishedSeedForTests(), int64(4322));
            CheckAfter = FPlatformTime::Seconds() + 0.5;
            Phase = 5;
        }
        else if (Phase == 5 && FPlatformTime::Seconds() >= CheckAfter)
        {
            Toolkit->PollJobsForTests();
            Test->TestEqual(TEXT("Stopped toolkit cannot publish later"), Toolkit->GetPreviewPublishCountForTests(), 2);
            return true;
        }
        return false;
    }
private:
    FAutomationTestBase* Test;
    TSharedRef<FVoxelLandGraphToolkit> Toolkit;
    TSharedRef<FVoxelLandReloadTestBarrier, ESPMode::ThreadSafe> Barrier;
    double Deadline;
    double CheckAfter = 0.0;
    int32 Phase = 0;
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandActivePreviewTest, "VoxelLand.M3.Editor.ActivePreviewLifecycle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVoxelLandActivePreviewTest::RunTest(const FString& Parameters)
{
    if (!FApp::CanEverRender())
    {
        AddWarning(TEXT("Active preview lifecycle requires a native render path and was not exercised with NullRHI"));
        return true;
    }
    UVoxelLandTerrainGraph* Source = LoadObject<UVoxelLandTerrainGraph>(nullptr, TEXT("/VoxelLand/Examples/DA_M3_Hills.DA_M3_Hills"));
    if (!Source)
    {
        AddError(TEXT("Example graph unavailable for active preview test"));
        return false;
    }
    UVoxelLandTerrainGraph* Graph = DuplicateObject<UVoxelLandTerrainGraph>(Source, GetTransientPackage());
    TSharedRef<FVoxelLandGraphToolkit> Toolkit = MakeShared<FVoxelLandGraphToolkit>();
    Toolkit->InitializeForTests(Graph);
    Toolkit->InitializePreviewForTests();
    TSharedRef<FVoxelLandReloadTestBarrier, ESPMode::ThreadSafe> Barrier = MakeShared<FVoxelLandReloadTestBarrier, ESPMode::ThreadSafe>();
    Toolkit->SetPreviewReloadBarrierForTests(Barrier);
    Toolkit->StartPreviewForTests();
    ADD_LATENT_AUTOMATION_COMMAND(FVoxelLandActivePreviewCommand(this, Toolkit, Barrier));
    return true;
}
namespace
{
class FVoxelLandRealPIECommand final : public IAutomationLatentCommand
{
public:
    FVoxelLandRealPIECommand(FAutomationTestBase* InTest, TSharedRef<FVoxelLandGraphToolkit> InToolkit,
        TSharedRef<FVoxelLandReloadTestBarrier, ESPMode::ThreadSafe> InBarrier)
        : Test(InTest), Toolkit(MoveTemp(InToolkit)), Barrier(MoveTemp(InBarrier)), Deadline(FPlatformTime::Seconds() + 45.0) {}
    virtual bool Update() override
    {
        if (Phase == 4)
        {
            if (!GEditor->IsPlaySessionInProgress() && !GEditor->PlayWorld)
            {
                return true;
            }
            if (FPlatformTime::Seconds() > Deadline)
            {
                Test->AddError(TEXT("PIE session did not finish within teardown deadline"));
                return true;
            }
            return false;
        }
        if (FPlatformTime::Seconds() > Deadline || Barrier->TimedOut.Load())
        {
            Test->AddError(FString::Printf(TEXT("Real PIE lifecycle timed out at phase %d"), Phase));
            if (GEditor->IsPlaySessionRequestQueued()) { GEditor->CancelRequestPlaySession(); }
            if (GEditor->IsPlayingSessionInEditor()) { GEditor->RequestEndPlayMap(); }
            Toolkit->Stop();
            Phase = 4;
            Deadline = FPlatformTime::Seconds() + 10.0;
            return false;
        }
        if (Phase == 0 && Barrier->Started.Load())
        {
            FStartPIECommand(false).Update();
            Phase = 1;
        }
        else if (Phase == 1 && GEditor->IsPlayingSessionInEditor() && GEditor->PlayWorld)
        {
            Test->TestTrue(TEXT("Engine PreBeginPIE reached toolkit"), Toolkit->IsPreviewPIEForTests());
            Test->TestTrue(TEXT("Real PIE start cancels active preview worker"), Barrier->SawCancellation.Load());
            Test->TestEqual(TEXT("PIE start publishes no stale preview"), Toolkit->GetPreviewPublishCountForTests(), 0);
            GEditor->RequestEndPlayMap();
            Phase = 2;
        }
        else if (Phase == 2 && !GEditor->IsPlayingSessionInEditor() && !GEditor->PlayWorld)
        {
            Test->TestFalse(TEXT("Engine EndPIE resumes toolkit"), Toolkit->IsPreviewPIEForTests());
            Phase = 3;
        }
        else if (Phase == 3 && Toolkit->GetStatusForTests().StartsWith(TEXT("Ready: render commands complete:")))
        {
            Test->TestEqual(TEXT("Post-PIE preview publishes once"), Toolkit->GetPreviewPublishCountForTests(), 1);
            Test->TestEqual(TEXT("Post-PIE preview retains current seed"), Toolkit->GetPublishedSeedForTests(), int64(1234));
            Toolkit->Stop();
            return true;
        }
        return false;
    }
private:
    FAutomationTestBase* Test;
    TSharedRef<FVoxelLandGraphToolkit> Toolkit;
    TSharedRef<FVoxelLandReloadTestBarrier, ESPMode::ThreadSafe> Barrier;
    double Deadline;
    int32 Phase = 0;
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandRealPIETest, "VoxelLand.M3.Editor.IsolatedPIELifecycle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVoxelLandRealPIETest::RunTest(const FString& Parameters)
{
    if (!FParse::Param(FCommandLine::Get(), TEXT("VoxelLandIsolatedPIE")) || !FApp::IsUnattended() || !FApp::CanEverRender())
    {
        AddWarning(TEXT("Real PIE lifecycle requires an isolated unattended real-RHI editor launched with -VoxelLandIsolatedPIE"));
        return true;
    }
    if (GEditor->IsPlaySessionInProgress())
    {
        AddError(TEXT("Refusing to replace an existing play session"));
        return false;
    }
    if (!FAutomationEditorCommonUtils::CreateNewMap())
    {
        AddError(TEXT("Could not create isolated in-memory editor map"));
        return false;
    }
    UVoxelLandTerrainGraph* Source = LoadObject<UVoxelLandTerrainGraph>(nullptr, TEXT("/VoxelLand/Examples/DA_M3_Hills.DA_M3_Hills"));
    if (!Source)
    {
        AddError(TEXT("Example graph unavailable for isolated PIE"));
        return false;
    }
    UVoxelLandTerrainGraph* Graph = DuplicateObject<UVoxelLandTerrainGraph>(Source, GetTransientPackage());
    TSharedRef<FVoxelLandGraphToolkit> Toolkit = MakeShared<FVoxelLandGraphToolkit>();
    Toolkit->InitializeForTests(Graph);
    Toolkit->InitializePreviewForTests();
    TSharedRef<FVoxelLandReloadTestBarrier, ESPMode::ThreadSafe> Barrier = MakeShared<FVoxelLandReloadTestBarrier, ESPMode::ThreadSafe>();
    Toolkit->SetPreviewReloadBarrierForTests(Barrier);
    Toolkit->StartPreviewForTests();
    ADD_LATENT_AUTOMATION_COMMAND(FVoxelLandRealPIECommand(this, Toolkit, Barrier));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandInitializationReplacementTest, "VoxelLand.M3.Editor.InitializationReplacement",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVoxelLandInitializationReplacementTest::RunTest(const FString& Parameters)
{
    UVoxelLandWorldSettings* Source = LoadObject<UVoxelLandWorldSettings>(nullptr, TEXT("/VoxelLand/Examples/DA_M3_WorldSettings.DA_M3_WorldSettings"));
    if (!Source || !Source->Graph || !GEngine) { AddError(TEXT("Example settings unavailable")); return false; }
    UVoxelLandWorldSettings* Settings = DuplicateObject<UVoxelLandWorldSettings>(Source, GetTransientPackage());
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    if (!World) { AddError(TEXT("Could not create test world")); return false; }
    World->AddToRoot();
    FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
    Context.SetCurrentWorld(World);
    World->InitializeActorsForPlay(FURL());
    AVoxelLandWorld* Actor = World->SpawnActor<AVoxelLandWorld>();
    if (!Actor)
    {
        World->DestroyWorld(false); GEngine->DestroyWorldContext(World); World->RemoveFromRoot();
        AddError(TEXT("Could not spawn terrain actor"));
        return false;
    }
    Actor->bDataOnly = true; Actor->DispatchBeginPlay();
    if (!Actor->InitializeFromSettings(Settings))
    {
        AddError(FString::Printf(TEXT("Initialization failed to start: %s"), *Actor->LastInitializationError));
        Actor->Shutdown(); World->DestroyWorld(false); GEngine->DestroyWorldContext(World); World->RemoveFromRoot();
        return false;
    }
    TMap<UObject*, UObject*> Replaced;
    Replaced.Add(Settings->Graph.Get(), nullptr);
    Actor->OnInitObjectsReplacedForTests(Replaced);
    TestFalse(TEXT("Replacing graph cancels pending initialization"), Actor->bInitializing);
    TestFalse(TEXT("Replacement cannot publish terrain state"), Actor->HasWorldStateForTests());
    TestTrue(TEXT("Replacement reports cancellation"), Actor->LastInitializationError.Contains(TEXT("cancelled")));
    Actor->Shutdown(); World->DestroyWorld(false); GEngine->DestroyWorldContext(World); World->RemoveFromRoot();
    return true;
}
#endif
