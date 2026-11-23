#if WITH_DEV_AUTOMATION_TESTS
#include "Data/Structs/VoxelLandFiniteWorldSnapshot.h"
#include "Data/Structs/VoxelLandM4WorldSnapshot.h"
#include "Misc/AutomationTest.h"
#include "World/VoxelLandFiniteSnapshotAdapter.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandFiniteSnapshotAdapterTest, "VoxelLand.M5.Runtime.FiniteSnapshotAdapter",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FVoxelLandFiniteSnapshotAdapterTest::RunTest(const FString& Parameters)
{
	FVoxelLandFiniteWorldSnapshot Source;
	Source.Common.Seed = -123;
	Source.Common.VoxelSizeCm = 75;
	Source.Common.BaseHeightQ8 = 1000;
	Source.Common.HeightStage.Version = 1;
	Source.Common.HeightStage.OutputNodeId = FGuid(11, 12, 13, 14);
	Source.Common.Continental.CurveKeys.AddDefaulted_GetRef().OutputQ8 = 21;
	Source.Common.Mountain.CurveKeys.AddDefaulted_GetRef().OutputQ8 = 22;
	Source.Common.Peaks.CurveKeys.AddDefaulted_GetRef().OutputQ8 = 23;
	Source.Common.bNormalizeSlope = true;
	Source.Common.bSmoothEnabled = true;
	Source.Common.SmoothStrength = 0.25f;
	Source.Common.HeightDetail.AmplitudeQ8 = 99;
	Source.Common.bOverhangEnabled = true;
	Source.Common.OverhangAmplitudeQ8 = 120;
	Source.Common.OverhangBandQ8 = 500;
	Source.Common.OverhangFrequencyPerCm = 0.02f;
	Source.Common.OverhangNodeId = FGuid(1, 2, 3, 4);
	Source.Common.OverhangSalt = 7;
	Source.Common.HorizontalRadius = 3;
	Source.Common.VerticalRadius = 2;
	Source.Finite.OriginXCm = -20000;
	Source.Finite.OriginYCm = -30000;
	Source.Finite.CellsX = 4;
	Source.Finite.CellsY = 5;
	Source.Finite.GridSpacingCm = 100;
	Source.Finite.ErosionStageId = FGuid(5, 6, 7, 8);
	Source.Finite.ErosionSalt = 9;
	Source.Finite.DropletCount = 10;
	Source.Finite.MaxSteps = 11;
	Source.Finite.ThermalIterations = 12;
	Source.Finite.CapacityMaxQ8 = 13;
	Source.Finite.TalusQ8 = 14;
	Source.Finite.MaxTransferQ8 = 15;
	FVoxelLandM4WorldSnapshot Out;
	FString Error;
	if (!TestTrue(TEXT("finite snapshot translated"), FVoxelLandFiniteSnapshotAdapter::TryConvert(Source, Out, Error)))
	{
		return false;
	}
	TestEqual(TEXT("format version"), Out.FormatVersion, 1);
	TestEqual(TEXT("seed"), Out.Seed, int64(-123));
	TestEqual(TEXT("voxel size"), Out.VoxelSizeCm, 75);
	TestEqual(TEXT("base height"), Out.BaseHeightQ8, 1000);
	TestTrue(TEXT("height stage retained"),
		Out.HeightStage.Version == 1 && Out.HeightStage.OutputNodeId == Source.Common.HeightStage.OutputNodeId);
	TestTrue(TEXT("control snapshots retained"),
		Out.Continental.CurveKeys.Num() == 1 && Out.Mountain.CurveKeys.Num() == 1 && Out.Peaks.CurveKeys.Num() == 1
			&& Out.Continental.CurveKeys[0].OutputQ8 == 21 && Out.Mountain.CurveKeys[0].OutputQ8 == 22
			&& Out.Peaks.CurveKeys[0].OutputQ8 == 23);
	TestTrue(TEXT("finite domain"), Out.Domain.Mode == EVoxelLandM4DomainMode::Finite && Out.Domain.bGlobalErosion);
	TestTrue(TEXT("grid geometry"), Out.Domain.OriginXCm == -20000 && Out.Domain.OriginYCm == -30000
										&& Out.Domain.CellsX == 4 && Out.Domain.CellsY == 5
										&& Out.Domain.GridSpacingCm == 100);
	TestTrue(TEXT("erosion identity"),
		Out.ErosionStageId == Source.Finite.ErosionStageId && Out.ErosionSalt == 9 && Out.DropletCount == 10);
	TestTrue(TEXT("erosion settings"), Out.Erosion.MaxSteps == 11 && Out.Erosion.ThermalIterations == 12
										   && Out.Erosion.CapacityMaxQ8 == 13 && Out.Erosion.TalusQ8 == 14
										   && Out.Erosion.MaxTransferQ8 == 15);
	TestTrue(TEXT("slope and smooth"),
		Out.bNormalizeSlope && Out.Smooth.Version == 1 && Out.Smooth.bEnabled && Out.Smooth.Strength == 0.25f);
	TestTrue(TEXT("detail and overhang"),
		Out.HeightDetail.AmplitudeQ8 == 99 && Out.bOverhangEnabled && Out.OverhangAmplitudeQ8 == 120
			&& Out.OverhangBandQ8 == 500 && Out.OverhangFrequencyPerCm == 0.02f
			&& Out.OverhangNodeId == Source.Common.OverhangNodeId && Out.OverhangSalt == 7);
	TestTrue(TEXT("radii"), Out.HorizontalRadius == 3 && Out.VerticalRadius == 2);
	Source.Finite.CellsX = 0;
	TestFalse(TEXT("invalid finite grid rejected"), FVoxelLandFiniteSnapshotAdapter::TryConvert(Source, Out, Error));
	TestTrue(TEXT("failure has reason"), !Error.IsEmpty());
	TestEqual(TEXT("failure retains output"), Out.Seed, int64(-123));
	Source.Finite.CellsX = 4;
	Source.Finite.MinZCm = MIN_int64;
	Source.Finite.MaxZCm = MAX_int64;
	TestFalse(TEXT("unrepresentable vertical range rejected"),
		FVoxelLandFiniteSnapshotAdapter::TryConvert(Source, Out, Error));
	TestEqual(TEXT("range failure retains output"), Out.Domain.CellsX, 4);
	return true;
}
#endif
