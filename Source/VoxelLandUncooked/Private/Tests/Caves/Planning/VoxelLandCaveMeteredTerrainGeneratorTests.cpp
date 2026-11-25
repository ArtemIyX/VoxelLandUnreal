#if WITH_DEV_AUTOMATION_TESTS
#include "Async/Async.h"
#include "Data/Terrain/Shaping/VoxelLandTerrainSmoothingSettings.h"
#include "Generation/Field/VoxelLandGenerator.h"
#include "Generation/Terrain/Worlds/VoxelLandTerrainGridLease.h"
#include "Generation/Terrain/Heightfield/VoxelLandHeightGrid.h"
#include "Generation/Caves/Infrastructure/VoxelLandCaveBudget.h"
#include "Misc/AutomationTest.h"

class FVoxelLandM5MeteredM4GeneratorFixtureLease final : public FVoxelLandM4GridLease
{
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM5MeteredM4GeneratorTest, "VoxelLand.M5.Runtime.MeteredM4Generator",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FVoxelLandM5MeteredM4GeneratorTest::RunTest(const FString& Parameters)
{
	bool bBuilt = false, bCompatible = false, bInvalid = false, bForeign = false;
	bool bCancelled = false, bExhausted = false, bLease = false, bReleased = false;
	FString Message;
	TWeakPtr<const FVoxelLandM4GridLease, ESPMode::ThreadSafe> WeakLease;
	Async(EAsyncExecution::ThreadPool, [&] {
		const auto Never = [] {
			return false;
		};
		TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, Other;
		if (!FVoxelLandM5Budget::TryCreate(128ll * 1024 * 1024, 100000000, Budget)
			|| !FVoxelLandM5Budget::TryCreate(128ll * 1024 * 1024, 100000000, Other))
		{
			return;
		}
		FVoxelLandProgramDraft Draft;
		Draft.RegisterCount = 1;
		FVoxelLandInstruction Load, Store;
		Load.Op = EVoxelLandOpcode::LoadZ;
		Store.Op = EVoxelLandOpcode::StoreDensity;
		Draft.Instructions = {Load, Store};
		TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Program;
		if (FVoxelLandProgram::ValidateMetered(Draft, Budget.ToSharedRef(), Never, Program).Code
				!= EVoxelLandM5ErrorCode::None
			|| !Program)
		{
			return;
		}
		TArray<int32> Heights;
		Heights.Init(0, 129 * 129);
		TSharedPtr<const FVoxelLandM4GridLease, ESPMode::ThreadSafe> GridLease =
			MakeShared<FVoxelLandM5MeteredM4GeneratorFixtureLease, ESPMode::ThreadSafe>();
		WeakLease = GridLease;
		TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe> Grid;
		if (!FVoxelLandM4HeightGrid::Create(-6400, -6400, 128, 128, 100, Heights, Grid, Never, GridLease))
		{
			return;
		}
		FVoxelLandGeneratorSettings Settings;
		Settings.Program = Program;
		Settings.Profile.VoxelSizeCm = 100.f;
		Settings.WorldSeed = 12345;
		FVoxelLandM4OverhangConfig Overhang;
		FVoxelLandM4SmoothSettings Smooth;
		FVoxelLandM4HeightDetailConfig Detail;
		Detail.bEnabled = true;
		Detail.AmplitudeQ8 = 128000;
		Detail.FrequencyPerCm = 0.004f;
		Detail.NodeId = FGuid(11, 22, 33, 44);
		TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Metered, Ordinary;
		const auto Built = FVoxelLandGenerator::CreateM4HeightFieldMetered(
			Settings, Grid, true, Overhang, Smooth, Detail, Budget.ToSharedRef(), Never, Metered);
		Message = Built.Message;
		if (Built.Code != EVoxelLandM5ErrorCode::None || !Metered
			|| FVoxelLandGenerator::CreateM4HeightField(Settings, Grid, true, Overhang, Smooth, Detail, Ordinary)
			|| !Ordinary)
		{
			return;
		}
		bBuilt = true;
		TUniquePtr<FVoxelLandWorkerContext> AWorker, BWorker;
		if (Metered->CreateWorker(AWorker) || Ordinary->CreateWorker(BWorker))
		{
			return;
		}
		FVoxelLandChunkKey Key;
		Key.Coordinate = FIntVector(-1, -1, -1);
		TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe> A, B;
		if (Metered->GenerateChunk(Key, *AWorker, A) || Ordinary->GenerateChunk(Key, *BWorker, B) || !A || !B)
		{
			return;
		}
		bCompatible = A->Density == B->Density && A->Hash == B->Hash;
		const auto Original = Metered;
		FVoxelLandM4SmoothSettings Invalid = Smooth;
		Invalid.Version = 2;
		const auto InvalidResult = FVoxelLandGenerator::CreateM4HeightFieldMetered(
			Settings, Grid, true, Overhang, Invalid, Detail, Budget.ToSharedRef(), Never, Metered);
		bInvalid = InvalidResult.Code == EVoxelLandM5ErrorCode::InvalidInput && Metered == Original;
		const auto ForeignResult = FVoxelLandGenerator::CreateM4HeightFieldMetered(
			Settings, Grid, true, Overhang, Smooth, Detail, Other.ToSharedRef(), Never, Metered);
		bForeign = ForeignResult.Code == EVoxelLandM5ErrorCode::InvalidInput && Metered == Original;
		const auto CancelResult = FVoxelLandGenerator::CreateM4HeightFieldMetered(
			Settings, Grid, true, Overhang, Smooth, Detail, Budget.ToSharedRef(), [] { return true; }, Metered);
		bCancelled = CancelResult.Code == EVoxelLandM5ErrorCode::Cancelled && Metered == Original;
		TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> TinyProgram;
		// A same-budget program with no remaining admission capacity cannot be
		// manufactured without violating its lease, so exhaust a fresh meter after
		// validating its program instead.
		TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Limited;
		if (!FVoxelLandM5Budget::TryCreate(128ll * 1024 * 1024, 100000000, Limited)
			|| FVoxelLandProgram::ValidateMetered(Draft, Limited.ToSharedRef(), Never, TinyProgram).Code
				   != EVoxelLandM5ErrorCode::None)
		{
			return;
		}
		int64 Live = 0, Peak = 0, Work = 0;
		Limited->GetUsage(Live, Peak, Work);
		if (!Limited->TryChargeWork(100000000 - Work))
		{
			return;
		}
		FVoxelLandGeneratorSettings LimitedSettings = Settings;
		LimitedSettings.Program = TinyProgram;
		const auto Exhausted = FVoxelLandGenerator::CreateM4HeightFieldMetered(
			LimitedSettings, Grid, true, Overhang, Smooth, Detail, Limited.ToSharedRef(), Never, Metered);
		bExhausted = Exhausted.Code == EVoxelLandM5ErrorCode::BudgetExceeded && Metered == Original;
		GridLease.Reset();
		Grid.Reset();
		Ordinary.Reset();
		BWorker.Reset();
		bLease = WeakLease.IsValid();
		Metered.Reset();
		AWorker.Reset();
		// Original retains the generator and therefore its grid until scope exit.
		bReleased = WeakLease.IsValid();
	}).Wait();
	bReleased &= !WeakLease.IsValid();
	TestTrue(FString::Printf(TEXT("metered M4 setup: %s"), *Message), bBuilt);
	TestTrue(TEXT("metered and ordinary chunks match"), bCompatible);
	TestTrue(TEXT("invalid options preserve generator"), bInvalid);
	TestTrue(TEXT("foreign program budget rejected"), bForeign);
	TestTrue(TEXT("cancellation preserves generator"), bCancelled);
	TestTrue(TEXT("exhausted budget preserves generator"), bExhausted);
	TestTrue(TEXT("generator retains grid lease"), bLease && bReleased);
	return bBuilt;
}
#endif
