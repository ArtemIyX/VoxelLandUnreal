#if WITH_DEV_AUTOMATION_TESTS

#include "Async/Async.h"
#include "Data/Assets/VoxelLandTerrainGraph.h"
#include "Engine/World.h"
#include "Graph/VoxelLandGraphToolkit.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandEditorModuleQuiesceTest,
	"VoxelLand.M3.Editor.ModuleQuiescePreservesPreviewWorlds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelLandEditorModuleQuiesceTest::RunTest(const FString& Parameters)
{
	UVoxelLandTerrainGraph* FirstAsset = NewObject<UVoxelLandTerrainGraph>();
	UVoxelLandTerrainGraph* SecondAsset = NewObject<UVoxelLandTerrainGraph>();
	TSharedRef<FVoxelLandGraphToolkit> First = MakeShared<FVoxelLandGraphToolkit>();
	TSharedRef<FVoxelLandGraphToolkit> Second = MakeShared<FVoxelLandGraphToolkit>();
	First->InitializeForTests(FirstAsset);
	Second->InitializeForTests(SecondAsset);
	First->InitializePreviewForTests();
	Second->InitializePreviewForTests();
	UWorld* FirstWorld = First->GetPreviewWorldForTests();
	UWorld* SecondWorld = Second->GetPreviewWorldForTests();
	if (!TestNotNull(TEXT("First preview world exists"), FirstWorld)
		|| !TestNotNull(TEXT("Second preview world exists"), SecondWorld))
	{
		First->Stop();
		Second->Stop();
		return false;
	}
	TAtomic<bool> bWorkerFinished(false);
	First->AddWorkerForTests(Async(EAsyncExecution::ThreadPool, [&bWorkerFinished]() { bWorkerFinished.Store(true); }));
	First->QuiesceForModuleShutdown();
	TestTrue(TEXT("Quiesce joins the editor worker"), bWorkerFinished.Load());
	TestTrue(TEXT("First toolkit no longer accepts work"), First->IsStoppedForTests());
	TestFalse(TEXT("Other toolkit remains active"), Second->IsStoppedForTests());
	TestTrue(TEXT("First preview world is retained"), First->GetPreviewWorldForTests() == FirstWorld);
	TestTrue(TEXT("Other preview world is retained"), Second->GetPreviewWorldForTests() == SecondWorld);
	TestFalse(TEXT("First preview world is not tearing down"), FirstWorld->bIsTearingDown);
	TestFalse(TEXT("Other preview world is not tearing down"), SecondWorld->bIsTearingDown);
	First->Stop();
	Second->Stop();
	return true;
}

#endif
