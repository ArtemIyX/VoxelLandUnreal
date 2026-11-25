#if WITH_DEV_AUTOMATION_TESTS
#include "../../../VoxelLandEditorBake.h"
#include "Async/Async.h"
#include "Data/Terrain/Controls/VoxelLandControlGraph.h"
#include "Data/Terrain/Worlds/VoxelLandTerrainWorldConfig.h"
#include "Data/Graph/Schema/VoxelLandBuiltinNode.h"
#include "Data/Terrain/Persistence/VoxelLandSavedTerrainWorldRecord.h"
#include "Generation/Field/VoxelLandGenerator.h"
#include "Graph/Serialization/VoxelLandProgramCodec.h"
#include "Misc/AutomationTest.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "UObject/Class.h"
#include "World/Terrain/Persistence/VoxelLandTerrainPrepassIdentity.h"
#include "World/Terrain/Persistence/VoxelLandSavedTerrainWorld.h"
#include "World/Terrain/Persistence/VoxelLandWorldIdentity.h"

namespace
{
constexpr EAutomationTestFlags M4NamedPersistenceFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

void M4PersistenceAppendOutput(UVoxelLandM4ControlGraph& Graph, int32 Index, float Value, FGuid& OutputId)
{
	FVoxelLandGraphNodeRecord Constant;
	Constant.Id = FGuid(101, 0, 0, Index + 1);
	Constant.OperatorId = VoxelLandBuiltinNodeId(EVoxelLandBuiltinNode::FloatConstant);
	Constant.Pins.SetNum(2);
	Constant.Pins[0].Id = FGuid(201, 0, Index + 1, 1);
	Constant.Pins[0].FloatDefault = Value;
	Constant.Pins[1].Id = FGuid(201, 0, Index + 1, 2);
	Constant.Pins[1].bOutput = true;
	FVoxelLandGraphNodeRecord Output;
	Output.Id = FGuid(301, 0, 0, Index + 1);
	OutputId = Output.Id;
	Output.OperatorId = VoxelLandBuiltinNodeId(EVoxelLandBuiltinNode::DensityOutput);
	Output.Pins.SetNum(1);
	Output.Pins[0].Id = FGuid(401, 0, 0, Index + 1);
	FVoxelLandGraphLinkRecord Link;
	Link.SourceNode = Constant.Id;
	Link.SourcePin = Constant.Pins[1].Id;
	Link.TargetNode = Output.Id;
	Link.TargetPin = Output.Pins[0].Id;
	Graph.Nodes.Add(Constant);
	Graph.Nodes.Add(Output);
	Graph.Links.Add(Link);
}

bool M4PersistenceBuildKey(const FVoxelLandM4WorldSnapshot& Snapshot, TArray<uint8>& Out, FString& Error)
{
	const FVoxelLandM4ControlSnapshot* Channels[] = {&Snapshot.Continental, &Snapshot.Mountain, &Snapshot.Peaks};
	TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Programs[3];
	for (int32 Index = 0; Index < 3; ++Index)
	{
		if (!FVoxelLandProgramCodec::Load(Channels[Index]->Source, Channels[Index]->Payload, Programs[Index], Error))
		{
			return false;
		}
	}
	if (const FVoxelLandError Failure =
			VoxelLandM4PrepassIdentity::BuildCandidate(Snapshot, *Programs[0], *Programs[1], *Programs[2], Out))
	{
		Error = Failure.Message;
		return false;
	}
	return true;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM4NamedPersistenceTest, "VoxelLand.M4.Editor.NamedControlPersistence", M4NamedPersistenceFlags)
bool FVoxelLandM4NamedPersistenceTest::RunTest(const FString& Parameters)
{
	UVoxelLandM4ControlGraph* Graph = NewObject<UVoxelLandM4ControlGraph>();
	M4PersistenceAppendOutput(*Graph, 0, -1.f, Graph->ContinentalOutputId);
	M4PersistenceAppendOutput(*Graph, 1, 0.f, Graph->MountainOutputId);
	M4PersistenceAppendOutput(*Graph, 2, 1.f, Graph->PeaksOutputId);
	UVoxelLandM4WorldConfig* Config = NewObject<UVoxelLandM4WorldConfig>();
	Config->Continental.Graph = Config->Mountain.Graph = Config->Peaks.Graph = Graph;
	FVoxelLandM4CurveAuthoringKey First, Last;
	Last.InputQ16 = 65535;
	Config->Continental.CurveKeys = Config->Mountain.CurveKeys = Config->Peaks.CurveKeys = {First, Last};
	Config->bGlobalErosion = false;
	FString Error;
	if (!TestTrue(TEXT("named graph compiles and bakes"), VoxelLandM4Editor::BakeWorldConfig(*Config, Error)))
	{
		AddError(Error);
		return false;
	}
	FVoxelLandM4WorldSnapshot Original;
	if (!TestTrue(TEXT("initial named graph captured"), Config->CaptureSnapshot(Original, Error)))
	{
		AddError(Error);
		return false;
	}
	Swap(Graph->ContinentalOutputId, Graph->MountainOutputId);
	if (!TestTrue(TEXT("reassigned named graph compiles"), VoxelLandM4Editor::BakeWorldConfig(*Config, Error)))
	{
		AddError(Error);
		return false;
	}
	FVoxelLandM4WorldSnapshot Reassigned;
	if (!TestTrue(TEXT("reassigned graph captured"), Config->CaptureSnapshot(Reassigned, Error)))
	{
		AddError(Error);
		return false;
	}
	bool bSaved = false, bRoundTrip = false, bSameKey = false, bChangedIdentity = false, bChangedKey = false;
	Async(EAsyncExecution::ThreadPool, [&] {
		FVoxelLandM4SavedWorld Saved;
		bSaved = VoxelLandM4SavedWorld::Capture(Original, Saved, Error);
		if (!bSaved)
		{
			return;
		}
		TArray<uint8> Bytes;
		FMemoryWriter MemoryWriter(Bytes);
		FObjectAndNameAsStringProxyArchive Writer(MemoryWriter, false);
		FVoxelLandM4SavedWorld::StaticStruct()->SerializeItem(Writer, &Saved, nullptr);
		FMemoryReader MemoryReader(Bytes);
		FObjectAndNameAsStringProxyArchive Reader(MemoryReader, true);
		FVoxelLandM4SavedWorld Loaded;
		FVoxelLandM4SavedWorld::StaticStruct()->SerializeItem(Reader, &Loaded, nullptr);
		FVoxelLandM4WorldSnapshot Restored;
		bRoundTrip = !Writer.IsError() && !Reader.IsError() && VoxelLandM4SavedWorld::Restore(Loaded, Restored, Error)
				  && VoxelLandM4WorldIdentity::Matches(Saved.Identity, Restored, Error);
		if (!bRoundTrip)
		{
			return;
		}
		TArray<uint8> OriginalKey, RestoredKey, ReassignedKey;
		if (!M4PersistenceBuildKey(Original, OriginalKey, Error) || !M4PersistenceBuildKey(Restored, RestoredKey, Error)
			|| !M4PersistenceBuildKey(Reassigned, ReassignedKey, Error))
		{
			return;
		}
		bSameKey = OriginalKey == RestoredKey;
		bChangedIdentity = !VoxelLandM4WorldIdentity::Matches(Saved.Identity, Reassigned, Error);
		bChangedKey = OriginalKey != ReassignedKey;
	}).Get();
	TestTrue(FString::Printf(TEXT("named snapshot saves: %s"), *Error), bSaved);
	TestTrue(TEXT("serialized named snapshot restores and matches"), bRoundTrip);
	TestTrue(TEXT("restored named snapshot preserves prepass dependencies"), bSameKey);
	TestTrue(TEXT("output reassignment changes world identity"), bChangedIdentity);
	TestTrue(TEXT("output reassignment changes prepass key"), bChangedKey);
	return true;
}
#endif
