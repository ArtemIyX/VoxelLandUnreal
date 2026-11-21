#if WITH_DEV_AUTOMATION_TESTS

#include "Generation/VoxelLandM4Domain.h"
#include "Misc/AutomationTest.h"
#include <limits>

namespace
{
constexpr EAutomationTestFlags M4DomainTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM4DomainSettingsTest, "VoxelLand.M4.Domain.Settings", M4DomainTestFlags)
bool FVoxelLandM4DomainSettingsTest::RunTest(const FString& Parameters)
{
	FVoxelLandM4DomainSettings Settings;
	TestFalse(TEXT("default finite policy is valid"), bool(VoxelLandM4Domain::Validate(Settings)));
	Settings.Mode = EVoxelLandM4DomainMode::Unlimited;
	TestTrue(TEXT("unlimited global erosion is rejected"), bool(VoxelLandM4Domain::Validate(Settings)));
	Settings.bGlobalErosion = false;
	TestFalse(TEXT("unlimited without erosion is valid"), bool(VoxelLandM4Domain::Validate(Settings)));
	Settings.Mode = EVoxelLandM4DomainMode::Finite;
	Settings.CellsX = 1025;
	TestTrue(TEXT("cell cap enforced"), bool(VoxelLandM4Domain::Validate(Settings)));
	Settings.CellsX = 256;
	Settings.GridSpacingCm = 0;
	TestTrue(TEXT("zero spacing rejected"), bool(VoxelLandM4Domain::Validate(Settings)));
	Settings.GridSpacingCm = 6400;
	Settings.OriginXCm = std::numeric_limits<int64>::max();
	TestTrue(TEXT("endpoint overflow rejected"), bool(VoxelLandM4Domain::Validate(Settings)));
	Settings.Mode = static_cast<EVoxelLandM4DomainMode>(255);
	TestTrue(TEXT("unknown domain mode rejected"), bool(VoxelLandM4Domain::Validate(Settings)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM4DomainFootprintTest, "VoxelLand.M4.Domain.Footprint", M4DomainTestFlags)
bool FVoxelLandM4DomainFootprintTest::RunTest(const FString& Parameters)
{
	FVoxelLandM4DomainSettings Settings;
	FVoxelLandM4DomainFootprint Footprint;
	Footprint.PaddingVoxels = 0;
	Footprint.MinX = -8192;
	Footprint.MinY = -8192;
	Footprint.MaxX = 8192;
	Footprint.MaxY = 8192;
	TestFalse(TEXT("both inclusive domain endpoints accepted"),
		bool(VoxelLandM4Domain::ValidateFootprint(Settings, Footprint)));
	Footprint.PaddingVoxels = 1;
	TestTrue(TEXT("padding outside domain rejected"), bool(VoxelLandM4Domain::ValidateFootprint(Settings, Footprint)));
	Footprint.MinX = -8189;
	Footprint.MinY = -8189;
	Footprint.MaxX = 8189;
	Footprint.MaxY = 8189;
	Footprint.FilterHaloVoxels = 1;
	Footprint.SourceMarginVoxels = 1;
	TestFalse(TEXT("combined margins exactly reach endpoints"),
		bool(VoxelLandM4Domain::ValidateFootprint(Settings, Footprint)));
	++Footprint.SourceMarginVoxels;
	TestTrue(TEXT("source margin crossing boundary rejected"),
		bool(VoxelLandM4Domain::ValidateFootprint(Settings, Footprint)));
	Settings.Mode = EVoxelLandM4DomainMode::Unlimited;
	Settings.bGlobalErosion = false;
	TestFalse(TEXT("unlimited does not clip finite boundary"),
		bool(VoxelLandM4Domain::ValidateFootprint(Settings, Footprint)));
	Footprint.MaxX = std::numeric_limits<int64>::max();
	TestTrue(TEXT("margin overflow rejected even for unlimited mode"),
		bool(VoxelLandM4Domain::ValidateFootprint(Settings, Footprint)));
	Footprint.MaxX = 8189;
	Footprint.VoxelSizeCm = std::numeric_limits<int64>::max();
	TestTrue(TEXT("coordinate multiplication overflow rejected"),
		bool(VoxelLandM4Domain::ValidateFootprint(Settings, Footprint)));
	Footprint.VoxelSizeCm = 100;
	Footprint.PaddingVoxels = -1;
	TestTrue(TEXT("negative margin rejected"), bool(VoxelLandM4Domain::ValidateFootprint(Settings, Footprint)));
	return true;
}

#endif
