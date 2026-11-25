#if WITH_DEV_AUTOMATION_TESTS
#include "../../../Graph/Preview/VoxelLandTerrainInspectionReport.h"
#include "Data/Terrain/Worlds/VoxelLandTerrainWorldSnapshot.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM4InspectionReportTest, "VoxelLand.M4.Editor.InspectionDescriptor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVoxelLandM4InspectionReportTest::RunTest(const FString& Parameters)
{
	FVoxelLandM4WorldSnapshot Base;
	const FString Original = VoxelLandM4Inspection::DescribeSnapshot(Base);
	auto Check = [this, &Base, &Original](const TCHAR* Label, TFunctionRef<void(FVoxelLandM4WorldSnapshot&)> Change) {
		FVoxelLandM4WorldSnapshot Edited = Base;
		Change(Edited);
		TestNotEqual(Label, VoxelLandM4Inspection::DescribeSnapshot(Edited), Original);
	};
	Check(TEXT("format"), [](auto& S) { ++S.FormatVersion; });
	Check(TEXT("seed"), [](auto& S) { ++S.Seed; });
	Check(TEXT("source voxel"), [](auto& S) { ++S.VoxelSizeCm; });
	Check(TEXT("base height"), [](auto& S) { ++S.BaseHeightQ8; });
	Check(TEXT("domain mode"), [](auto& S) { S.Domain.Mode = EVoxelLandM4DomainMode::Unlimited; });
	Check(TEXT("erosion switch"), [](auto& S) { S.Domain.bGlobalErosion = !S.Domain.bGlobalErosion; });
	Check(TEXT("origin x"), [](auto& S) { ++S.Domain.OriginXCm; });
	Check(TEXT("origin y"), [](auto& S) { ++S.Domain.OriginYCm; });
	Check(TEXT("cells x"), [](auto& S) { ++S.Domain.CellsX; });
	Check(TEXT("cells y"), [](auto& S) { ++S.Domain.CellsY; });
	Check(TEXT("spacing"), [](auto& S) { ++S.Domain.GridSpacingCm; });
	Check(TEXT("slope"), [](auto& S) { S.bNormalizeSlope = !S.bNormalizeSlope; });
	Check(TEXT("smooth version"), [](auto& S) { ++S.Smooth.Version; });
	Check(TEXT("smooth switch"), [](auto& S) { S.Smooth.bEnabled = !S.Smooth.bEnabled; });
	Check(TEXT("smooth strength"), [](auto& S) { S.Smooth.Strength = 0.125f; });
	Check(TEXT("erosion stage"), [](auto& S) { S.ErosionStageId = FGuid(1, 2, 3, 4); });
	Check(TEXT("erosion salt"), [](auto& S) { ++S.ErosionSalt; });
	Check(TEXT("droplets"), [](auto& S) { ++S.DropletCount; });
	Check(TEXT("steps"), [](auto& S) { ++S.Erosion.MaxSteps; });
	Check(TEXT("thermal"), [](auto& S) { ++S.Erosion.ThermalIterations; });
	Check(TEXT("capacity"), [](auto& S) { ++S.Erosion.CapacityMaxQ8; });
	Check(TEXT("talus"), [](auto& S) { ++S.Erosion.TalusQ8; });
	Check(TEXT("transfer"), [](auto& S) { ++S.Erosion.MaxTransferQ8; });
	Check(TEXT("detail version"), [](auto& S) { ++S.HeightDetail.AlgorithmVersion; });
	Check(TEXT("detail switch"), [](auto& S) { S.HeightDetail.bEnabled = !S.HeightDetail.bEnabled; });
	Check(TEXT("detail amplitude"), [](auto& S) { ++S.HeightDetail.AmplitudeQ8; });
	Check(TEXT("detail frequency"), [](auto& S) { S.HeightDetail.FrequencyPerCm = 0.25f; });
	Check(TEXT("detail node"), [](auto& S) { S.HeightDetail.NodeId = FGuid(1, 2, 3, 4); });
	Check(TEXT("detail salt"), [](auto& S) { ++S.HeightDetail.Salt; });
	Check(TEXT("overhang switch"), [](auto& S) { S.bOverhangEnabled = !S.bOverhangEnabled; });
	Check(TEXT("overhang amplitude"), [](auto& S) { ++S.OverhangAmplitudeQ8; });
	Check(TEXT("overhang band"), [](auto& S) { ++S.OverhangBandQ8; });
	Check(TEXT("overhang frequency"), [](auto& S) { S.OverhangFrequencyPerCm = 0.25f; });
	Check(TEXT("overhang node"), [](auto& S) { S.OverhangNodeId = FGuid(1, 2, 3, 4); });
	Check(TEXT("overhang salt"), [](auto& S) { ++S.OverhangSalt; });
	Check(TEXT("horizontal radius"), [](auto& S) { ++S.HorizontalRadius; });
	Check(TEXT("vertical radius"), [](auto& S) { ++S.VerticalRadius; });
	Check(TEXT("material"), [](auto& S) { S.MaterialSetupPath = FSoftObjectPath(TEXT("/Game/M4/Material.Material")); });
	return true;
}
#endif
