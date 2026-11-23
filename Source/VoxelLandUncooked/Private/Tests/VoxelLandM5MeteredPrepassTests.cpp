#if WITH_DEV_AUTOMATION_TESTS
#include "Async/Async.h"
#include "Data/Enums/VoxelLandBuiltinNode.h"
#include "Data/Enums/VoxelLandM4DomainMode.h"
#include "Data/Structs/VoxelLandCanonicalWriter.h"
#include "Data/Structs/VoxelLandM4WorldSnapshot.h"
#include "Generation/VoxelLandGenerator.h"
#include "Generation/VoxelLandM5Budget.h"
#include "Graph/VoxelLandGraphCompiler.h"
#include "Graph/VoxelLandProgramCodec.h"
#include "Hash/xxhash.h"
#include "Misc/AutomationTest.h"
#include "World/VoxelLandM4PrepassIdentity.h"
#include "World/VoxelLandM5MeteredPrepass.h"
#include "World/VoxelLandM5PrepassBytes.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM5MeteredPrepassTest, "VoxelLand.M5.Identity.MeteredPrepass",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FVoxelLandM5MeteredPrepassTest::RunTest(const FString& Parameters)
{
	bool bBytes[2] = {}, bFailureAtomic = false, bCancelled = false, bHeightRejected = false, bReleased = false;
	bool bHeightSupported = false, bHeightTextRejected = false;
	bool bInfiniteRejected = false, bVersionRejected = false;
	FString Error;
	Async(EAsyncExecution::ThreadPool, [&] {
		FVoxelLandM4WorldSnapshot S;
		S.Domain.bGlobalErosion = false;
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
		TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Programs[3];
		for (int32 I = 0; I < 3; ++I)
		{
			auto& C = *Channels[I];
			Output.Pins[0].Id = FGuid(2, 1, 1, 3 + I);
			Link.TargetPin = Output.Pins[0].Id;
			C.Source.Nodes = {Constant, Output};
			C.Source.Links = {Link};
			FVoxelLandProgramDraft Draft;
			if (!FVoxelLandGraphCompiler::Compile(C.Source, Draft, Error)
				|| FVoxelLandProgram::Validate(Draft, Programs[I])
				|| !FVoxelLandProgramCodec::Pack(C.Source, *Programs[I], C.Payload, Error))
			{
				return;
			}
			FVoxelLandM4CurveAuthoringKey First, Last;
			Last.InputQ16 = 65535;
			C.CurveKeys = {First, Last};
			C.CookedCurve.SourceKeys = C.CurveKeys;
			C.CookedCurve.BakeVersion = 1;
			C.CookedCurve.SamplesQ8.Init(0, 4097);
			FCanonicalWriter W;
			for (int32 Value : C.CookedCurve.SamplesQ8)
			{
				W.U32(uint32(Value));
			}
			const uint64 Hash = FXxHash64::HashBuffer(W.Bytes.GetData(), W.Bytes.Num()).Hash;
			FMemory::Memcpy(&C.CookedCurve.SamplesHash, &Hash, sizeof(Hash));
		}
		auto NeverCancel = [] {
			return false;
		};
		TSharedPtr<const FVoxelLandM5PrepassBytes, ESPMode::ThreadSafe> Retained;
		TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> RetainedBudget;
		for (int32 Scenario = 0; Scenario < 2; ++Scenario)
		{
			const uint32 Encoding = Scenario == 0 ? 0u : 1u;
			S.Domain.Mode = EVoxelLandM4DomainMode::Finite;
			TArray<uint8> Expected;
			if (VoxelLandM4PrepassIdentity::BuildCandidate(
					S, *Programs[0], *Programs[1], *Programs[2], Expected, Encoding))
			{
				return;
			}
			TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget;
			if (!FVoxelLandM5Budget::TryCreate(64 * 1024 * 1024, 100000000, Budget))
			{
				return;
			}
			TSharedPtr<const FVoxelLandM5PrepassBytes, ESPMode::ThreadSafe> Current;
			const auto D = FVoxelLandM5MeteredPrepass::Build(S, Encoding, Budget.ToSharedRef(), NeverCancel, Current);
			bBytes[Scenario] = D.Code == EVoxelLandM5ErrorCode::None && Current && Current->GetBytes() == Expected
							&& Current->UsesBudget(Budget.Get());
			if (!bBytes[Scenario])
			{
				Error = D.Message;
				return;
			}
			if (Scenario == 0)
			{
				Retained = Current;
				RetainedBudget = Budget;
			}
			Current.Reset();
			int64 Live = 0, Peak = 0, Work = 0;
			Budget->GetUsage(Live, Peak, Work);
			if (Scenario != 0 && Live != 0)
			{
				return;
			}
		}

		S.Domain.Mode = EVoxelLandM4DomainMode::Finite;
		TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Short;
		if (!FVoxelLandM5Budget::TryCreate(1, 1, Short))
		{
			return;
		}
		const auto* Before = Retained.Get();
		const auto Failed = FVoxelLandM5MeteredPrepass::Build(S, 1, Short.ToSharedRef(), NeverCancel, Retained);
		bFailureAtomic = Failed.Code == EVoxelLandM5ErrorCode::BudgetExceeded && Retained.Get() == Before;
		auto Cancel = [] {
			return true;
		};
		const auto Cancelled = FVoxelLandM5MeteredPrepass::Build(S, 1, Short.ToSharedRef(), Cancel, Retained);
		bCancelled = Cancelled.Code == EVoxelLandM5ErrorCode::Cancelled && Retained.Get() == Before;
		TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> PolicyBudget;
		if (!FVoxelLandM5Budget::TryCreate(64 * 1024 * 1024, 100000000, PolicyBudget))
		{
			return;
		}
		const auto Version = FVoxelLandM5MeteredPrepass::Build(S, 2, PolicyBudget.ToSharedRef(), NeverCancel, Retained);
		bVersionRejected = Version.Code == EVoxelLandM5ErrorCode::UnsupportedVersion && Retained.Get() == Before;
		S.Domain.Mode = EVoxelLandM4DomainMode::Unlimited;
		const auto Infinite =
			FVoxelLandM5MeteredPrepass::Build(S, 1, PolicyBudget.ToSharedRef(), NeverCancel, Retained);
		bInfiniteRejected = Infinite.Code == EVoxelLandM5ErrorCode::Unproven && Retained.Get() == Before;
		S.Domain.Mode = EVoxelLandM4DomainMode::Finite;
		S.HeightStage.Version = 1;
		S.HeightStage.OutputNodeId = Output.Id;
		S.HeightStage.ContinentalInputId = FGuid(2, 1, 1, 3);
		S.HeightStage.MountainInputId = FGuid(2, 1, 1, 4);
		S.HeightStage.PeaksInputId = FGuid(2, 1, 1, 5);
		TArray<uint8> ExpectedHeight;
		if (VoxelLandM4PrepassIdentity::BuildCandidate(S, *Programs[0], *Programs[1], *Programs[2], ExpectedHeight, 1))
		{
			return;
		}
		TSharedPtr<const FVoxelLandM5PrepassBytes, ESPMode::ThreadSafe> HeightBytes;
		const auto Height = FVoxelLandM5MeteredPrepass::Build(S, 1, PolicyBudget.ToSharedRef(), NeverCancel, HeightBytes);
		bHeightSupported = Height.Code == EVoxelLandM5ErrorCode::None && HeightBytes.IsValid()
			&& HeightBytes->GetBytes() == ExpectedHeight;
		S.Continental.Source.Parameters.AddDefaulted();
		const auto TextFailure =
			FVoxelLandM5MeteredPrepass::Build(S, 1, PolicyBudget.ToSharedRef(), NeverCancel, Retained);
		bHeightTextRejected = TextFailure.Code == EVoxelLandM5ErrorCode::Unproven && Retained.Get() == Before;
		S.Continental.Source.Parameters.Reset();
		S.Peaks.Source.Nodes[0].Pins[0].FloatDefault = 0.25f;
		const auto InvalidHeight =
			FVoxelLandM5MeteredPrepass::Build(S, 1, PolicyBudget.ToSharedRef(), NeverCancel, Retained);
		bHeightRejected = InvalidHeight.Code == EVoxelLandM5ErrorCode::InvalidInput && Retained.Get() == Before;
		Retained.Reset();
		int64 Live = 0, Peak = 0, Work = 0;
		RetainedBudget->GetUsage(Live, Peak, Work);
		bReleased = Live == 0;
	}).Wait();
	for (int32 I = 0; I < 2; ++I)
	{
		TestTrue(*FString::Printf(TEXT("current bytes scenario %d: %s"), I, *Error), bBytes[I]);
	}
	TestTrue(TEXT("failure retains prior bytes"), bFailureAtomic);
	TestTrue(TEXT("cancellation retains prior bytes"), bCancelled);
	TestTrue(TEXT("parameter-free height stage produces matching prepass bytes"), bHeightSupported);
	TestTrue(TEXT("unmetered FText is rejected without replacing bytes"), bHeightTextRejected);
	TestTrue(TEXT("invalid height stage fails closed"), bHeightRejected);
	TestTrue(TEXT("infinite mode rejects finite prepass"), bInfiniteRejected);
	TestTrue(TEXT("unknown encoding rejected"), bVersionRejected);
	TestTrue(TEXT("leases released"), bReleased);
	return true;
}
#endif
