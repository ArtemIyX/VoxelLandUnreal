#if WITH_DEV_AUTOMATION_TESTS
#include "Data/Enums/VoxelLandBuiltinNode.h"
#include "Data/Structs/VoxelLandM4WorldSnapshot.h"
#include "Data/VoxelLandM4HeightStageValidation.h"
#include "Generation/VoxelLandM5Budget.h"
#include "Misc/AutomationTest.h"
#include "World/VoxelLandM5HeightStageValidation.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM5HeightStageValidationTest, "VoxelLand.M5.Identity.HeightStageProjection",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FVoxelLandM5HeightStageValidationTest::RunTest(const FString& Parameters)
{
	FVoxelLandM4WorldSnapshot Snapshot;
	Snapshot.HeightStage.Version = 1;
	Snapshot.HeightStage.OutputNodeId = FGuid(1, 2, 3, 4);
	Snapshot.HeightStage.ContinentalInputId = FGuid(2, 1, 1, 1);
	Snapshot.HeightStage.MountainInputId = FGuid(2, 1, 1, 2);
	Snapshot.HeightStage.PeaksInputId = FGuid(2, 1, 1, 3);
	const FGuid InputIds[] = {Snapshot.HeightStage.ContinentalInputId, Snapshot.HeightStage.MountainInputId,
		Snapshot.HeightStage.PeaksInputId};
	FVoxelLandM4ControlSnapshot* Channels[] = {&Snapshot.Continental, &Snapshot.Mountain, &Snapshot.Peaks};
	FVoxelLandGraphNodeRecord Shared;
	Shared.Id = FGuid(3, 1, 1, 1);
	Shared.OperatorId = VoxelLandBuiltinNodeId(EVoxelLandBuiltinNode::FloatConstant);
	Shared.Configuration = {1, 2, 3};
	Shared.Pins.SetNum(1);
	Shared.Pins[0].Name = TEXT("shared");
	Shared.FunctionPath = FSoftObjectPath(TEXT("/Game/Functions/Example.Example"));
	FVoxelLandGraphFunctionSnapshot Function;
	Function.AssetPath = TEXT("/Game/Functions/Example.Example");
	Function.Nodes.Add(Shared);
	FVoxelLandGraphLinkRecord SharedLink;
	SharedLink.SourceNode = Shared.Id;
	SharedLink.TargetNode = Shared.Id;
	Function.Links.Add(SharedLink);
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Channels); ++Index)
	{
		FVoxelLandGraphNodeRecord Output;
		Output.Id = Snapshot.HeightStage.OutputNodeId;
		Output.OperatorId = VoxelLandBuiltinNodeId(EVoxelLandBuiltinNode::DensityOutput);
		Output.Pins.SetNum(1);
		Output.Pins[0].Id = InputIds[Index];
		Channels[Index]->Source.Nodes = {Shared, Output};
		Channels[Index]->Source.Links.Add(SharedLink);
		Channels[Index]->Source.Functions.Add(Function);
	}
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget;
	if (!TestTrue(TEXT("budget created"), FVoxelLandM5Budget::TryCreate(1024 * 1024, 10000000, Budget)))
	{
		return false;
	}
	const auto NeverCancel = [] {
		return false;
	};
	FString Reason;
	TestTrue(
		TEXT("reference accepts matching projections"), VoxelLandM4HeightStageValidation::Validate(Snapshot, Reason));
	TestEqual(TEXT("metered accepts matching nested data"),
		FVoxelLandM5HeightStageValidation::Validate(Snapshot, Budget.ToSharedRef(), NeverCancel).Code,
		EVoxelLandM5ErrorCode::None);
	Snapshot.Peaks.Source.Functions[0].Nodes[0].Pins[0].Name = TEXT("changed");
	TestFalse(TEXT("reference rejects changed function"), VoxelLandM4HeightStageValidation::Validate(Snapshot, Reason));
	TestEqual(TEXT("metered rejects changed function"),
		FVoxelLandM5HeightStageValidation::Validate(Snapshot, Budget.ToSharedRef(), NeverCancel).Code,
		EVoxelLandM5ErrorCode::InvalidInput);
	Snapshot.Peaks.Source.Functions[0].Nodes[0].Pins[0].Name = TEXT("shared");
	Snapshot.Mountain.Source.Links[0].SourcePin = FGuid(8, 1, 1, 1);
	TestEqual(TEXT("metered rejects changed nonterminal link"),
		FVoxelLandM5HeightStageValidation::Validate(Snapshot, Budget.ToSharedRef(), NeverCancel).Code,
		EVoxelLandM5ErrorCode::InvalidInput);
	Snapshot.Mountain.Source.Links[0] = SharedLink;
	Snapshot.Continental.Source.Parameters.AddDefaulted();
	TestEqual(TEXT("root parameter history fails closed"),
		FVoxelLandM5HeightStageValidation::Validate(Snapshot, Budget.ToSharedRef(), NeverCancel).Code,
		EVoxelLandM5ErrorCode::Unproven);
	Snapshot.Continental.Source.Parameters.Reset();
	Snapshot.Continental.Source.Functions[0].Inputs.AddDefaulted();
	TestEqual(TEXT("function parameter history fails closed"),
		FVoxelLandM5HeightStageValidation::Validate(Snapshot, Budget.ToSharedRef(), NeverCancel).Code,
		EVoxelLandM5ErrorCode::Unproven);
	Snapshot.Continental.Source.Functions[0].Inputs.Reset();
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Empty;
	if (!TestTrue(TEXT("empty budget created"), FVoxelLandM5Budget::TryCreate(0, 0, Empty)))
	{
		return false;
	}
	TestEqual(TEXT("work budget rejected"),
		FVoxelLandM5HeightStageValidation::Validate(Snapshot, Empty.ToSharedRef(), NeverCancel).Code,
		EVoxelLandM5ErrorCode::BudgetExceeded);
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> NoScratch;
	if (!TestTrue(TEXT("scratch-limited budget created"), FVoxelLandM5Budget::TryCreate(0, 10000000, NoScratch)))
	{
		return false;
	}
	TestEqual(TEXT("scratch budget rejected"),
		FVoxelLandM5HeightStageValidation::Validate(Snapshot, NoScratch.ToSharedRef(), NeverCancel).Code,
		EVoxelLandM5ErrorCode::BudgetExceeded);
	int32 Polls = 0;
	const auto CancelDuringInventory = [&Polls] {
		return ++Polls == 5;
	};
	TestEqual(TEXT("inventory cancellation"),
		FVoxelLandM5HeightStageValidation::Validate(Snapshot, Budget.ToSharedRef(), CancelDuringInventory).Code,
		EVoxelLandM5ErrorCode::Cancelled);
	int64 Live = -1;
	int64 Peak = -1;
	int64 Work = -1;
	Budget->GetUsage(Live, Peak, Work);
	TestEqual(TEXT("validation scratch released"), Live, int64(0));
	return true;
}
#endif
