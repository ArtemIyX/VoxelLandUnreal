#if WITH_DEV_AUTOMATION_TESTS
#include "VoxelLandM7CheckpointTestFixture.h"
#include "VoxelLandM7PackedFrameTest.h"

#include "Async/Async.h"
#include "Data/Structs/VoxelLandM7NormalFrameBatch.h"
#include "Generation/VoxelLandM7NormalFrames.h"
#include "Misc/AutomationTest.h"
#include <functional>

class FVoxelLandM7NormalSourceFixture final
{
public:
	static FString Run();

private:
	using FFixture = FVoxelLandM7CheckpointTestFixture;
	using FFrames = FVoxelLandM7NormalFrames;

	static bool CheckWorker(const FVoxelLandGenerator& Generator);
	static bool CheckFootprints(FVoxelLandGeneratorSettings Settings);
	static bool CountPoll(int32& Polls)
	{
		++Polls;
		return false;
	}
	static bool CancelOnZero(int32& Remaining) { return --Remaining == 0; }
	static bool SameFrame(const FVoxelLandM7NormalFrame& A, const FVoxelLandM7NormalFrame& B)
	{
		return A.bFallback == B.bFallback && FVoxelLandM7PackedFrameTest::Matches(A.Normal, A.Tangent, B.Normal, B.Tangent);
	}
};

bool FVoxelLandM7NormalSourceFixture::CheckWorker(const FVoxelLandGenerator& Generator)
{
	TUniquePtr<FVoxelLandWorkerContext> Worker;
	if (Generator.CreateWorker(Worker))
	{
		return false;
	}
	FVoxelLandM7NormalAnchor Tilt, Rebased, Saturated;
	const FVector Point(-12.5, -87.5, 7.5);
	const FVector Rebase(3200, -6400, 1600);
	if (!FFrames::MakeAnchor(Point, 100, Tilt) || !FFrames::MakeAnchor((Point - Rebase) + Rebase, 100, Rebased)
		|| !(Tilt == Rebased) || Tilt.BaseCell != FInt64Vector(-1, -1, 0)
		|| !FFrames::MakeAnchor(FVector(12.5, -87.5, 6400), 100, Saturated))
	{
		return false;
	}

	TArray<FVoxelLandM7NormalAnchor> Anchors;
	Anchors.Init(Tilt, FFrames::MaxStencilCellsPerBatch + 1);
	Anchors.Add(Rebased);
	Anchors.Add(Saturated);
	FVoxelLandM7NormalFrameBatch Batch;
	int32 Polls = 0;
	if (Generator.GenerateM7NormalFrames(Anchors, *Worker, Batch, std::bind_front(&CountPoll, std::ref(Polls)))
		|| Batch.Frames.Num() != Anchors.Num() || Batch.FallbackCount != 1)
	{
		return false;
	}
	const FVector3f Analytic = FVector3f(-0.25f, -0.125f, 1.f).GetSafeNormal();
	for (int32 Index = 0; Index < Batch.Frames.Num() - 1; ++Index)
	{
		const FVoxelLandM7NormalFrame& Frame = Batch.Frames[Index];
		if (Frame.bFallback || FVector3f::DotProduct(Frame.Normal, Analytic) < 0.9999f
			|| !SameFrame(Frame, Batch.Frames[0]))
		{
			return false;
		}
	}
	if (!Batch.Frames.Last().bFallback || Batch.Frames.Last().Normal != FVector3f::UpVector)
	{
		return false;
	}

	const FVoxelLandM7NormalAnchor Reverse[] = {Saturated, Rebased, Tilt};
	FVoxelLandM7NormalFrameBatch Again;
	if (Generator.GenerateM7NormalFrames(Reverse, *Worker, Again, &FFixture::NeverCancel)
		|| Again.FallbackCount != 1 || Again.Frames.Num() != UE_ARRAY_COUNT(Reverse)
		|| !SameFrame(Again.Frames[0], Batch.Frames.Last()) || !SameFrame(Again.Frames[2], Batch.Frames[0]))
	{
		return false;
	}
	int32 Remaining = Polls - 1; // Last anchor evaluation before the publication poll.
	const FVoxelLandError Cancelled =
		Generator.GenerateM7NormalFrames(Anchors, *Worker, Batch, std::bind_front(&CancelOnZero, std::ref(Remaining)));
	if (Cancelled.Code != EVoxelLandErrorCode::Cancelled || !Cancelled.Message.Contains(TEXT("evaluation"))
		|| Batch.Frames.Num() != Anchors.Num() || !SameFrame(Batch.Frames[0], Again.Frames[2]))
	{
		return false;
	}

	TArray<FVoxelLandM7NormalAnchor> TooManyCells;
	for (int32 Index = 0; Index <= FFrames::MaxStencilCellsPerBatch; ++Index)
	{
		FVoxelLandM7NormalAnchor Cell;
		Cell.BaseCell = FInt64Vector(Index % 20, Index / 20, 0);
		TooManyCells.Add(Cell);
	}
	if (Generator.GenerateM7NormalFrames(TooManyCells, *Worker, Batch, &FFixture::NeverCancel).Code
			!= EVoxelLandErrorCode::InvalidParameter || Batch.Frames.Num() != Anchors.Num())
	{
		return false;
	}
	FVoxelLandM7NormalAnchor Invalid = Tilt;
	Invalid.FractionQ16.X = 65536;
	return Generator.GenerateM7NormalFrames(MakeArrayView(&Invalid, 1), *Worker, Batch, &FFixture::NeverCancel).Code
			== EVoxelLandErrorCode::InvalidParameter && Batch.Frames.Num() == Anchors.Num();
}

