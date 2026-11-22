#if WITH_DEV_AUTOMATION_TESTS
#include "Async/Async.h"
#include "Data/Enums/VoxelLandBuiltinNode.h"
#include "Data/Structs/VoxelLandCanonicalWriter.h"
#include "Data/Structs/VoxelLandM4IdentityRecord.h"
#include "Data/Structs/VoxelLandM4WorldSnapshot.h"
#include "Generation/VoxelLandGenerator.h"
#include "Graph/VoxelLandGraphCompiler.h"
#include "Graph/VoxelLandProgramCodec.h"
#include "Hash/xxhash.h"
#include "Misc/AutomationTest.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "World/VoxelLandM4WorldIdentity.h"
#include "UObject/Class.h"

namespace
{
constexpr EAutomationTestFlags M4WorldIdentityTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM4WorldIdentityTest, "VoxelLand.M4.WorldIdentity.Matches", M4WorldIdentityTestFlags)
bool FVoxelLandM4WorldIdentityTest::RunTest(const FString& Parameters)
{
	bool bBuilt = false, bStable = false, bSeed = false, bShape = false, bDetail = false;
	bool bMaterial = false, bUnlimited = false, bVersion = false, bCorrupt = false, bRoundTrip = false;
	bool bInvalidOverride = false;
	FString Error;
	Async(EAsyncExecution::ThreadPool, [&] {
		FVoxelLandM4WorldSnapshot S;
		S.Domain.bGlobalErosion = false;
		S.OverhangNodeId = FGuid(1, 2, 3, 4);
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
		FVoxelLandM4ControlSnapshot* Channels[] = {&S.Continental, &S.Mountain, &S.Peaks};
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
		FVoxelLandM4IdentityRecord Record;
		bBuilt = VoxelLandM4WorldIdentity::Capture(S, Record, Error);
		if (!bBuilt)
		{
			return;
		}
		bStable = VoxelLandM4WorldIdentity::Matches(Record, S, Error);
		FVoxelLandParameterOverride Unknown;
		Unknown.ParameterId = FGuid(9, 9, 9, 9);
		S.Continental.Overrides.Add(Unknown);
		FVoxelLandM4IdentityRecord Invalid;
		bInvalidOverride = !VoxelLandM4WorldIdentity::Capture(S, Invalid, Error)
						&& !VoxelLandM4WorldIdentity::Matches(Record, S, Error);
		S.Continental.Overrides.Reset();
		TArray<uint8> Stored;
		FMemoryWriter Writer(Stored);
		FVoxelLandM4IdentityRecord::StaticStruct()->SerializeItem(Writer, &Record, nullptr);
		FMemoryReader Reader(Stored);
		FVoxelLandM4IdentityRecord Restored;
		FVoxelLandM4IdentityRecord::StaticStruct()->SerializeItem(Reader, &Restored, nullptr);
		bRoundTrip = !Reader.IsError() && VoxelLandM4WorldIdentity::Matches(Restored, S, Error);
		++S.Seed;
		bSeed = !VoxelLandM4WorldIdentity::Matches(Record, S, Error);
		--S.Seed;
		S.bNormalizeSlope = true;
		bShape = !VoxelLandM4WorldIdentity::Matches(Record, S, Error);
		S.bNormalizeSlope = false;
		S.HeightDetail.bEnabled = true;
		S.HeightDetail.AmplitudeQ8 = 256;
		bDetail = !VoxelLandM4WorldIdentity::Matches(Record, S, Error);
		S.HeightDetail.bEnabled = false;
		S.MaterialSetupPath = FSoftObjectPath(TEXT("/Game/Test/Material.Material"));
		bMaterial = VoxelLandM4WorldIdentity::Matches(Record, S, Error);
		S.Domain.Mode = EVoxelLandM4DomainMode::Unlimited;
		FVoxelLandM4IdentityRecord Unlimited;
		bUnlimited = VoxelLandM4WorldIdentity::Capture(S, Unlimited, Error)
				  && VoxelLandM4WorldIdentity::Matches(Unlimited, S, Error)
				  && !VoxelLandM4WorldIdentity::Matches(Record, S, Error);
		++Unlimited.FormatVersion;
		bVersion = !VoxelLandM4WorldIdentity::Matches(Unlimited, S, Error);
		--Unlimited.FormatVersion;
		Unlimited.CanonicalBytes[0] ^= 1;
		bCorrupt = !VoxelLandM4WorldIdentity::Matches(Unlimited, S, Error);
	}).Get();
	TestTrue(FString::Printf(TEXT("identity captures: %s"), *Error), bBuilt);
	TestTrue(TEXT("identical inputs match"), bStable);
	TestTrue(TEXT("capture and matching reject unknown overrides"), bInvalidOverride);
	TestTrue(TEXT("serialized identity matches after round trip"), bRoundTrip);
	TestTrue(TEXT("seed changes reject reuse"), bSeed);
	TestTrue(TEXT("slope changes reject reuse"), bShape);
	TestTrue(TEXT("height detail changes reject reuse"), bDetail);
	TestTrue(TEXT("material does not affect identity"), bMaterial);
	TestTrue(TEXT("unlimited identity is distinct and matches itself"), bUnlimited);
	TestTrue(TEXT("unknown record versions fail"), bVersion);
	TestTrue(TEXT("corrupt bytes fail"), bCorrupt);
	return true;
}
#endif
