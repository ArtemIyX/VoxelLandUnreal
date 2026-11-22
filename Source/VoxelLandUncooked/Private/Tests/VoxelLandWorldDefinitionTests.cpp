#if WITH_DEV_AUTOMATION_TESTS
#include "Async/Async.h"
#include "Data/Assets/VoxelLandFiniteWorld.h"
#include "Data/Assets/VoxelLandGraphFunction.h"
#include "Data/Assets/VoxelLandInfiniteWorld.h"
#include "Data/Assets/VoxelLandTerrainGraph.h"
#include "Data/VoxelLandGraphFunctionId.h"
#include "Data/Enums/VoxelLandM5PlanningMode.h"
#include "Data/Structs/VoxelLandFiniteWorldSnapshot.h"
#include "Data/Structs/VoxelLandInfiniteWorldSnapshot.h"
#include "Misc/AutomationTest.h"
#include "UObject/UnrealType.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
void WorldDefinitionTestControls(UVoxelLandWorldDefinition& World)
{
	FVoxelLandM4ControlConfig* Controls[] = {&World.Common.Continental, &World.Common.Mountain, &World.Common.Peaks};
	for (FVoxelLandM4ControlConfig* Control : Controls)
	{
		Control->Graph = NewObject<UVoxelLandTerrainGraph>(&World);
		FVoxelLandM4CurveAuthoringKey First;
		FVoxelLandM4CurveAuthoringKey Last;
		Last.InputQ16 = 65535;
		Control->CurveKeys = {First, Last};
	}
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandWorldDefinitionCaptureTest, "VoxelLand.WorldDefinition.Capture",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVoxelLandWorldDefinitionCaptureTest::RunTest(const FString& Parameters)
{
	UVoxelLandFiniteWorld* Finite = NewObject<UVoxelLandFiniteWorld>();
	UVoxelLandInfiniteWorld* Infinite = NewObject<UVoxelLandInfiniteWorld>();
	WorldDefinitionTestControls(*Finite);
	WorldDefinitionTestControls(*Infinite);
	Infinite->Common.Continental.Graph = Finite->Common.Continental.Graph;
	TestTrue(TEXT("both world assets can reference the same terrain graph"),
		Infinite->Common.Continental.Graph == Finite->Common.Continental.Graph);
	TestTrue(TEXT("finite asset identifies its kind"), Finite->GetWorldKind() == EVoxelLandWorldKind::Finite);
	TestTrue(TEXT("infinite asset identifies its kind"), Infinite->GetWorldKind() == EVoxelLandWorldKind::Infinite);
	TestNull(TEXT("infinite asset has no finite settings property"),
		FindFProperty<FProperty>(Infinite->GetClass(), TEXT("Finite")));
	TestNull(TEXT("finite asset has no infinite settings property"),
		FindFProperty<FProperty>(Finite->GetClass(), TEXT("Infinite")));

	FString Error;
	FVoxelLandFiniteWorldSnapshot FiniteSnapshot;
	Finite->Common.Seed = -12345;
	Finite->Finite.DropletCount = 77;
	Finite->Common.Caves.PlanningMode = EVoxelLandM5PlanningMode::Regional;
	if (!TestTrue(TEXT("finite authoring captures"), Finite->CaptureSnapshot(FiniteSnapshot, Error)))
	{
		AddError(Error);
		return false;
	}
	TestEqual(TEXT("finite seed is copied"), FiniteSnapshot.Common.Seed, int64(-12345));
	TestEqual(TEXT("finite erosion setting is copied"), FiniteSnapshot.Finite.DropletCount, 77);
	TestTrue(TEXT("finite type selects finite cave planning"),
		FiniteSnapshot.Common.Caves.PlanningMode == EVoxelLandM5PlanningMode::Finite);
	TestEqual(TEXT("finite cave minimum follows control grid"), FiniteSnapshot.Common.Caves.Bounds.MinInclusive.X,
		Finite->Finite.OriginXCm);
	TestEqual(TEXT("finite cave maximum includes final grid sample"), FiniteSnapshot.Common.Caves.Bounds.MaxExclusive.X,
		Finite->Finite.OriginXCm + Finite->Finite.CellsX * Finite->Finite.GridSpacingCm + 1);
	TestEqual(TEXT("control curve keys are captured"), FiniteSnapshot.Common.Continental.CurveKeys.Num(), 2);

	FVoxelLandInfiniteWorldSnapshot InfiniteSnapshot;
	Infinite->Infinite.RegionSizeCm = 409600;
	Infinite->Infinite.RegionOriginXCm = -409600;
	Infinite->Common.Caves.PlanningMode = EVoxelLandM5PlanningMode::Finite;
	if (!TestTrue(TEXT("infinite authoring captures"), Infinite->CaptureSnapshot(InfiniteSnapshot, Error)))
	{
		AddError(Error);
		return false;
	}
	TestTrue(TEXT("infinite type selects regional cave planning"),
		InfiniteSnapshot.Common.Caves.PlanningMode == EVoxelLandM5PlanningMode::Regional);
	TestEqual(TEXT("regional size comes from asset"), InfiniteSnapshot.Common.Caves.RegionSizeCm, int64(409600));
	TestEqual(TEXT("regional origin comes from asset"), InfiniteSnapshot.Common.Caves.RegionOriginXCm, int64(-409600));
	TestEqual(TEXT("infinite world has no global XY cave extent"), InfiniteSnapshot.Common.Caves.Bounds.MaxExclusive.X,
		int64(0));
	TestEqual(TEXT("infinite region inherits its vertical interval"),
		InfiniteSnapshot.Common.Caves.Bounds.MinInclusive.Z, Infinite->Infinite.MinZCm);

	FiniteSnapshot.Common.Seed = 7654;
	Finite->Finite.CellsX = 0;
	TestFalse(TEXT("invalid finite domain rejected"), Finite->CaptureSnapshot(FiniteSnapshot, Error));
	TestEqual(TEXT("finite failure leaves prior snapshot"), FiniteSnapshot.Common.Seed, int64(7654));
	Finite->Finite.CellsX = 256;
	Finite->Common.Continental.CurveKeys[1].InputQ16 = 0;
	TestFalse(TEXT("unordered control curve rejected"), Finite->CaptureSnapshot(FiniteSnapshot, Error));
	TestEqual(TEXT("control failure leaves prior snapshot"), FiniteSnapshot.Common.Seed, int64(7654));
	Finite->Common.Continental.CurveKeys[1].InputQ16 = 65535;

	// Provider records and node configuration are captured as bounded input;
	// the selected world's compiler must still resolve and validate them.
	UVoxelLandTerrainGraph* Graph = Finite->Common.Continental.Graph;
	// Invalid duplicate records also violate the smaller capture budget. The
	// budget error must take precedence over the structural validator's error.
	Graph->Nodes.SetNum(129);
	TestFalse(
		TEXT("oversized graph rejected before structural validation"), Finite->CaptureSnapshot(FiniteSnapshot, Error));
	TestTrue(TEXT("oversized graph reports capture budget"), Error.Contains(TEXT("capture limits")));
	TestEqual(TEXT("oversized graph retains prior snapshot"), FiniteSnapshot.Common.Seed, int64(7654));
	Graph->Nodes.Reset();
	FVoxelLandGraphNodeRecord ProviderNode;
	ProviderNode.Id = FGuid(1, 2, 3, 4);
	ProviderNode.OperatorId = FGuid(5, 6, 7, 8);
	ProviderNode.Configuration = {1, 2, 3};
	Graph->Nodes.Add(ProviderNode);
	FVoxelLandExternalOperator Provider;
	Provider.Id = ProviderNode.OperatorId;
	Provider.Version = 1;
	Provider.Configuration = {4, 5};
	Graph->CompiledProgram.ExternalOperators.Add(Provider);
	if (TestTrue(TEXT("bounded provider data may be captured before capability validation"),
			Finite->CaptureSnapshot(FiniteSnapshot, Error)))
	{
		TestEqual(TEXT("provider configuration copied"),
			FiniteSnapshot.Common.Continental.Payload.ExternalOperators[0].Configuration.Num(), 2);
	}
	else
	{
		AddError(Error);
	}
	Graph->Nodes.Reset();
	Graph->CompiledProgram.ExternalOperators.Reset();

	TStrongObjectPtr<UVoxelLandGraphFunction> Function(NewObject<UVoxelLandGraphFunction>());
	FVoxelLandGraphNodeRecord FunctionNode;
	FunctionNode.Id = FGuid(9, 10, 11, 12);
	FunctionNode.OperatorId = VoxelLandGraphFunctionCallId();
	FunctionNode.FunctionPath = FSoftObjectPath(Function.Get());
	Graph->Nodes.Add(FunctionNode);
	Function->Parameters.AddDefaulted();
	FiniteSnapshot.Common.Seed = 54321;
	TestFalse(TEXT("function inputs fail closed until bounded interface capture exists"),
		Finite->CaptureSnapshot(FiniteSnapshot, Error));
	TestTrue(
		TEXT("unsupported function explains the interface restriction"), Error.Contains(TEXT("bounded interface")));
	TestEqual(TEXT("unsupported function retains previous snapshot"), FiniteSnapshot.Common.Seed, int64(54321));
	Function->Parameters.Reset();
	TStrongObjectPtr<UVoxelLandGraphFunction> Nested(NewObject<UVoxelLandGraphFunction>());
	FVoxelLandGraphNodeRecord NestedNode;
	NestedNode.Id = FGuid(13, 14, 15, 16);
	NestedNode.OperatorId = VoxelLandGraphFunctionCallId();
	NestedNode.FunctionPath = FSoftObjectPath(Nested.Get());
	Function->Nodes.Add(NestedNode);
	int32 LoadCount = 0;
	VoxelLandWorldDefinitionSetBeforeLoadHook([Graph, &Function, &LoadCount]() {
		if (++LoadCount == 2)
		{
			Graph->Nodes.Reset();
			Function->Parameters.AddDefaulted();
		}
	});
	TestFalse(TEXT("reentrant graph mutation rejects capture"), Finite->CaptureSnapshot(FiniteSnapshot, Error));
	TestEqual(TEXT("mutation occurs at second nested load"), LoadCount, 2);
	TestEqual(TEXT("reentrant capture retains previous snapshot"), FiniteSnapshot.Common.Seed, int64(54321));
	TestTrue(TEXT("reentrant mutation reports freshness failure"), Error.Contains(TEXT("changed")));
	VoxelLandWorldDefinitionSetBeforeLoadHook(TFunction<void()>());

	InfiniteSnapshot.Common.Seed = 9876;
	Infinite->Infinite.RegionSizeCm = 101;
	TestFalse(TEXT("region width must be voxel aligned"), Infinite->CaptureSnapshot(InfiniteSnapshot, Error));
	TestEqual(TEXT("infinite failure leaves prior snapshot"), InfiniteSnapshot.Common.Seed, int64(9876));
	Infinite->Infinite.RegionSizeCm = 409600;
	Infinite->Infinite.MaxZCm = Infinite->Infinite.MinZCm;
	TestFalse(TEXT("empty vertical domain rejected"), Infinite->CaptureSnapshot(InfiniteSnapshot, Error));

	// These methods must reject before touching any UObject from a worker thread.
	const bool bWorkerRejected = Async(EAsyncExecution::ThreadPool, [Finite]() {
		FVoxelLandFiniteWorldSnapshot WorkerSnapshot;
		FString WorkerError;
		return !Finite->CaptureSnapshot(WorkerSnapshot, WorkerError);
	}).Get();
	TestTrue(TEXT("worker thread capture rejected"), bWorkerRejected);
	return true;
}
#endif
