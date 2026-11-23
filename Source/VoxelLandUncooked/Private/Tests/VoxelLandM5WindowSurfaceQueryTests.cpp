#if WITH_DEV_AUTOMATION_TESTS

#include "Async/Async.h"
#include "Generation/VoxelLandGenerator.h"
#include "Generation/VoxelLandM4HeightGrid.h"
#include "Generation/VoxelLandM5BaselineWindow.h"
#include "Generation/VoxelLandM5Budget.h"
#include "Generation/VoxelLandM5ProtectionGrid.h"
#include "Generation/VoxelLandM5WindowSurfaceQuery.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM5WindowSurfaceQueryTest, "VoxelLand.M5.WindowSurfaceQuery.Finite",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FVoxelLandM5WindowSurfaceQueryTest::RunTest(const FString& Parameters)
{
	bool bFlat = false, bProtected = false, bProtectAll = false, bMissingVertical = false;
	bool bMissingHorizontal = false, bWrongBudget = false, bNoDry = false, bCancelled = false;
	const bool bSetup = Async(EAsyncExecution::ThreadPool, [&]() {
		FVoxelLandProgramDraft Draft;
		Draft.RegisterCount = 1;
		FVoxelLandInstruction Input, Store;
		Input.Op = EVoxelLandOpcode::LoadZ;
		Store.Op = EVoxelLandOpcode::StoreDensity;
		Draft.Instructions = {Input, Store};
		TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Program;
		if (FVoxelLandProgram::Validate(Draft, Program))
		{
			return false;
		}
		TArray<int32> Heights;
		Heights.Init(50 * 256, 81 * 81);
		TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe> Grid;
		if (!FVoxelLandM4HeightGrid::Create(-4000, -4000, 80, 80, 100, Heights, Grid))
		{
			return false;
		}
		FVoxelLandGeneratorSettings Settings;
		Settings.Program = Program;
		Settings.Profile.VoxelSizeCm = 100.f;
		TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Generator;
		if (FVoxelLandGenerator::CreateM4HeightField(Settings, Grid, false, Generator) || !Generator)
		{
			return false;
		}
		TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget;
		if (!FVoxelLandM5Budget::TryCreate(32 * 1024 * 1024, 20000000, Budget))
		{
			return false;
		}
		TSharedPtr<const FVoxelLandM5BaselineWindow, ESPMode::ThreadSafe> Window;
		if (FVoxelLandM5BaselineWindow::Build(Generator.ToSharedRef(), FIntVector(-1, -1, -1),
				FIntVector(1, 1, 1), 8, Budget.ToSharedRef(), [] { return false; }, Window).Code
			!= EVoxelLandM5ErrorCode::None || !Window)
		{
			return false;
		}
		int64 Height = 9, Low = 10, High = 11;
		int32 Slope = 12;
		bool bMarked = true, bFlooded = true;
		const auto Query = [&](const FVoxelLandM5ProtectionGrid* Mask, bool bAll, bool bDry,
			TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> QueryBudget, TFunctionRef<bool()> Cancel) {
			return VoxelLand::M5::WindowSurfaceQuery::QueryFlat(*Window, Mask, bAll, bDry, QueryBudget,
				0, 0, 0, 1000000, Cancel, Height, Low, High, Slope, bMarked, bFlooded);
		};
		const auto Never = [] { return false; };
		auto Result = Query(nullptr, false, true, Budget.ToSharedRef(), Never);
		bFlat = Result.Code == EVoxelLandM5ErrorCode::None && Height == 50 && Low == 50 && High == 50
			&& Slope == 0 && !bMarked && !bFlooded;
		Result = Query(nullptr, true, true, Budget.ToSharedRef(), Never);
		bProtectAll = Result.Code == EVoxelLandM5ErrorCode::None && bMarked;

		FVoxelLandM5PointCm MaskOrigin = Window->GetOriginCm();
		MaskOrigin.X -= 100;
		MaskOrigin.Y -= 100;
		MaskOrigin.Z -= 100;
		const FIntVector MaskSize(69, 69, 69);
		TArray<uint8> Cells;
		Cells.Init(0, 69 * 69 * 69);
		Cells[34 + 69 * (34 + 69 * 34)] = 1;
		TSharedPtr<const FVoxelLandM5ProtectionGrid, ESPMode::ThreadSafe> Mask;
		if (FVoxelLandM5ProtectionGrid::Capture(MaskSize, MaskOrigin, 100, Cells, Budget.ToSharedRef(), Never, Mask).Code
			!= EVoxelLandM5ErrorCode::None || !Mask)
		{
			return false;
		}
		Result = Query(Mask.Get(), false, true, Budget.ToSharedRef(), Never);
		bProtected = Result.Code == EVoxelLandM5ErrorCode::None && bMarked;
		Height = 91;
		Low = 92;
		High = 93;
		Slope = 94;
		bMarked = false;
		bFlooded = true;
		Result = Query(Mask.Get(), false, false, Budget.ToSharedRef(), Never);
		bNoDry = Result.Code == EVoxelLandM5ErrorCode::Unproven && Height == 91 && Low == 92
			&& High == 93 && Slope == 94 && !bMarked && bFlooded;
		Result = Query(Mask.Get(), false, true, Budget.ToSharedRef(), [] { return true; });
		bCancelled = Result.Code == EVoxelLandM5ErrorCode::Cancelled && Height == 91 && !bMarked;

		TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> OtherBudget;
		FVoxelLandM5Budget::TryCreate(32 * 1024 * 1024, 20000000, OtherBudget);
		Result = Query(Mask.Get(), false, true, OtherBudget.ToSharedRef(), Never);
		bWrongBudget = Result.Code == EVoxelLandM5ErrorCode::InvalidInput && Height == 91;

		FVoxelLandM5PointCm ShortOrigin = MaskOrigin;
		ShortOrigin.Z += 100;
		TSharedPtr<const FVoxelLandM5ProtectionGrid, ESPMode::ThreadSafe> ShortVertical;
		if (FVoxelLandM5ProtectionGrid::Capture(MaskSize, ShortOrigin, 100, Cells, Budget.ToSharedRef(), Never,
				ShortVertical).Code != EVoxelLandM5ErrorCode::None)
		{
			return false;
		}
		Result = Query(ShortVertical.Get(), false, true, Budget.ToSharedRef(), Never);
		bMissingVertical = Result.Code == EVoxelLandM5ErrorCode::OutOfDomain && Height == 91;
		ShortOrigin = MaskOrigin;
		ShortOrigin.X += 100;
		TSharedPtr<const FVoxelLandM5ProtectionGrid, ESPMode::ThreadSafe> ShortHorizontal;
		if (FVoxelLandM5ProtectionGrid::Capture(MaskSize, ShortOrigin, 100, Cells, Budget.ToSharedRef(), Never,
				ShortHorizontal).Code != EVoxelLandM5ErrorCode::None)
		{
			return false;
		}
		Result = Query(ShortHorizontal.Get(), false, true, Budget.ToSharedRef(), Never);
		bMissingHorizontal = Result.Code == EVoxelLandM5ErrorCode::OutOfDomain && Height == 91;
		return true;
	}).Get();
	TestTrue(TEXT("fixture setup"), bSetup);
	TestTrue(TEXT("flat dry window"), bFlat);
	TestTrue(TEXT("authored mask protects"), bProtected);
	TestTrue(TEXT("constant protection protects"), bProtectAll);
	TestTrue(TEXT("missing vertical coverage rejected"), bMissingVertical);
	TestTrue(TEXT("missing horizontal coverage rejected"), bMissingHorizontal);
	TestTrue(TEXT("foreign budget rejected"), bWrongBudget);
	TestTrue(TEXT("unproven water rejected without output changes"), bNoDry);
	TestTrue(TEXT("cancellation preserves output"), bCancelled);
	return bSetup;
}

#endif
