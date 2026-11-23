#if WITH_DEV_AUTOMATION_TESTS

#include "Async/Async.h"
#include "Data/Structs/VoxelLandM5StageManifest.h"
#include "Generation/VoxelLandGenerator.h"
#include "Generation/VoxelLandM4HeightGrid.h"
#include "Generation/VoxelLandM5BaselineWindow.h"
#include "Generation/VoxelLandM5Budget.h"
#include "Generation/VoxelLandM5CandidatePlanner.h"
#include "Generation/VoxelLandM5CandidateResult.h"
#include "Generation/VoxelLandM5FinalField.h"
#include "Generation/VoxelLandM5WindowFinalField.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM5WindowFinalFieldTest, "VoxelLand.M5.WindowFinalField.Finite",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FVoxelLandM5WindowFinalFieldTest::RunTest(const FString& Parameters)
{
	bool bSuccess = false, bVersion = false, bSeed = false, bCancelled = false, bForeign = false;
	bool bPocketOnly = false, bPocketCandidateRejected = false, bBothDisabledRejected = false;
	bool bNetworkCandidateMissing = false;
	bool bPocketVersionRejected = false, bPocketCancel = false, bPocketMaskMissing = false;
	const bool bSetup = Async(EAsyncExecution::ThreadPool, [&]() {
		const auto Never = [] { return false; };
		FVoxelLandProgramDraft Draft;
		Draft.RegisterCount = 1;
		FVoxelLandInstruction Input, Store;
		Input.Op = EVoxelLandOpcode::LoadZ;
		Store.Op = EVoxelLandOpcode::StoreDensity;
		Draft.Instructions = {Input, Store};
		TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Program;
		if (FVoxelLandProgram::Validate(Draft, Program) || !Program) { return false; }
		TArray<int32> Heights;
		Heights.Init(50 * 256, 81 * 81);
		TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe> Grid;
		if (!FVoxelLandM4HeightGrid::Create(-4000, -4000, 80, 80, 100, Heights, Grid)) { return false; }
		FVoxelLandGeneratorSettings GeneratorSettings;
		GeneratorSettings.Program = Program;
		GeneratorSettings.Profile.VoxelSizeCm = 100.f;
		TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Generator;
		if (FVoxelLandGenerator::CreateM4HeightField(GeneratorSettings, Grid, false, Generator) || !Generator) { return false; }
		TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget;
		if (!FVoxelLandM5Budget::TryCreate(64 * 1024 * 1024, 100000000, Budget)) { return false; }
		TSharedPtr<const FVoxelLandM5BaselineWindow, ESPMode::ThreadSafe> Window;
		if (FVoxelLandM5BaselineWindow::Build(Generator.ToSharedRef(), FIntVector(-1, -1, -1),
				FIntVector(1, 1, 1), 8, Budget.ToSharedRef(), Never, Window).Code != EVoxelLandM5ErrorCode::None || !Window)
		{ return false; }
		FVoxelLandM5CaveSettings Settings;
		Settings.bEnabled = true;
		Settings.StageId = FGuid(1, 2, 3, 4);
		Settings.Bounds.MinInclusive.X = Settings.Bounds.MinInclusive.Y = Settings.Bounds.MinInclusive.Z = -3000;
		Settings.Bounds.MaxExclusive.X = Settings.Bounds.MaxExclusive.Y = Settings.Bounds.MaxExclusive.Z = 3001;
		Settings.Topology.MinChambers = Settings.Topology.MaxChambers = 3;
		Settings.Topology.MinLoops = 0;
		Settings.Topology.MaxLoops = 3;
		Settings.Topology.EntranceCount = 0;
		Settings.Topology.MinTunnelRadiusCm = Settings.Topology.MaxTunnelRadiusCm = 100;
		Settings.Topology.MinChamberRadiusCm = Settings.Topology.MaxChamberRadiusCm = 200;
		Settings.Capsule.ClearanceMarginCm = 0;
		Settings.RoofThicknessCm = 0;
		Settings.Limits.MaxCandidates = 3;
		Settings.Limits.MaxEdges = 3;
		Settings.Limits.MaxSegments = 4;
		Settings.Limits.MaxAttempts = 4;
		Settings.Limits.MaxBytes = 64 * 1024 * 1024;
		Settings.Limits.MaxValidationWork = 100000000;
		Settings.Water.Policy = EVoxelLandM5WaterPolicy::DryWorld;
		TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> Candidate;
		if (VoxelLand::M5::CandidatePlanner::Build(Settings, 42, {}, Budget.ToSharedRef(), Never, Candidate).Code
				!= EVoxelLandM5ErrorCode::None || !Candidate) { return false; }
		const FVoxelLandM5PointCm Origin = Window->GetOriginCm();
		const FIntVector Start(int32(-Origin.X / 100), int32(-Origin.Y / 100), int32((-1000 - Origin.Z) / 100));
		const FIntVector One(1, 1, 1);
		FVoxelLandM5StageManifest Manifest;
		TSharedPtr<const FVoxelLandM5FinalField, ESPMode::ThreadSafe> Out;
		auto D = VoxelLand::M5::WindowFinalField::Build(Settings, Manifest, 42, *Window,
			Candidate.ToSharedRef(), nullptr, Start, One, Budget.ToSharedRef(), Never, Out);
		bSuccess = D.Code == EVoxelLandM5ErrorCode::None && Out && Out->GetDensity().Num() == 1;
		if (!Out) { return true; }
		const auto Original = Out;
		D = VoxelLand::M5::WindowFinalField::Build(Settings, Manifest, 42, *Window,
			nullptr, nullptr, Start, One, Budget.ToSharedRef(), Never, Out);
		bNetworkCandidateMissing = D.Code == EVoxelLandM5ErrorCode::InvalidInput && Out == Original;
		Manifest.Composition = 2;
		D = VoxelLand::M5::WindowFinalField::Build(Settings, Manifest, 42, *Window,
			Candidate.ToSharedRef(), nullptr, Start, One, Budget.ToSharedRef(), Never, Out);
		bVersion = D.Code == EVoxelLandM5ErrorCode::UnsupportedVersion && Out == Original;
		Manifest.Composition = 1;
		D = VoxelLand::M5::WindowFinalField::Build(Settings, Manifest, 41, *Window,
			Candidate.ToSharedRef(), nullptr, Start, One, Budget.ToSharedRef(), Never, Out);
		bSeed = D.Code != EVoxelLandM5ErrorCode::None && Out == Original;
		D = VoxelLand::M5::WindowFinalField::Build(Settings, Manifest, 42, *Window,
			Candidate.ToSharedRef(), nullptr, Start, One, Budget.ToSharedRef(), [] { return true; }, Out);
		bCancelled = D.Code == EVoxelLandM5ErrorCode::Cancelled && Out == Original;
		TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Other;
		if (!FVoxelLandM5Budget::TryCreate(64 * 1024 * 1024, 100000000, Other)) { return false; }
		D = VoxelLand::M5::WindowFinalField::Build(Settings, Manifest, 42, *Window,
			Candidate.ToSharedRef(), nullptr, Start, One, Other.ToSharedRef(), Never, Out);
		bForeign = D.Code == EVoxelLandM5ErrorCode::InvalidInput && Out == Original;

		// Independent known pocket-noise vector at global origin: 24801 for
		// this stage, seed, salt and period (VoxelLand.M5.Pockets.ReferenceVectors).
		// The second flat M4 surface is 1050 cm, so origin has a complete deep
		// pocket interval while retaining a strictly signed crossing.
		TArray<int32> HighHeights;
		HighHeights.Init(1050 * 256, 81 * 81);
		TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe> HighGrid;
		if (!FVoxelLandM4HeightGrid::Create(-4000, -4000, 80, 80, 100, HighHeights, HighGrid)) { return false; }
		TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> HighGenerator;
		if (FVoxelLandGenerator::CreateM4HeightField(GeneratorSettings, HighGrid, false, HighGenerator) || !HighGenerator)
		{ return false; }
		TSharedPtr<const FVoxelLandM5BaselineWindow, ESPMode::ThreadSafe> HighWindow;
		if (FVoxelLandM5BaselineWindow::Build(HighGenerator.ToSharedRef(), FIntVector(-1, -1, -1),
				FIntVector(1, 1, 1), 8, Budget.ToSharedRef(), Never, HighWindow).Code != EVoxelLandM5ErrorCode::None
			|| !HighWindow) { return false; }
		FVoxelLandM5CaveSettings Pocket = Settings;
		Pocket.bEnabled = false;
		Pocket.StageId = FGuid(0x01234567u, 0x89abcdefu, 0x10203040u, 0xfedcba98u);
		Pocket.Pockets.bEnabled = true;
		Pocket.Pockets.LatticeSpacingCm = 16;
		Pocket.Pockets.ThresholdQ16 = 0;
		Pocket.Pockets.AmplitudeCm = 400;
		Pocket.Pockets.MinDepthCm = 0;
		Pocket.Pockets.MaxDepthCm = 5000;
		Pocket.Pockets.Salt = 0x0102030405060708LL;
		const auto HighOrigin = HighWindow->GetOriginCm();
		const FIntVector PocketStart(int32(-HighOrigin.X / 100), int32(-HighOrigin.Y / 100), int32(-HighOrigin.Z / 100));
		TSharedPtr<const FVoxelLandM5FinalField, ESPMode::ThreadSafe> PocketField;
		D = VoxelLand::M5::WindowFinalField::Build(Pocket, Manifest, -1, *HighWindow,
			nullptr, nullptr, PocketStart, One, Budget.ToSharedRef(), Never, PocketField);
		bPocketOnly = D.Code == EVoxelLandM5ErrorCode::None && PocketField
			&& PocketField->GetDensity().Num() == 1 && PocketField->GetDensity()[0] > 0;
		const auto PocketOriginal = PocketField;
		D = VoxelLand::M5::WindowFinalField::Build(Pocket, Manifest, -1, *HighWindow,
			Candidate.ToSharedRef(), nullptr, PocketStart, One, Budget.ToSharedRef(), Never, PocketField);
		bPocketCandidateRejected = D.Code == EVoxelLandM5ErrorCode::InvalidInput && PocketField == PocketOriginal;
		Pocket.Pockets.bEnabled = false;
		D = VoxelLand::M5::WindowFinalField::Build(Pocket, Manifest, -1, *HighWindow,
			nullptr, nullptr, PocketStart, One, Budget.ToSharedRef(), Never, PocketField);
		bBothDisabledRejected = D.Code == EVoxelLandM5ErrorCode::InvalidInput && PocketField == PocketOriginal;
		Pocket.Pockets.bEnabled = true;
		Pocket.Protection.bRequireMask = true;
		D = VoxelLand::M5::WindowFinalField::Build(Pocket, Manifest, -1, *HighWindow,
			nullptr, nullptr, PocketStart, One, Budget.ToSharedRef(), Never, PocketField);
		bPocketMaskMissing = D.Code == EVoxelLandM5ErrorCode::Unproven && PocketField == PocketOriginal;
		Pocket.Protection.bRequireMask = false;
		Manifest.PocketNoise = 2;
		D = VoxelLand::M5::WindowFinalField::Build(Pocket, Manifest, -1, *HighWindow,
			nullptr, nullptr, PocketStart, One, Budget.ToSharedRef(), Never, PocketField);
		bPocketVersionRejected = D.Code == EVoxelLandM5ErrorCode::UnsupportedVersion && PocketField == PocketOriginal;
		Manifest.PocketNoise = 1;
		D = VoxelLand::M5::WindowFinalField::Build(Pocket, Manifest, -1, *HighWindow,
			nullptr, nullptr, PocketStart, One, Budget.ToSharedRef(), [] { return true; }, PocketField);
		bPocketCancel = D.Code == EVoxelLandM5ErrorCode::Cancelled && PocketField == PocketOriginal;
		return true;
	}).Get();
	TestTrue(TEXT("fixture setup"), bSetup);
	TestTrue(TEXT("finite window composes one sample"), bSuccess);
	TestTrue(TEXT("enabled network requires its candidate"), bNetworkCandidateMissing);
	TestTrue(TEXT("unsupported version preserves output"), bVersion);
	TestTrue(TEXT("mismatched seed preserves output"), bSeed);
	TestTrue(TEXT("cancellation preserves output"), bCancelled);
	TestTrue(TEXT("foreign budget preserves output"), bForeign);
	TestTrue(TEXT("pocket-only stage carves known global sample"), bPocketOnly);
	TestTrue(TEXT("pocket-only stage rejects a network candidate"), bPocketCandidateRejected);
	TestTrue(TEXT("disabled stages reject scratch work"), bBothDisabledRejected);
	TestTrue(TEXT("pocket version preserves output"), bPocketVersionRejected);
	TestTrue(TEXT("pocket stage requires authored mask when requested"), bPocketMaskMissing);
	TestTrue(TEXT("pocket cancellation preserves output"), bPocketCancel);
	return bSetup;
}

#endif
