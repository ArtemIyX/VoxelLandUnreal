#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
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
#endif
