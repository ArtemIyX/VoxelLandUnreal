#if WITH_DEV_AUTOMATION_TESTS
#include "Generation/Caves/Infrastructure/VoxelLandCaveBudget.h"
#include "Generation/Caves/Geometry/VoxelLandCaveFeetChain.h"
#include "Misc/AutomationTest.h"

namespace
{
constexpr EAutomationTestFlags M5FeetChainFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

FVoxelLandM5Segment M5ChainTestSegment(uint64 Id, int64 StartX, int64 EndX, int64 Z)
{
	FVoxelLandM5Segment Result;
	Result.bHasId = true;
	Result.Id = Id;
	Result.bHasConnectionId = true;
	Result.ConnectionId = 0;
	Result.StartCm.X = StartX;
	Result.StartCm.Z = Z;
	Result.EndCm.X = EndX;
	Result.EndCm.Z = Z;
	Result.RadiusCm = 103;
	return Result;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM5FeetChainTest, "VoxelLand.M5.Routes.FeetChain", M5FeetChainFlags)
bool FVoxelLandM5FeetChainTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget;
	if (!TestTrue(TEXT("budget created"), FVoxelLandM5Budget::TryCreate(65536, 100000, Budget)) || !Budget.IsValid())
	{
		return false;
	}
	const auto NeverCancel = [] {
		return false;
	};
	FVoxelLandM5CapsuleProfile Capsule;
	TArray<FVoxelLandM5Segment> Segments;
	Segments.Add(M5ChainTestSegment(0, -200, 0, 200));
	Segments.Add(M5ChainTestSegment(1, 200, 0, 200)); // reversed relative to traversal
	TSharedPtr<const FVoxelLandM5FeetChain, ESPMode::ThreadSafe> Output;
	const auto Build = [&](TFunctionRef<bool()> Cancel) {
		return FVoxelLandM5FeetChain::Build(Segments, 0, false, Capsule, 2, Budget.ToSharedRef(), Cancel, Output);
	};
	if (!TestTrue(TEXT("two connected segments accepted"), Build(NeverCancel).Code == EVoxelLandM5ErrorCode::None)
		|| !TestTrue(TEXT("result has two legs"), Output.IsValid() && Output->GetLegs().Num() == 2))
	{
		return false;
	}
	const auto& Legs = Output->GetLegs();
	TestEqual(TEXT("first segment ID may be zero"), Legs[0].SegmentId, uint64(0));
	TestEqual(TEXT("odd height produces feet Z"), Legs[0].Feet.StartFeetCm.Z, int64(108));
	TestEqual(TEXT("reversed input joins at zero"), Legs[1].Feet.StartFeetCm.X, int64(0));
	TestEqual(TEXT("reversed input reaches endpoint"), Legs[1].Feet.EndFeetCm.X, int64(200));
	auto Retained = Output;
	// Reverse the first segment as well; the same two-leg traversal is still
	// derived from the explicit initial orientation and shared endpoint. The
	// second leg turns toward positive Y, so the join is not just collinear.
	Swap(Segments[0].StartCm, Segments[0].EndCm);
	Segments[1].StartCm.Y = 100;
	TSharedPtr<const FVoxelLandM5FeetChain, ESPMode::ThreadSafe> ReverseOutput;
	const FVoxelLandM5Diagnostic ReverseError =
		FVoxelLandM5FeetChain::Build(Segments, 0, true, Capsule, 2, Budget.ToSharedRef(), NeverCancel, ReverseOutput);
	if (TestTrue(TEXT("reverse-first chain accepted"), ReverseError.Code == EVoxelLandM5ErrorCode::None)
		&& TestTrue(TEXT("reverse-first output valid"), ReverseOutput.IsValid() && ReverseOutput->GetLegs().Num() == 2))
	{
		TestEqual(
			TEXT("reverse-first begins at minus 200"), ReverseOutput->GetLegs()[0].Feet.StartFeetCm.X, int64(-200));
		TestEqual(TEXT("reverse-first joins zero"), ReverseOutput->GetLegs()[1].Feet.StartFeetCm.X, int64(0));
		TestEqual(TEXT("reverse-first ends at 200"), ReverseOutput->GetLegs()[1].Feet.EndFeetCm.X, int64(200));
		TestEqual(TEXT("reverse-first turns toward Y"), ReverseOutput->GetLegs()[1].Feet.EndFeetCm.Y, int64(100));
	}
	ReverseOutput.Reset();
	Swap(Segments[0].StartCm, Segments[0].EndCm);
	Segments[1].StartCm.Y = 0;
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> NoWorkBudget;
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> NoMemoryBudget;
	if (!TestTrue(TEXT("zero work budget created"), FVoxelLandM5Budget::TryCreate(65536, 0, NoWorkBudget))
		|| !NoWorkBudget.IsValid()
		|| !TestTrue(TEXT("zero memory budget created"), FVoxelLandM5Budget::TryCreate(0, 100000, NoMemoryBudget))
		|| !NoMemoryBudget.IsValid())
	{
		return false;
	}
	TestTrue(TEXT("work exhaustion rejected"),
		FVoxelLandM5FeetChain::Build(Segments, 0, false, Capsule, 2, NoWorkBudget.ToSharedRef(), NeverCancel, Output)
				.Code
			== EVoxelLandM5ErrorCode::BudgetExceeded);
	TestTrue(TEXT("work exhaustion preserves prior result"), Output == Retained);
	TestTrue(TEXT("memory exhaustion rejected"),
		FVoxelLandM5FeetChain::Build(Segments, 0, false, Capsule, 2, NoMemoryBudget.ToSharedRef(), NeverCancel, Output)
				.Code
			== EVoxelLandM5ErrorCode::BudgetExceeded);
	TestTrue(TEXT("memory exhaustion preserves prior result"), Output == Retained);
	Segments[1].StartCm = Segments[1].EndCm;
	TestTrue(TEXT("zero-length segment rejected"), Build(NeverCancel).Code == EVoxelLandM5ErrorCode::InvalidInput);
	TestTrue(TEXT("zero-length failure preserves prior result"), Output == Retained);
	Segments[1].StartCm.X = 200;
	Segments[1].EndCm.X = 1;
	TestTrue(TEXT("gap fails"), Build(NeverCancel).Code == EVoxelLandM5ErrorCode::InconsistentTopology);
	TestTrue(TEXT("failure preserves prior result"), Output == Retained);
	Segments[1].EndCm.X = 0;
	Segments[1].Id = 0;
	TestTrue(TEXT("duplicate ID rejected"), Build(NeverCancel).Code == EVoxelLandM5ErrorCode::IdentityCollision);
	TestTrue(TEXT("collision preserves prior result"), Output == Retained);
	Segments[1].Id = 1;
	Segments[1].ConnectionId = 1;
	TestTrue(TEXT("wrong connection rejected"), Build(NeverCancel).Code == EVoxelLandM5ErrorCode::InvalidInput);
	Segments[1].ConnectionId = 0;
	int32 Polls = 0;
	const auto CancelAfterFirst = [&Polls] {
		return ++Polls >= 5;
	};
	TestTrue(TEXT("mid-chain cancellation"), Build(CancelAfterFirst).Code == EVoxelLandM5ErrorCode::Cancelled);
	TestTrue(TEXT("cancellation preserves prior result"), Output == Retained);
	int64 Live = 0, Peak = 0, Work = 0;
	Budget->GetUsage(Live, Peak, Work);
	TestTrue(TEXT("reader retains admission"), Live > 0);
	Output.Reset();
	Retained.Reset();
	Budget->GetUsage(Live, Peak, Work);
	TestEqual(TEXT("last reader releases admission"), Live, int64(0));
	return true;
}
#endif