bool FVoxelLandM7NormalSourceFixture::CheckFootprints(FVoxelLandGeneratorSettings Settings)
{
	FFixture::FGridPtr CompletedOnly, RawOnly, Full;
	if (!FFixture::CreateTiltedGrid(-100, 3, 100, CompletedOnly)
		|| !FFixture::CreateTiltedGrid(-200, 5, 100, RawOnly) || !FFixture::CreateTiltedGrid(-300, 7, 100, Full))
	{
		return false;
	}
	FFixture::FGeneratorPtr Pure, Narrow, Smoothed, NarrowSlope, Shaped;
	if (!FFixture::CreateGenerator(Settings, CompletedOnly, false, false, Pure)
		|| !FFixture::CreateGenerator(Settings, CompletedOnly, true, false, Narrow)
		|| !FFixture::CreateGenerator(Settings, RawOnly, true, false, Smoothed)
		|| !FFixture::CreateGenerator(Settings, RawOnly, true, true, NarrowSlope)
		|| !FFixture::CreateGenerator(Settings, Full, true, true, Shaped))
	{
		return false;
	}
	FVoxelLandM6Stamp Stamp;
	Stamp.Id = FGuid(0x4d37, 2, 0, 1);
	Stamp.RadiusCm = 100;
	Settings.M6Stamps.Stamps = MakeArrayView(&Stamp, 1);
	FFixture::FGeneratorPtr ActiveM6;
	if (!FFixture::CreateGenerator(Settings, Full, false, false, ActiveM6))
	{
		return false;
	}

	const FVoxelLandM7NormalAnchor Anchor;
	const FVoxelLandGenerator* Sources[] = {Pure.Get(), Narrow.Get(), Smoothed.Get(), NarrowSlope.Get(),
		Shaped.Get(), ActiveM6.Get()};
	const EVoxelLandErrorCode Expected[] = {EVoxelLandErrorCode::None, EVoxelLandErrorCode::InvalidParameter,
		EVoxelLandErrorCode::None, EVoxelLandErrorCode::InvalidParameter, EVoxelLandErrorCode::None,
		EVoxelLandErrorCode::UnsupportedLod};
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Sources); ++Index)
	{
		TUniquePtr<FVoxelLandWorkerContext> Worker;
		FVoxelLandM7NormalFrameBatch Batch;
		Batch.FallbackCount = 42;
		if (Sources[Index]->CreateWorker(Worker))
		{
			return false;
		}
		const FVoxelLandError Error =
			Sources[Index]->GenerateM7NormalFrames(MakeArrayView(&Anchor, 1), *Worker, Batch, &FFixture::NeverCancel);
		const bool bSuccess = Expected[Index] == EVoxelLandErrorCode::None;
		if (Error.Code != Expected[Index] || Batch.Frames.Num() != int32(bSuccess)
			|| Batch.FallbackCount != (bSuccess ? 0u : 42u))
		{
			return false;
		}
	}
	return true;
}

FString FVoxelLandM7NormalSourceFixture::Run()
{
	FVoxelLandGeneratorSettings Settings;
	FFixture::FGridPtr Grid;
	FFixture::FGeneratorPtr Generator;
	if (!FFixture::CreateSettings(Settings) || !FFixture::CreateTiltedGrid(-16000, 32, 1000, Grid)
		|| !FFixture::CreateGenerator(Settings, Grid, true, true, Generator))
	{
		return TEXT("Ordinary finite normal-source setup failed");
	}
	if (!CheckWorker(*Generator))
	{
		return TEXT("Shared frame orientation, rebasing, deduplication, fallback count, or cancellation differs");
	}
	if (!CheckFootprints(Settings))
	{
		return TEXT("Shared frame compatibility, slope, or M6 admission differs");
	}
	return {};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM7NormalSourceTest, "VoxelLand.M7.Normals.FiniteSharedSource",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVoxelLandM7NormalSourceTest::RunTest(const FString& Parameters)
{
	const FString Error = Async(EAsyncExecution::ThreadPool, &FVoxelLandM7NormalSourceFixture::Run).Get();
	return TestTrue(Error.IsEmpty() ? TEXT("Canonical shared frames from the ordinary finite field") : Error, Error.IsEmpty());
}
#endif
