#if WITH_DEV_AUTOMATION_TESTS

#include "Async/Async.h"
#include "Data/Structs/VoxelLandM4UnlimitedConfig.h"
#include "Data/Structs/VoxelLandM4SmoothSettings.h"
#include "Generation/VoxelLandGenerator.h"
#include "Generation/VoxelLandM4CurveLut.h"
#include "Generation/VoxelLandM4HeightGrid.h"
#include "Generation/VoxelLandM5BaselineWindow.h"
#include "Generation/VoxelLandM5Budget.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM5BaselineWindowTest, "VoxelLand.M5.BaselineWindow.Finite",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FVoxelLandM5BaselineWindowTest::RunTest(const FString& Parameters)
{
	FString Failure;
	bool bAnalytic = false, bExactChunk = false, bAtomic = false, bBudget = false, bCancelled = false;
	bool bLocalMatches = false, bMidCancellation = false, bBoundary = false, bNonuniformSmooth = false;
	bool bFixedLocalMatches = false, bSampleLimits = false;
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
