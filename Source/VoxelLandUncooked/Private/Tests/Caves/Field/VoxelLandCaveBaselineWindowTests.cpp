#if WITH_DEV_AUTOMATION_TESTS

#include "Async/Async.h"
#include "Data/Terrain/ChunkLocal/VoxelLandChunkLocalTerrainConfig.h"
#include "Data/Terrain/Shaping/VoxelLandTerrainSmoothingSettings.h"
#include "Generation/Field/VoxelLandGenerator.h"
#include "Generation/Terrain/Curves/VoxelLandCurveLut.h"
#include "Generation/Terrain/Heightfield/VoxelLandHeightGrid.h"
#include "Generation/Caves/Field/VoxelLandCaveBaselineWindow.h"
#include "Generation/Caves/Infrastructure/VoxelLandCaveBudget.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM5BaselineWindowTest, "VoxelLand.M5.BaselineWindow.Finite",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FVoxelLandM5BaselineWindowTest::RunTest(const FString& Parameters)
{
	FString Failure;
	bool bAnalytic = false, bExactChunk = false, bAtomic = false, bBudget = false, bCancelled = false;
	bool bLocalMatches = false, bMidCancellation = false, bBoundary = false, bNonuniformSmooth = false;
	bool bFixedLocalMatches = false, bSampleLimits = false, bReusedWorker = false, bForeignWorker = false;
	bool bWrongThread = false, bWorkerCancelled = false, bBoxParity = false, bBoxSmoothParity = false;
	bool bBoxAtomic = false, bBoxBudget = false, bShapedBoxParity = false;
	bool bDetailEffect = false, bOverhangEffect = false;
	const bool bCompleted = Async(EAsyncExecution::ThreadPool, [&]() {
		FVoxelLandProgramDraft Draft;
		Draft.RegisterCount = 1;
		FVoxelLandInstruction Input, Store;
		Input.Op = EVoxelLandOpcode::LoadZ;
		Store.Op = EVoxelLandOpcode::StoreDensity;
		Draft.Instructions = {Input, Store};
		TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Program;
		if (FVoxelLandProgram::Validate(Draft, Program))
		{
			Failure = TEXT("program validation failed");
			return false;
		}
		TArray<int32> Heights;
		Heights.Init(0, 81 * 81);
		TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe> Grid;
		if (!FVoxelLandM4HeightGrid::Create(-4000, -4000, 80, 80, 100, Heights, Grid))
		{
			Failure = TEXT("height grid creation failed");
			return false;
		}
		FVoxelLandGeneratorSettings Settings;
		Settings.Program = Program;
		Settings.Profile.VoxelSizeCm = 100.f;
		TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Generator;
		if (FVoxelLandGenerator::CreateM4HeightField(Settings, Grid, false, Generator) || !Generator)
		{
			Failure = TEXT("generator creation failed");
			return false;
		}
		TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget;
		if (!FVoxelLandM5Budget::TryCreate(16 * 1024 * 1024, 10000000, Budget))
		{
			Failure = TEXT("budget creation failed");
			return false;
		}
		TSharedPtr<const FVoxelLandM5BaselineWindow, ESPMode::ThreadSafe> Window;
		const FIntVector Min(-1, -1, -1), Max(1, 1, 1);
		const FVoxelLandM5Diagnostic Error = FVoxelLandM5BaselineWindow::Build(
			Generator.ToSharedRef(), Min, Max, 8, Budget.ToSharedRef(), [] { return false; }, Window);
		if (Error.Code != EVoxelLandM5ErrorCode::None || !Window || Window->GetSize() != FIntVector(67, 67, 67)
			|| Window->GetDensity().Num() != 67 * 67 * 67)
		{
			Failure = TEXT("finite baseline window failed");
			return false;
		}
		const auto Origin = Window->GetOriginCm();
		bAnalytic = Origin.X == -3300 && Origin.Y == -3300 && Origin.Z == -3300 && Window->GetVoxelSizeCm() == 100;
		for (int32 Z = 0; Z < 67 && bAnalytic; ++Z)
		{
			// Height is exactly zero and no shaping/filter is enabled. The
			// independent reference is z * 1024 with int16 saturation.
			const int32 Expected = FMath::Clamp((Z - 33) * 1024, -32768, 32767);
			for (int32 Y : {0, 32, 33, 34, 66})
			{
				for (int32 X : {0, 32, 33, 34, 66})
				{
					bAnalytic &= Window->GetDensity()[X + 67 * (Y + 67 * Z)] == Expected;
				}
			}
		}
		TUniquePtr<FVoxelLandWorkerContext> Worker;
		TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe> Chunk;
		FVoxelLandChunkKey Key;
		Key.Coordinate = FIntVector::ZeroValue;
		if (Generator->CreateWorker(Worker) || !Worker || Generator->GenerateChunk(Key, *Worker, Chunk) || !Chunk)
		{
			Failure = TEXT("direct M4 chunk generation failed");
			return false;
		}
		bExactChunk = Chunk->Density.Num() == FVoxelLandChunkData::SampleCount;
		for (int32 Z = 0; Z < 35 && bExactChunk; ++Z)
		{
			for (int32 Y = 0; Y < 35 && bExactChunk; ++Y)
			{
				for (int32 X = 0; X < 35; ++X)
				{
					bExactChunk &= Chunk->Density[X + 35 * (Y + 35 * Z)]
								== Window->GetDensity()[(X + 32) + 67 * ((Y + 32) + 67 * (Z + 32))];
				}
			}
		}
		TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> ReuseBudget;
		FVoxelLandM5Budget::TryCreate(16 * 1024 * 1024, 10000000, ReuseBudget);
		TSharedPtr<const FVoxelLandM5BaselineWindow, ESPMode::ThreadSafe> Reused;
		const auto ReuseResult = FVoxelLandM5BaselineWindow::Build(
			Generator.ToSharedRef(), *Worker, Min, Max, 8, ReuseBudget.ToSharedRef(), [] { return false; }, Reused);
		bReusedWorker = ReuseResult.Code == EVoxelLandM5ErrorCode::None && Reused
					 && Reused->GetSize() == Window->GetSize()
					 && Reused->GetDensity().Num() == Window->GetDensity().Num();
		if (bReusedWorker)
		{
			for (int32 I = 0; I < Window->GetDensity().Num(); ++I)
			{
				bReusedWorker &= Reused->GetDensity()[I] == Window->GetDensity()[I];
			}
		}
		TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> ForeignGenerator;
		TUniquePtr<FVoxelLandWorkerContext> ForeignWorker;
		if (FVoxelLandGenerator::CreateM4HeightField(Settings, Grid, false, ForeignGenerator) || !ForeignGenerator
			|| ForeignGenerator->CreateWorker(ForeignWorker) || !ForeignWorker)
		{
			return false;
		}
		int64 WorkerBeforeLive = 0, WorkerBeforePeak = 0, WorkerBeforeWork = 0;
		int64 WorkerAfterLive = 0, WorkerAfterPeak = 0, WorkerAfterWork = 0;
		ReuseBudget->GetUsage(WorkerBeforeLive, WorkerBeforePeak, WorkerBeforeWork);
		const auto PreviousReuse = Reused;
		const auto ForeignResult = FVoxelLandM5BaselineWindow::Build(
			Generator.ToSharedRef(), *ForeignWorker, Min, Max, 8, ReuseBudget.ToSharedRef(), [] { return false; },
			Reused);
		ReuseBudget->GetUsage(WorkerAfterLive, WorkerAfterPeak, WorkerAfterWork);
		bForeignWorker = ForeignResult.Code == EVoxelLandM5ErrorCode::InvalidInput && Reused == PreviousReuse
					  && WorkerBeforeLive == WorkerAfterLive && WorkerBeforePeak == WorkerAfterPeak
					  && WorkerBeforeWork == WorkerAfterWork;
		const auto ThreadResult = Async(EAsyncExecution::Thread, [&] {
			return FVoxelLandM5BaselineWindow::Build(
				Generator.ToSharedRef(), *Worker, Min, Max, 8, ReuseBudget.ToSharedRef(), [] { return false; }, Reused);
		}).Get();
		ReuseBudget->GetUsage(WorkerAfterLive, WorkerAfterPeak, WorkerAfterWork);
		bWrongThread = ThreadResult.Code == EVoxelLandM5ErrorCode::InvalidInput && Reused == PreviousReuse
					&& WorkerBeforeLive == WorkerAfterLive && WorkerBeforePeak == WorkerAfterPeak
					&& WorkerBeforeWork == WorkerAfterWork;
		const auto WorkerCancel = FVoxelLandM5BaselineWindow::Build(
			Generator.ToSharedRef(), *Worker, Min, Max, 8, ReuseBudget.ToSharedRef(), [] { return true; }, Reused);
		bWorkerCancelled = WorkerCancel.Code == EVoxelLandM5ErrorCode::Cancelled && Reused == PreviousReuse;
		TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> BoxBudget;
		FVoxelLandM5Budget::TryCreate(16 * 1024 * 1024, 10000000, BoxBudget);
		FVoxelLandM5PointCm BoxMin;
		BoxMin.X = BoxMin.Y = BoxMin.Z = -33;
		TSharedPtr<const FVoxelLandM5BaselineWindow, ESPMode::ThreadSafe> Box;
		const auto BoxResult = FVoxelLandM5BaselineWindow::BuildBox(
			Generator.ToSharedRef(), *Worker, BoxMin, FIntVector(67, 67, 67), BoxBudget.ToSharedRef(),
			[] { return false; }, Box);
		bBoxParity = BoxResult.Code == EVoxelLandM5ErrorCode::None && Box && Box->GetSize() == Window->GetSize()
				  && Box->GetOriginCm().X == Window->GetOriginCm().X
				  && Box->GetDensity().Num() == Window->GetDensity().Num();
		if (bBoxParity)
		{
			for (int32 I = 0; I < Window->GetDensity().Num(); ++I)
			{
				bBoxParity &= Box->GetDensity()[I] == Window->GetDensity()[I];
			}
		}
		const auto PriorBox = Box;
		const auto BoxCancel = FVoxelLandM5BaselineWindow::BuildBox(
			Generator.ToSharedRef(), *Worker, BoxMin, FIntVector(67, 67, 67), BoxBudget.ToSharedRef(),
			[] { return true; }, Box);
		bBoxAtomic = BoxCancel.Code == EVoxelLandM5ErrorCode::Cancelled && Box == PriorBox;
		TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> BoxTiny;
		FVoxelLandM5Budget::TryCreate(1024, 1, BoxTiny);
		const auto BoxExhausted = FVoxelLandM5BaselineWindow::BuildBox(
			Generator.ToSharedRef(), *Worker, BoxMin, FIntVector(67, 67, 67), BoxTiny.ToSharedRef(),
			[] { return false; }, Box);
		bBoxBudget = BoxExhausted.Code == EVoxelLandM5ErrorCode::BudgetExceeded && Box == PriorBox;
		const auto Previous = Window;
		const FVoxelLandM5Diagnostic Invalid = FVoxelLandM5BaselineWindow::Build(
			Generator.ToSharedRef(), Min, Max, 7, Budget.ToSharedRef(), [] { return false; }, Window);
		bAtomic = Invalid.Code == EVoxelLandM5ErrorCode::OutOfDomain && Window == Previous;
		int64 LimitLiveBefore = 0, LimitPeakBefore = 0, LimitWorkBefore = 0;
		Budget->GetUsage(LimitLiveBefore, LimitPeakBefore, LimitWorkBefore);
		const FVoxelLandM5Diagnostic PositiveLimit = FVoxelLandM5BaselineWindow::Build(
			Generator.ToSharedRef(), FIntVector(327, 0, 0), FIntVector(328, 1, 1), 1, Budget.ToSharedRef(),
			[] { return false; }, Window);
		const FVoxelLandM5Diagnostic NegativeLimit = FVoxelLandM5BaselineWindow::Build(
			Generator.ToSharedRef(), FIntVector(-328, 0, 0), FIntVector(-327, 1, 1), 1, Budget.ToSharedRef(),
			[] { return false; }, Window);
		const FVoxelLandM5Diagnostic InvalidExtreme = FVoxelLandM5BaselineWindow::Build(
			Generator.ToSharedRef(), FIntVector(MIN_int32, 0, 0), FIntVector(MIN_int32, 1, 1), 1, Budget.ToSharedRef(),
			[] { return false; }, Window);
		int64 LimitLiveAfter = 0, LimitPeakAfter = 0, LimitWorkAfter = 0;
		Budget->GetUsage(LimitLiveAfter, LimitPeakAfter, LimitWorkAfter);
		bSampleLimits = PositiveLimit.Code == EVoxelLandM5ErrorCode::OutOfDomain
					 && NegativeLimit.Code == EVoxelLandM5ErrorCode::OutOfDomain
					 && InvalidExtreme.Code == EVoxelLandM5ErrorCode::OutOfDomain && Window == Previous
					 && LimitLiveBefore == LimitLiveAfter && LimitPeakBefore == LimitPeakAfter
					 && LimitWorkBefore == LimitWorkAfter;
		TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Tiny;
		FVoxelLandM5Budget::TryCreate(1, 10000000, Tiny);
		const FVoxelLandM5Diagnostic Limited = FVoxelLandM5BaselineWindow::Build(
			Generator.ToSharedRef(), Min, Max, 8, Tiny.ToSharedRef(), [] { return false; }, Window);
		bBudget = Limited.Code == EVoxelLandM5ErrorCode::BudgetExceeded && Window == Previous;
		const FVoxelLandM5Diagnostic Cancelled = FVoxelLandM5BaselineWindow::Build(
			Generator.ToSharedRef(), Min, Max, 8, Budget.ToSharedRef(), [] { return true; }, Window);
		bCancelled = Cancelled.Code == EVoxelLandM5ErrorCode::Cancelled && Window == Previous;
		int32 Polls = 0;
		const FVoxelLandM5Diagnostic Mid = FVoxelLandM5BaselineWindow::Build(
			Generator.ToSharedRef(), Min, Max, 8, Budget.ToSharedRef(), [&Polls] { return ++Polls >= 20; }, Window);
		bMidCancellation = Mid.Code == EVoxelLandM5ErrorCode::Cancelled && Window == Previous && Polls >= 20;

		// An exact finite boundary succeeds; extending the same rectangle by one
		// chunk must fail before output/scratch allocation and preserve its window.
		TArray<int32> BoundaryHeights;
		BoundaryHeights.Init(0, 35 * 35);
		TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe> BoundaryGrid;
		TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> BoundaryGenerator;
		if (!FVoxelLandM4HeightGrid::Create(-100, -100, 34, 34, 100, BoundaryHeights, BoundaryGrid)
			|| FVoxelLandGenerator::CreateM4HeightField(Settings, BoundaryGrid, false, BoundaryGenerator))
		{
			Failure = TEXT("finite boundary setup failed");
			return false;
		}
		TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> BoundaryBudget;
		FVoxelLandM5Budget::TryCreate(8 * 1024 * 1024, 10000000, BoundaryBudget);
		TSharedPtr<const FVoxelLandM5BaselineWindow, ESPMode::ThreadSafe> BoundaryWindow;
		const auto AtBoundary = FVoxelLandM5BaselineWindow::Build(
			BoundaryGenerator.ToSharedRef(), FIntVector(0, 0, 0), FIntVector(1, 1, 1), 2, BoundaryBudget.ToSharedRef(),
			[] { return false; }, BoundaryWindow);
		if (AtBoundary.Code != EVoxelLandM5ErrorCode::None || !BoundaryWindow)
		{
			Failure = TEXT("finite boundary window failed");
			return false;
		}
		int64 BeforeBytes = 0, BeforePeak = 0, BeforeWork = 0;
		BoundaryBudget->GetUsage(BeforeBytes, BeforePeak, BeforeWork);
		const auto PriorBoundary = BoundaryWindow;
		const auto Outside = FVoxelLandM5BaselineWindow::Build(
			BoundaryGenerator.ToSharedRef(), FIntVector(0, 0, 0), FIntVector(2, 1, 1), 2, BoundaryBudget.ToSharedRef(),
			[] { return false; }, BoundaryWindow);
		int64 AfterBytes = 0, AfterPeak = 0, AfterWork = 0;
		BoundaryBudget->GetUsage(AfterBytes, AfterPeak, AfterWork);
		bBoundary = Outside.Code == EVoxelLandM5ErrorCode::OutOfDomain && BoundaryWindow == PriorBoundary
				 && BeforeBytes == AfterBytes && BeforePeak == AfterPeak
				 && Outside.Message.Contains(TEXT("height grid"));

		TArray<int32> Uneven;
		for (int32 Y = 0; Y < 81; ++Y)
		{
			for (int32 X = 0; X < 81; ++X)
			{
				Uneven.Add(((X * X + 3 * Y) % 71 - 35) * 256);
			}
		}
		TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe> UnevenGrid;
		TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> SmoothedGenerator;
		FVoxelLandM4SmoothSettings Smooth;
		Smooth.bEnabled = true;
		Smooth.Strength = 0.5f;
		FVoxelLandM4OverhangConfig Overhang;
		if (!FVoxelLandM4HeightGrid::Create(-4000, -4000, 80, 80, 100, Uneven, UnevenGrid)
			|| FVoxelLandGenerator::CreateM4HeightField(
				Settings, UnevenGrid, false, Overhang, Smooth, SmoothedGenerator))
		{
			Failure = TEXT("nonuniform smoothed setup failed");
			return false;
		}
		TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> SmoothBudget;
		FVoxelLandM5Budget::TryCreate(16 * 1024 * 1024, 10000000, SmoothBudget);
		TSharedPtr<const FVoxelLandM5BaselineWindow, ESPMode::ThreadSafe> SmoothWindow;
		const auto SmoothError = FVoxelLandM5BaselineWindow::Build(
			SmoothedGenerator.ToSharedRef(), FIntVector(-1, 0, 0), FIntVector(1, 1, 1), 2, SmoothBudget.ToSharedRef(),
			[] { return false; }, SmoothWindow);
		TUniquePtr<FVoxelLandWorkerContext> SmoothWorker;
		TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe> Left, Right;
		FVoxelLandChunkKey LeftKey, RightKey;
		LeftKey.Coordinate.X = -1;
		if (SmoothError.Code != EVoxelLandM5ErrorCode::None || !SmoothWindow
			|| SmoothedGenerator->CreateWorker(SmoothWorker) || !SmoothWorker
			|| SmoothedGenerator->GenerateChunk(LeftKey, *SmoothWorker, Left)
			|| SmoothedGenerator->GenerateChunk(RightKey, *SmoothWorker, Right) || !Left || !Right)
		{
			Failure = TEXT("nonuniform smoothed generation failed");
			return false;
		}
		bNonuniformSmooth = Left->Density.Num() == FVoxelLandChunkData::SampleCount
						 && Right->Density.Num() == FVoxelLandChunkData::SampleCount;
		bool bDifferentColumns = false;
		for (int32 Z = 0; Z < 35 && bNonuniformSmooth; ++Z)
		{
			for (int32 Y = 0; Y < 35 && bNonuniformSmooth; ++Y)
			{
				for (int32 Plane = 0; Plane < 3; ++Plane)
				{
					const int32 A = 32 + Plane + 35 * (Y + 35 * Z);
					const int32 B = Plane + 35 * (Y + 35 * Z);
					bNonuniformSmooth &=
						Left->Density[A] == Right->Density[B]
						&& Left->Density[A] == SmoothWindow->GetDensity()[(32 + Plane) + 67 * (Y + 35 * Z)];
					if (Y > 0 && Left->Density[A] != Left->Density[32 + Plane + 35 * (35 * Z)])
					{
						bDifferentColumns = true;
					}
				}
			}
		}
		bNonuniformSmooth &= bDifferentColumns;
		TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> SmoothBoxBudget;
		FVoxelLandM5Budget::TryCreate(16 * 1024 * 1024, 10000000, SmoothBoxBudget);
		FVoxelLandM5PointCm SmoothBoxMin;
		SmoothBoxMin.X = -33;
		SmoothBoxMin.Y = SmoothBoxMin.Z = -1;
		TSharedPtr<const FVoxelLandM5BaselineWindow, ESPMode::ThreadSafe> SmoothBox;
		const auto SmoothBoxError = FVoxelLandM5BaselineWindow::BuildBox(
			SmoothedGenerator.ToSharedRef(), *SmoothWorker, SmoothBoxMin, FIntVector(67, 35, 35),
			SmoothBoxBudget.ToSharedRef(), [] { return false; }, SmoothBox);
		bBoxSmoothParity = SmoothBoxError.Code == EVoxelLandM5ErrorCode::None && SmoothBox
						&& SmoothBox->GetDensity().Num() == SmoothWindow->GetDensity().Num();
		if (bBoxSmoothParity)
		{
			for (int32 I = 0; I < SmoothWindow->GetDensity().Num(); ++I)
			{
				bBoxSmoothParity &= SmoothBox->GetDensity()[I] == SmoothWindow->GetDensity()[I];
			}
		}

		// Native noise, nonzero detail and overhang, slope normalization and
		// smoothing all participate. The second 35-cell tile has a shorter
		// noise row than a chunk; compare every stored sample across x=-1/0.
		FVoxelLandM4HeightDetailConfig ActiveDetail;
		ActiveDetail.bEnabled = true;
		ActiveDetail.AmplitudeQ8 = 128000;
		ActiveDetail.FrequencyPerCm = 0.004f;
		ActiveDetail.NodeId = FGuid(11, 22, 33, 44);
		FVoxelLandM4OverhangConfig ActiveOverhang;
		ActiveOverhang.Displacement.bEnabled = true;
		ActiveOverhang.Displacement.AmplitudeQ8 = 128000;
		ActiveOverhang.Displacement.BandHalfWidthQ8 = 256000;
		ActiveOverhang.FrequencyPerCm = 0.004f;
		ActiveOverhang.NodeId = FGuid(55, 66, 77, 88);
		FVoxelLandM4HeightDetailConfig NoDetail;
		FVoxelLandM4OverhangConfig NoOverhang;
		TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Shaped, NoDetailGenerator, NoOverhangGenerator;
		if (FVoxelLandGenerator::CreateM4HeightField(
				Settings, UnevenGrid, true, ActiveOverhang, Smooth, ActiveDetail, Shaped)
			|| FVoxelLandGenerator::CreateM4HeightField(
				Settings, UnevenGrid, true, ActiveOverhang, Smooth, NoDetail, NoDetailGenerator)
			|| FVoxelLandGenerator::CreateM4HeightField(
				Settings, UnevenGrid, true, NoOverhang, Smooth, ActiveDetail, NoOverhangGenerator)
			|| !Shaped || !NoDetailGenerator || !NoOverhangGenerator)
		{
			return false;
		}
		TUniquePtr<FVoxelLandWorkerContext> ShapedWorker, NoDetailWorker, NoOverhangWorker;
		if (Shaped->CreateWorker(ShapedWorker) || NoDetailGenerator->CreateWorker(NoDetailWorker)
			|| NoOverhangGenerator->CreateWorker(NoOverhangWorker) || !ShapedWorker || !NoDetailWorker
			|| !NoOverhangWorker)
		{
			return false;
		}
		TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe> ShapedLeft, ShapedRight, WithoutDetail,
			WithoutOverhang;
		if (Shaped->GenerateChunk(LeftKey, *ShapedWorker, ShapedLeft)
			|| Shaped->GenerateChunk(RightKey, *ShapedWorker, ShapedRight)
			|| NoDetailGenerator->GenerateChunk(RightKey, *NoDetailWorker, WithoutDetail)
			|| NoOverhangGenerator->GenerateChunk(RightKey, *NoOverhangWorker, WithoutOverhang) || !ShapedLeft
			|| !ShapedRight || !WithoutDetail || !WithoutOverhang)
		{
			return false;
		}
		bDetailEffect = ShapedRight->Density != WithoutDetail->Density;
		bOverhangEffect = ShapedRight->Density != WithoutOverhang->Density;
		TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> ShapedBudget;
		FVoxelLandM5Budget::TryCreate(16 * 1024 * 1024, 10000000, ShapedBudget);
		TSharedPtr<const FVoxelLandM5BaselineWindow, ESPMode::ThreadSafe> ShapedBox;
		const auto ShapedError = FVoxelLandM5BaselineWindow::BuildBox(
			Shaped.ToSharedRef(), *ShapedWorker, SmoothBoxMin, FIntVector(67, 35, 35), ShapedBudget.ToSharedRef(),
			[] { return false; }, ShapedBox);
		bShapedBoxParity = ShapedError.Code == EVoxelLandM5ErrorCode::None && ShapedBox;
		if (bShapedBoxParity)
		{
			for (int32 Z = 0; Z < 35; ++Z)
			{
				for (int32 Y = 0; Y < 35; ++Y)
				{
					for (int32 X = 0; X < 35; ++X)
					{
						const int32 ChunkIndex = X + 35 * (Y + 35 * Z);
						bShapedBoxParity &=
							ShapedLeft->Density[ChunkIndex] == ShapedBox->GetDensity()[X + 67 * (Y + 35 * Z)]
							&& ShapedRight->Density[ChunkIndex]
								   == ShapedBox->GetDensity()[(X + 32) + 67 * (Y + 35 * Z)];
					}
				}
			}
		}

		FVoxelLandProgramDraft ControlDraft;
		ControlDraft.RegisterCount = 1;
		ControlDraft.Constants = {0.25f};
		FVoxelLandInstruction XInput, ControlStore;
		XInput.Op = EVoxelLandOpcode::LoadConstant;
		ControlStore.Op = EVoxelLandOpcode::StoreDensity;
		ControlDraft.Instructions = {XInput, ControlStore};
		TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> ControlProgram;
		if (FVoxelLandProgram::Validate(ControlDraft, ControlProgram))
		{
			Failure = TEXT("local control program validation failed");
			return false;
		}
		FVoxelLandGeneratorSettings ControlSettings;
		ControlSettings.Program = ControlProgram;
		ControlSettings.Profile.VoxelSizeCm = 100.f;
		TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Control;
		if (FVoxelLandGenerator::Create(ControlSettings, Control) || !Control)
		{
			Failure = TEXT("local control generator creation failed");
			return false;
		}
		TArray<int32> ZeroValues;
		ZeroValues.Init(0, FVoxelLandM4CurveLut::SampleCount);
		TSharedPtr<const FVoxelLandM4CurveLut, ESPMode::ThreadSafe> ZeroCurve;
		FString CurveError;
		if (!FVoxelLandM4CurveLut::Create(ZeroValues, 0, 0, ZeroCurve, CurveError))
		{
			Failure = TEXT("local curve creation failed");
			return false;
		}
		FVoxelLandM4UnlimitedConfig Local;
		Local.ContinentalGenerator = Control;
		Local.MountainGenerator = Control;
		Local.PeaksGenerator = Control;
		Local.ContinentalCurve = ZeroCurve;
		Local.MountainCurve = ZeroCurve;
		Local.PeaksCurve = ZeroCurve;
		TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> LocalGenerator;
		if (FVoxelLandGenerator::CreateM4Unlimited(Settings, Local, LocalGenerator) || !LocalGenerator)
		{
			Failure = TEXT("local M4 generator creation failed");
			return false;
		}
		TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> LocalBudget;
		FVoxelLandM5Budget::TryCreate(16 * 1024 * 1024, 10000000, LocalBudget);
		TSharedPtr<const FVoxelLandM5BaselineWindow, ESPMode::ThreadSafe> LocalWindow;
		const FVoxelLandM5Diagnostic LocalError = FVoxelLandM5BaselineWindow::Build(
			LocalGenerator.ToSharedRef(), FIntVector(-1, 0, 0), FIntVector(1, 1, 1), 2, LocalBudget.ToSharedRef(),
			[] { return false; }, LocalWindow);
		if (LocalError.Code != EVoxelLandM5ErrorCode::None || !LocalWindow)
		{
			Failure = TEXT("local M4 window generation failed");
			return false;
		}
		TUniquePtr<FVoxelLandWorkerContext> LocalWorker;
		TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe> LocalChunk;
		if (LocalGenerator->CreateWorker(LocalWorker) || !LocalWorker
			|| LocalGenerator->GenerateChunk(Key, *LocalWorker, LocalChunk) || !LocalChunk)
		{
			Failure = TEXT("direct local M4 chunk failed");
			return false;
		}
		bLocalMatches = LocalWindow->GetSize() == FIntVector(67, 35, 35)
					 && LocalChunk->Density.Num() == FVoxelLandChunkData::SampleCount;
		for (int32 Z = 0; Z < 35 && bLocalMatches; ++Z)
		{
			for (int32 Y = 0; Y < 35 && bLocalMatches; ++Y)
			{
				for (int32 X = 0; X < 35; ++X)
				{
					bLocalMatches &= LocalChunk->Density[X + 35 * (Y + 35 * Z)]
								  == LocalWindow->GetDensity()[(X + 32) + 67 * (Y + 35 * Z)];
				}
			}
		}

		// Fixed-lattice controls require scalar output rather than raw LoadX.
		FVoxelLandProgramDraft FixedDraft;
		FixedDraft.FormatVersion = 2;
		FixedDraft.RegisterCount = 1;
		FixedDraft.Constants = {0.f};
		FVoxelLandInstruction Constant, FixedStore;
		Constant.Op = EVoxelLandOpcode::LoadConstant;
		FixedStore.Op = EVoxelLandOpcode::StoreDensity;
		FixedDraft.Instructions = {Constant, FixedStore};
		TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> FixedProgram;
		TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> FixedControl, FixedGenerator;
		if (FVoxelLandProgram::Validate(FixedDraft, FixedProgram))
		{
			Failure = TEXT("fixed-lattice control program invalid");
			return false;
		}
		ControlSettings.Program = FixedProgram;
		if (FVoxelLandGenerator::Create(ControlSettings, FixedControl) || !FixedControl)
		{
			Failure = TEXT("fixed-lattice control generator failed");
			return false;
		}
		Local.ContinentalGenerator = Local.MountainGenerator = Local.PeaksGenerator = FixedControl;
		if (FVoxelLandGenerator::CreateM4Unlimited(Settings, Local, FixedGenerator) || !FixedGenerator)
		{
			Failure = TEXT("fixed-lattice M4 generator failed");
			return false;
		}
		TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> FixedBudget;
		FVoxelLandM5Budget::TryCreate(16 * 1024 * 1024, 10000000, FixedBudget);
		TSharedPtr<const FVoxelLandM5BaselineWindow, ESPMode::ThreadSafe> FixedWindow;
		const auto FixedError = FVoxelLandM5BaselineWindow::Build(
			FixedGenerator.ToSharedRef(), FIntVector(-1, 0, 0), FIntVector(1, 1, 1), 2, FixedBudget.ToSharedRef(),
			[] { return false; }, FixedWindow);
		TUniquePtr<FVoxelLandWorkerContext> FixedWorker;
		TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe> FixedChunk;
		if (FixedError.Code != EVoxelLandM5ErrorCode::None || !FixedWindow || FixedGenerator->CreateWorker(FixedWorker)
			|| !FixedWorker || FixedGenerator->GenerateChunk(Key, *FixedWorker, FixedChunk) || !FixedChunk)
		{
			Failure = TEXT("fixed-lattice M4 window failed");
			return false;
		}
		bFixedLocalMatches = FixedWindow->GetSize() == FIntVector(67, 35, 35)
						  && FixedChunk->Density.Num() == FVoxelLandChunkData::SampleCount;
		for (int32 Z = 0; Z < 35 && bFixedLocalMatches; ++Z)
		{
			for (int32 Y = 0; Y < 35 && bFixedLocalMatches; ++Y)
			{
				for (int32 X = 0; X < 35; ++X)
				{
					bFixedLocalMatches &= FixedChunk->Density[X + 35 * (Y + 35 * Z)]
									   == FixedWindow->GetDensity()[(X + 32) + 67 * (Y + 35 * Z)];
				}
			}
		}
		return true;
	}).Get();
	TestTrue(FString::Printf(TEXT("worker setup: %s"), *Failure), bCompleted);
	TestTrue(TEXT("flat-field independent reference across shared planes"), bAnalytic);
	TestTrue(TEXT("merged samples match direct M4 chunk"), bExactChunk);
	TestTrue(TEXT("admitted worker reproduces entire baseline window"), bReusedWorker);
	TestTrue(TEXT("foreign worker rejected before admission"), bForeignWorker);
	TestTrue(TEXT("wrong-thread worker rejected before admission"), bWrongThread);
	TestTrue(TEXT("admitted worker cancellation preserves output"), bWorkerCancelled);
	TestTrue(TEXT("rectangular finite box matches complete chunk assembly"), bBoxParity);
	TestTrue(TEXT("nonuniform smoothed box matches complete chunk assembly"), bBoxSmoothParity);
	TestTrue(TEXT("native detail affects chunk density"), bDetailEffect);
	TestTrue(TEXT("native overhang affects chunk density"), bOverhangEffect);
	TestTrue(TEXT("native detail, overhang and slope box matches both negative-seam chunks"), bShapedBoxParity);
	TestTrue(TEXT("box cancellation preserves output"), bBoxAtomic);
	TestTrue(TEXT("box exhaustion preserves output"), bBoxBudget);
	TestTrue(TEXT("unsupported domain preserves prior result"), bAtomic);
	TestTrue(TEXT("budget failure preserves prior result"), bBudget);
	TestTrue(TEXT("cancellation preserves prior result"), bCancelled);
	TestTrue(TEXT("in-flight cancellation preserves prior result"), bMidCancellation);
	TestTrue(TEXT("local-mode samples match direct M4 chunk"), bLocalMatches);
	TestTrue(TEXT("fixed-lattice local samples match direct M4 chunk"), bFixedLocalMatches);
	TestTrue(TEXT("finite boundary checked before scratch allocation"), bBoundary);
	TestTrue(TEXT("positive, negative and empty extreme windows fail before admission"), bSampleLimits);
	TestTrue(TEXT("nonuniform smoothed neighboring halos agree"), bNonuniformSmooth);
	return bCompleted;
}

#endif
