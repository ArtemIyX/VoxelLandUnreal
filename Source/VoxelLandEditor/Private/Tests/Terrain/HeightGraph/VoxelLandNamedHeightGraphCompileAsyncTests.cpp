#if WITH_DEV_AUTOMATION_TESTS
#include "Data/Terrain/Controls/VoxelLandControlGraph.h"
#include "../../../Graph/Toolkit/VoxelLandGraphToolkit.h"
#include "Misc/AutomationTest.h"

namespace
{
class FM4NamedCompileAsyncCommand final : public IAutomationLatentCommand
{
public:
	FM4NamedCompileAsyncCommand(FAutomationTestBase* InTest, UVoxelLandM4ControlGraph* InGraph,
		TSharedRef<FVoxelLandGraphToolkit> InToolkit, FGuid InOriginal)
		: Test(InTest)
		, Graph(InGraph)
		, Toolkit(InToolkit)
		, Original(InOriginal)
		, Started(FPlatformTime::Seconds())
	{
	}
	virtual bool Update() override
	{
		if (!IsValid(Graph) || FPlatformTime::Seconds() - Started > 30.0)
		{
			Test->AddError(TEXT("Named M4 compilation did not finish"));
			Cleanup();
			return true;
		}
		Toolkit->PollJobsForTests();
		if (Toolkit->HasCompileWorkForTests())
		{
			return false;
		}
		if (Phase == 0)
		{
			Test->TestTrue(TEXT("changed output ID discards result"),
				Graph->ContinentalProgram.Instructions.IsEmpty() && Graph->MountainProgram.Instructions.IsEmpty()
					&& Graph->PeaksProgram.Instructions.IsEmpty());
			Test->TestTrue(TEXT("discard is reported as stale"),
				Toolkit->GetStatusForTests().Contains(TEXT("stale"), ESearchCase::IgnoreCase));
			Graph->HeightOutputId = Original;
			Toolkit->RequestCompileForTests();
			if (!Test->TestTrue(TEXT("current named compile is queued"), Toolkit->HasCompileWorkForTests()))
			{
				Test->AddError(Toolkit->GetStatusForTests());
				Cleanup();
				return true;
			}
			Phase = 1;
			return false;
		}
		Test->TestTrue(TEXT("all three named outputs publish together"),
			!Graph->ContinentalProgram.Instructions.IsEmpty() && !Graph->MountainProgram.Instructions.IsEmpty()
				&& !Graph->PeaksProgram.Instructions.IsEmpty());
		if (Graph->ContinentalProgram.Instructions.IsEmpty())
		{
			Test->AddError(Toolkit->GetStatusForTests());
		}
		Cleanup();
		return true;
	}

private:
	void Cleanup()
	{
		Toolkit->Stop();
		if (IsValid(Graph))
		{
			Graph->RemoveFromRoot();
		}
	}
	FAutomationTestBase* Test;
	UVoxelLandM4ControlGraph* Graph;
	TSharedRef<FVoxelLandGraphToolkit> Toolkit;
	FGuid Original;
	double Started;
	int32 Phase = 0;
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM4NamedCompileAsyncTest, "VoxelLand.M4.Editor.NamedCompileAsync",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVoxelLandM4NamedCompileAsyncTest::RunTest(const FString& Parameters)
{
	UVoxelLandM4ControlGraph* Graph = NewObject<UVoxelLandM4ControlGraph>();
	Graph->AddToRoot();
	TSharedRef<FVoxelLandGraphToolkit> Toolkit = MakeShared<FVoxelLandGraphToolkit>();
	Toolkit->InitializeForTests(Graph);
	const FGuid Original = Graph->HeightOutputId;
	Toolkit->RequestCompileForTests();
	if (!TestTrue(TEXT("named compile returns with queued work"), Toolkit->HasCompileWorkForTests()))
	{
		AddError(Toolkit->GetStatusForTests());
		Toolkit->Stop();
		Graph->RemoveFromRoot();
		return false;
	}
	// Change without pumping completion, even if the worker already completed.
	Graph->HeightOutputId = FGuid(1, 2, 3, 4);
	ADD_LATENT_AUTOMATION_COMMAND(FM4NamedCompileAsyncCommand(this, Graph, Toolkit, Original));
	return true;
}
#endif
