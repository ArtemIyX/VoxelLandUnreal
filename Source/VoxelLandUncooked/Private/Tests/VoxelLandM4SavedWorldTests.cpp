#if WITH_DEV_AUTOMATION_TESTS
#include "Async/Async.h"
#include "Data/Enums/VoxelLandBuiltinNode.h"
#include "Data/Structs/VoxelLandCanonicalWriter.h"
#include "Data/Structs/VoxelLandM4SavedWorld.h"
#include "Generation/VoxelLandGenerator.h"
#include "Graph/VoxelLandGraphCompiler.h"
#include "Graph/VoxelLandProgramCodec.h"
#include "Hash/xxhash.h"
#include "Misc/AutomationTest.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "UObject/Class.h"
#include "World/VoxelLandM4SavedWorld.h"
#include "World/VoxelLandM4WorldIdentity.h"

namespace
{
constexpr EAutomationTestFlags M4SavedWorldTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM4SavedWorldTest, "VoxelLand.M4.SavedWorld.RoundTrip", M4SavedWorldTestFlags)
bool FVoxelLandM4SavedWorldTest::RunTest(const FString& Parameters)
{
	bool bCaptured = false, bRestored = false, bFields = false, bIdentity = false;
	bool bTampered = false, bUnknown = false, bAtomic = false, bBadProgram = false;
	bool bCaptureRadius = false, bCaptureOversized = false, bCaptureAtomic = false;
	FString Error;
	Async(EAsyncExecution::ThreadPool, [&] {
		FVoxelLandM4WorldSnapshot Original;
		Original.Domain.bGlobalErosion = false;
		Original.Seed = 91234;
		Original.BaseHeightQ8 = 321;
		Original.OverhangNodeId = FGuid(5, 6, 7, 8);
		Original.HeightDetail.bEnabled = true;
		Original.HeightDetail.AmplitudeQ8 = 500;
		Original.HorizontalRadius = 2;
		Original.MaterialSetupPath = FSoftObjectPath(TEXT("/Game/Test/Material.Material"));
		FVoxelLandGraphNodeRecord Constant, Output;
		Constant.Id = FGuid(1, 1, 1, 1);
		Constant.OperatorId = VoxelLandBuiltinNodeId(EVoxelLandBuiltinNode::FloatConstant);
		Constant.Pins.SetNum(2);
		Constant.Pins[0].Id = FGuid(2, 1, 1, 1);
		Constant.Pins[1].Id = FGuid(2, 1, 1, 2);
		Constant.Pins[1].bOutput = true;
		Output.Id = FGuid(1, 1, 1, 2);
		Output.OperatorId = VoxelLandBuiltinNodeId(EVoxelLandBuiltinNode::DensityOutput);
		Output.Pins.SetNum(1);
		Output.Pins[0].Id = FGuid(2, 1, 1, 3);
		FVoxelLandGraphLinkRecord Link;
		Link.SourceNode = Constant.Id;
		Link.SourcePin = Constant.Pins[1].Id;
		Link.TargetNode = Output.Id;
		Link.TargetPin = Output.Pins[0].Id;
		FVoxelLandM4ControlSnapshot* Channels[] = {&Original.Continental, &Original.Mountain, &Original.Peaks};
		for (FVoxelLandM4ControlSnapshot* C : Channels)
		{
			C->Source.Nodes = {Constant, Output};
			C->Source.Links = {Link};
			FVoxelLandProgramDraft Draft;
			TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Program;
			if (!FVoxelLandGraphCompiler::Compile(C->Source, Draft, Error)
				|| FVoxelLandProgram::Validate(Draft, Program)
				|| !FVoxelLandProgramCodec::Pack(C->Source, *Program, C->Payload, Error))
			{
				return;
			}
			FVoxelLandM4CurveAuthoringKey First, Last;
			Last.InputQ16 = 65535;
			C->CurveKeys = {First, Last};
			C->CookedCurve.SourceKeys = C->CurveKeys;
			C->CookedCurve.BakeVersion = 1;
			C->CookedCurve.SamplesQ8.Init(0, 4097);
			FCanonicalWriter W;
			for (int32 Value : C->CookedCurve.SamplesQ8)
			{
				W.U32(uint32(Value));
			}
			const uint64 Hash = FXxHash64::HashBuffer(W.Bytes.GetData(), W.Bytes.Num()).Hash;
			FMemory::Memcpy(&C->CookedCurve.SamplesHash, &Hash, sizeof(Hash));
		}
		FVoxelLandM4SavedWorld Saved;
		bCaptured = VoxelLandM4SavedWorld::Capture(Original, Saved, Error);
		if (!bCaptured)
		{
			return;
		}
		FVoxelLandM4SavedWorld Protected = Saved;
		FVoxelLandM4WorldSnapshot InvalidSnapshot = Original;
		InvalidSnapshot.HorizontalRadius = 0;
		bCaptureRadius =
			!VoxelLandM4SavedWorld::Capture(InvalidSnapshot, Protected, Error) && Error.Contains(TEXT("admission"));
		bCaptureAtomic = Protected.Snapshot.Seed == Saved.Snapshot.Seed
					  && Protected.Identity.CanonicalBytes == Saved.Identity.CanonicalBytes;
		InvalidSnapshot = Original;
		InvalidSnapshot.Continental.Source.Functions.SetNum(17);
		bCaptureOversized = !VoxelLandM4SavedWorld::Capture(InvalidSnapshot, Protected, Error)
						 && Error.Contains(TEXT("admission"))
						 && Protected.Identity.CanonicalBytes == Saved.Identity.CanonicalBytes;
		TArray<uint8> Bytes;
		FMemoryWriter MemoryWriter(Bytes);
		FObjectAndNameAsStringProxyArchive Writer(MemoryWriter, false);
		FVoxelLandM4SavedWorld::StaticStruct()->SerializeItem(Writer, &Saved, nullptr);
		FMemoryReader MemoryReader(Bytes);
		FObjectAndNameAsStringProxyArchive Reader(MemoryReader, true);
		FVoxelLandM4SavedWorld Loaded;
		FVoxelLandM4SavedWorld::StaticStruct()->SerializeItem(Reader, &Loaded, nullptr);
		FVoxelLandM4WorldSnapshot Restored;
		bRestored = !Writer.IsError() && !Reader.IsError() && VoxelLandM4SavedWorld::Restore(Loaded, Restored, Error);
		if (!bRestored)
		{
			return;
		}
		bFields = Restored.Seed == Original.Seed && Restored.BaseHeightQ8 == Original.BaseHeightQ8
			   && Restored.HeightDetail.AmplitudeQ8 == 500 && Restored.HorizontalRadius == 2
			   && Restored.MaterialSetupPath == Original.MaterialSetupPath
			   && Restored.Continental.Source.Nodes.Num() == 2 && Restored.Mountain.Source.Links.Num() == 1
			   && Restored.Peaks.Payload.ProgramFingerprint == Original.Peaks.Payload.ProgramFingerprint
			   && Restored.Peaks.CookedCurve.SamplesQ8.Num() == 4097;
		bIdentity = VoxelLandM4WorldIdentity::Matches(Saved.Identity, Restored, Error);
		Loaded.Snapshot.Seed++;
		Restored.Seed = 777;
		bTampered = !VoxelLandM4SavedWorld::Restore(Loaded, Restored, Error);
		bAtomic = Restored.Seed == 777;
		Loaded.Snapshot.Seed--;
		Loaded.FormatVersion++;
		bUnknown = !VoxelLandM4SavedWorld::Restore(Loaded, Restored, Error) && Restored.Seed == 777;
		Loaded.FormatVersion--;
		Loaded.Snapshot.Continental.Payload.ProgramVersion = -1;
		bBadProgram = !VoxelLandM4SavedWorld::Restore(Loaded, Restored, Error) && Restored.Seed == 777;
	}).Get();
	TestTrue(FString::Printf(TEXT("world captured: %s"), *Error), bCaptured);
	TestTrue(TEXT("world restored after property serialization"), bRestored);
	TestTrue(TEXT("capture rejects invalid startup radius"), bCaptureRadius);
	TestTrue(TEXT("capture rejects excessive nested metadata"), bCaptureOversized);
	TestTrue(TEXT("failed capture leaves its prior output unchanged"), bCaptureAtomic);
	TestTrue(TEXT("settings, source, program and LUT survive"), bFields);
	TestTrue(TEXT("restored density identity matches"), bIdentity);
	TestTrue(TEXT("tampered density setting rejected"), bTampered);
	TestTrue(TEXT("unsupported version rejected"), bUnknown);
	TestTrue(TEXT("failed restore leaves output unchanged"), bAtomic);
	TestTrue(TEXT("invalid compiled program rejected"), bBadProgram);
	return true;
}
#endif
