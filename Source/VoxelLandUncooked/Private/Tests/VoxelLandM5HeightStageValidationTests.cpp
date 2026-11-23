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
	FVoxelLandGraphParameter Parameter;
	Parameter.Id = FGuid(4, 1, 1, 1);
	Parameter.Name = TEXT("Amplitude");
	for (FVoxelLandM4ControlSnapshot* Channel : Channels)
	{
		Channel->Source.Parameters.Add(Parameter);
		Channel->Source.Functions[0].Inputs.Add(Parameter);
	}
	Snapshot.Peaks.Source.Parameters[0].Tooltip = FText::FromString(TEXT("different display text"));
	Snapshot.Peaks.Source.Functions[0].Inputs[0].Tooltip = FText::FromString(TEXT("different function text"));
	TestEqual(TEXT("presentation-only tooltip differences do not affect projections"),
		FVoxelLandM5HeightStageValidation::Validate(Snapshot, Budget.ToSharedRef(), NeverCancel).Code,
		EVoxelLandM5ErrorCode::None);
	const auto MutateParameter = [](FVoxelLandGraphParameter& Item, int32 Field) {
		switch (Field)
		{
			case 0:
				Item.Id = FGuid(9, 1, 1, 1);
				break;
			case 1:
				Item.Name = TEXT("amplitude");
				break;
			case 2:
				Item.Type = EVoxelLandGraphValueType::Int32;
				break;
			case 3:
				Item.FloatDefault = 0.5f;
				break;
			case 4:
				Item.IntDefault = 1;
				break;
			case 5:
				Item.BoolDefault = true;
				break;
			case 6:
				Item.bHasRange = true;
				break;
			case 7:
				Item.FloatMin = -1.f;
				break;
			case 8:
				Item.FloatMax = 2.f;
				break;
			case 9:
				Item.IntMin = -1;
				break;
			case 10:
				Item.IntMax = 2;
				break;
		}
	};
	for (int32 Field = 0; Field < 11; ++Field)
	{
		MutateParameter(Snapshot.Peaks.Source.Parameters[0], Field);
		TestEqual(*FString::Printf(TEXT("root semantic parameter %d rejected"), Field),
			FVoxelLandM5HeightStageValidation::Validate(Snapshot, Budget.ToSharedRef(), NeverCancel).Code,
			EVoxelLandM5ErrorCode::InvalidInput);
		Snapshot.Peaks.Source.Parameters[0] = Parameter;
		MutateParameter(Snapshot.Peaks.Source.Functions[0].Inputs[0], Field);
		TestEqual(*FString::Printf(TEXT("function semantic parameter %d rejected"), Field),
			FVoxelLandM5HeightStageValidation::Validate(Snapshot, Budget.ToSharedRef(), NeverCancel).Code,
			EVoxelLandM5ErrorCode::InvalidInput);
		Snapshot.Peaks.Source.Functions[0].Inputs[0] = Parameter;
	}
	const FVoxelLandGraphNodeRecord OriginalNode = Snapshot.Peaks.Source.Nodes[0];
	for (int32 Field = 0; Field < 16; ++Field)
	{
		FVoxelLandGraphNodeRecord& Node = Snapshot.Peaks.Source.Nodes[0];
		switch (Field)
		{
			case 0:
				Node.Id = FGuid(9, 1, 1, 1);
				break;
			case 1:
				Node.OperatorId = FGuid(9, 1, 1, 2);
				break;
			case 2:
				++Node.OperatorVersion;
				break;
			case 3:
				++Node.SeedSalt;
				break;
			case 4:
				Node.Configuration[0]++;
				break;
			case 5:
				Node.FunctionPath = FSoftObjectPath(TEXT("/Game/Functions/Other.Other"));
				break;
			case 6:
				Node.ParameterId = FGuid(9, 1, 1, 3);
				break;
			case 7:
				Node.Pins.AddDefaulted();
				break;
			case 8:
				Node.Pins[0].Id = FGuid(9, 1, 1, 4);
				break;
			case 9:
				Node.Pins[0].InterfaceId = FGuid(9, 1, 1, 5);
				break;
			case 10:
				Node.Pins[0].Name = TEXT("SHARED");
				break;
			case 11:
				Node.Pins[0].Type = EVoxelLandGraphValueType::Int32;
				break;
			case 12:
				Node.Pins[0].bOutput = true;
				break;
			case 13:
				Node.Pins[0].FloatDefault = 1.f;
				break;
			case 14:
				Node.Pins[0].IntDefault = 1;
				break;
			case 15:
				Node.Pins[0].BoolDefault = true;
				break;
		}
		TestEqual(*FString::Printf(TEXT("node or pin semantic field %d rejected"), Field),
			FVoxelLandM5HeightStageValidation::Validate(Snapshot, Budget.ToSharedRef(), NeverCancel).Code,
			EVoxelLandM5ErrorCode::InvalidInput);
		Node = OriginalNode;
	}
	Snapshot.Peaks.Source.Nodes[0].Pins[0].bInvalidDefault = true;
	TestEqual(TEXT("invalid pin flag rejected"),
		FVoxelLandM5HeightStageValidation::Validate(Snapshot, Budget.ToSharedRef(), NeverCancel).Code,
		EVoxelLandM5ErrorCode::InvalidInput);
	Snapshot.Peaks.Source.Nodes[0] = OriginalNode;
	const FVoxelLandGraphFunctionSnapshot OriginalFunction = Snapshot.Peaks.Source.Functions[0];
	for (int32 Field = 0; Field < 5; ++Field)
	{
		FVoxelLandGraphFunctionSnapshot& Item = Snapshot.Peaks.Source.Functions[0];
		switch (Field)
		{
			case 0:
				Item.AssetPath = TEXT("/Game/Functions/Other.Other");
				break;
			case 1:
				++Item.SourceVersion;
				break;
			case 2:
				Item.OutputType = EVoxelLandGraphValueType::Int32;
				break;
			case 3:
				Item.OutputNode = FGuid(9, 1, 1, 1);
				break;
			case 4:
				Item.OutputPin = FGuid(9, 1, 1, 2);
				break;
		}
		TestEqual(*FString::Printf(TEXT("function semantic field %d rejected"), Field),
			FVoxelLandM5HeightStageValidation::Validate(Snapshot, Budget.ToSharedRef(), NeverCancel).Code,
			EVoxelLandM5ErrorCode::InvalidInput);
		Item = OriginalFunction;
	}
	Snapshot.Peaks.Source.Functions[0].Links[0].TargetPin = FGuid(9, 2, 2, 2);
	TestEqual(TEXT("nested link rejected"),
		FVoxelLandM5HeightStageValidation::Validate(Snapshot, Budget.ToSharedRef(), NeverCancel).Code,
		EVoxelLandM5ErrorCode::InvalidInput);
	Snapshot.Peaks.Source.Functions[0] = OriginalFunction;
	FString EmbeddedA(TEXT("prefix"));
	EmbeddedA.AppendChar(TCHAR(0));
	EmbeddedA.AppendChar(TCHAR('x'));
	FString EmbeddedB = EmbeddedA;
	EmbeddedB[EmbeddedB.Len() - 1] = TCHAR('y');
	if (!TestEqual(TEXT("embedded-null fixture retains trailing code unit"), EmbeddedA.Len(), 8))
	{
		return false;
	}
	for (FVoxelLandM4ControlSnapshot* Channel : Channels)
	{
		Channel->Source.Parameters[0].Name = EmbeddedA;
	}
	Snapshot.Peaks.Source.Parameters[0].Name = EmbeddedB;
	TestEqual(TEXT("embedded null in parameter names rejected"),
		FVoxelLandM5HeightStageValidation::Validate(Snapshot, Budget.ToSharedRef(), NeverCancel).Code,
		EVoxelLandM5ErrorCode::InvalidInput);
	for (FVoxelLandM4ControlSnapshot* Channel : Channels)
	{
		Channel->Source.Parameters[0].Name = Parameter.Name;
		Channel->Source.Nodes[0].Pins[0].Name = EmbeddedA;
	}
	Snapshot.Peaks.Source.Nodes[0].Pins[0].Name = EmbeddedB;
	TestEqual(TEXT("embedded null in pin names rejected"),
		FVoxelLandM5HeightStageValidation::Validate(Snapshot, Budget.ToSharedRef(), NeverCancel).Code,
		EVoxelLandM5ErrorCode::InvalidInput);
	for (FVoxelLandM4ControlSnapshot* Channel : Channels)
	{
		Channel->Source.Nodes[0].Pins[0].Name = TEXT("shared");
		Channel->Source.Functions[0].AssetPath = EmbeddedA;
	}
	Snapshot.Peaks.Source.Functions[0].AssetPath = EmbeddedB;
	TestEqual(TEXT("embedded null in function paths rejected"),
		FVoxelLandM5HeightStageValidation::Validate(Snapshot, Budget.ToSharedRef(), NeverCancel).Code,
		EVoxelLandM5ErrorCode::InvalidInput);
	for (FVoxelLandM4ControlSnapshot* Channel : Channels)
	{
		Channel->Source.Functions[0].AssetPath = Function.AssetPath;
	}
	for (int32 Field = 0; Field < 4; ++Field)
	{
		FVoxelLandGraphLinkRecord& Link = Snapshot.Peaks.Source.Links[0];
		switch (Field)
		{
			case 0:
				Link.SourceNode = FGuid(9, 1, 1, 1);
				break;
			case 1:
				Link.SourcePin = FGuid(9, 1, 1, 2);
				break;
			case 2:
				Link.TargetNode = FGuid(9, 1, 1, 3);
				break;
			case 3:
				Link.TargetPin = FGuid(9, 1, 1, 4);
				break;
		}
		TestEqual(*FString::Printf(TEXT("link semantic field %d rejected"), Field),
			FVoxelLandM5HeightStageValidation::Validate(Snapshot, Budget.ToSharedRef(), NeverCancel).Code,
			EVoxelLandM5ErrorCode::InvalidInput);
		Link = SharedLink;
	}
	Snapshot.Peaks.Source.Nodes[1].OperatorId = FGuid(9, 1, 1, 5);
	TestEqual(TEXT("wrong terminal operator rejected"),
		FVoxelLandM5HeightStageValidation::Validate(Snapshot, Budget.ToSharedRef(), NeverCancel).Code,
		EVoxelLandM5ErrorCode::InvalidInput);
	Snapshot.Peaks.Source.Nodes[1].OperatorId = VoxelLandBuiltinNodeId(EVoxelLandBuiltinNode::DensityOutput);
	Snapshot.HeightStage.PeaksInputId = Snapshot.HeightStage.MountainInputId;
	TestEqual(TEXT("duplicate terminal input rejected"),
		FVoxelLandM5HeightStageValidation::Validate(Snapshot, Budget.ToSharedRef(), NeverCancel).Code,
		EVoxelLandM5ErrorCode::InvalidInput);
	Snapshot.HeightStage.PeaksInputId = InputIds[2];
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
