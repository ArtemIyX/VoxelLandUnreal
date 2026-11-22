#if WITH_DEV_AUTOMATION_TESTS
#include "../VoxelLandM4EditorBake.h"
#include "Async/Async.h"
#include "Data/Assets/VoxelLandM4ControlGraph.h"
#include "Data/Assets/VoxelLandM4WorldConfig.h"
#include "Data/Enums/VoxelLandBuiltinNode.h"
#include "Data/Structs/VoxelLandM4SavedWorld.h"
#include "Generation/VoxelLandGenerator.h"
#include "Graph/VoxelLandProgramCodec.h"
#include "Misc/AutomationTest.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "UObject/Class.h"
#include "World/VoxelLandM4PrepassIdentity.h"
#include "World/VoxelLandM4SavedWorld.h"
#include "World/VoxelLandM4WorldIdentity.h"

namespace
{
constexpr EAutomationTestFlags M4HeightSavedFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

bool M4HeightSavedKey(const FVoxelLandM4WorldSnapshot& Snapshot, TArray<uint8>& Out, FString& Error)
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
	FVoxelLandM4HeightSavedTest, "VoxelLand.M4.Editor.HeightOutputSavedWorld", M4HeightSavedFlags)
bool FVoxelLandM4HeightSavedTest::RunTest(const FString& Parameters)
{
	UVoxelLandM4ControlGraph* Graph = NewObject<UVoxelLandM4ControlGraph>();
	FVoxelLandGraphNodeRecord Height;
	Height.Id = FGuid(500, 1, 1, 1);
	Height.OperatorId = VoxelLandBuiltinNodeId(EVoxelLandBuiltinNode::M4HeightOutput);
	Height.Pins.SetNum(3);
	for (int32 Index = 0; Index < 3; ++Index)
	{
		Height.Pins[Index].Id = FGuid(501, 1, 1, Index + 1);
		FVoxelLandGraphNodeRecord Constant;
		Constant.Id = FGuid(510, 1, 1, Index + 1);
		Constant.OperatorId = VoxelLandBuiltinNodeId(EVoxelLandBuiltinNode::FloatConstant);
		Constant.Pins.SetNum(2);
		Constant.Pins[0].Id = FGuid(511, 1, Index + 1, 1);
		Constant.Pins[0].FloatDefault = float(Index - 1);
		Constant.Pins[1].Id = FGuid(511, 1, Index + 1, 2);
		Constant.Pins[1].bOutput = true;
		Graph->Nodes.Add(Constant);
		FVoxelLandGraphLinkRecord Link;
		Link.SourceNode = Constant.Id;
		Link.SourcePin = Constant.Pins[1].Id;
		Link.TargetNode = Height.Id;
		Link.TargetPin = Height.Pins[Index].Id;
		Graph->Links.Add(Link);
	}
	Graph->HeightOutputId = Height.Id;
	Graph->Nodes.Add(Height);
	UVoxelLandM4WorldConfig* Config = NewObject<UVoxelLandM4WorldConfig>();
	Config->Continental.Graph = Config->Mountain.Graph = Config->Peaks.Graph = Graph;
	FVoxelLandM4CurveAuthoringKey First, Last;
	Last.InputQ16 = 65535;
	Last.OutputQ8 = 25600;
	Config->Continental.CurveKeys = Config->Mountain.CurveKeys = Config->Peaks.CurveKeys = {First, Last};
	Config->bGlobalErosion = false;
	FString Error;
	if (!TestTrue(TEXT("graph height config bakes"), VoxelLandM4Editor::BakeWorldConfig(*Config, Error)))
	{
		AddError(Error);
		return false;
	}
	FVoxelLandM4WorldSnapshot Snapshot;
	if (!TestTrue(TEXT("height-stage snapshot captures"), Config->CaptureSnapshot(Snapshot, Error)))
	{
		AddError(Error);
		return false;
	}
	bool bSaved = false, bRestored = false, bKeyEqual = false, bTamper = false, bAtomic = false;
	bool bLegacy = false, bLegacyRejectNewStage = false;
	Async(EAsyncExecution::ThreadPool, [&] {
		FVoxelLandM4SavedWorld Saved;
		bSaved = VoxelLandM4SavedWorld::Capture(Snapshot, Saved, Error) && Saved.Identity.FormatVersion == 4;
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
		bRestored = !Writer.IsError() && !Reader.IsError() && VoxelLandM4SavedWorld::Restore(Loaded, Restored, Error)
				 && Restored.HeightStage.Version == 1
				 && Restored.HeightStage.OutputNodeId == Snapshot.HeightStage.OutputNodeId
				 && Restored.HeightStage.MountainInputId == Snapshot.HeightStage.MountainInputId;
		if (!bRestored)
		{
			return;
		}
		TArray<uint8> OriginalKey, RestoredKey;
		bKeyEqual = M4HeightSavedKey(Snapshot, OriginalKey, Error) && M4HeightSavedKey(Restored, RestoredKey, Error)
				 && OriginalKey == RestoredKey;
		Loaded.Snapshot.HeightStage.MountainInputId = Loaded.Snapshot.HeightStage.ContinentalInputId;
		Restored.Seed = 4321;
		bTamper = !VoxelLandM4SavedWorld::Restore(Loaded, Restored, Error);
		bAtomic = Restored.Seed == 4321;
		FVoxelLandM4WorldSnapshot Legacy = Snapshot;
		Legacy.HeightStage = FVoxelLandM4HeightStageManifest();
		bLegacy = true;
		for (int32 Version = 1; Version <= 3; ++Version)
		{
			FVoxelLandM4IdentityRecord Old;
			bLegacy &= VoxelLandM4WorldIdentity::CaptureVersion(Legacy, Version, Old, Error)
					&& VoxelLandM4WorldIdentity::Matches(Old, Legacy, Error);
		}
		FVoxelLandM4IdentityRecord Old;
		bLegacyRejectNewStage = !VoxelLandM4WorldIdentity::CaptureVersion(Snapshot, 3, Old, Error);
	}).Get();
	TestTrue(FString::Printf(TEXT("graph-height identity saves as v4: %s"), *Error), bSaved);
	TestTrue(TEXT("graph-height manifest survives serialized restore"), bRestored);
	TestTrue(TEXT("restored height graph retains prepass key"), bKeyEqual);
	TestTrue(TEXT("corrupt graph-height manifest rejected"), bTamper);
	TestTrue(TEXT("failed manifest restore leaves output intact"), bAtomic);
	TestTrue(TEXT("stage-zero identities retain v1-v3 matching"), bLegacy);
	TestTrue(TEXT("old identity versions cannot omit a graph-height stage"), bLegacyRejectNewStage);
	return true;
}
#endif
