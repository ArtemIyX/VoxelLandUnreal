#if WITH_DEV_AUTOMATION_TESTS
#include "Data/Terrain/Persistence/VoxelLandSavedTerrainWorldRecord.h"
#include "Misc/AutomationTest.h"
#include "World/Terrain/Persistence/VoxelLandSavedTerrainWorldPreflight.h"
#include "World/Terrain/Persistence/VoxelLandWorldIdentity.h"
namespace
{
constexpr EAutomationTestFlags M4SavedWorldPreflightFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM4SavedWorldPreflightTest, "VoxelLand.M4.SavedWorld.Admission", M4SavedWorldPreflightFlags)
bool FVoxelLandM4SavedWorldPreflightTest::RunTest(const FString& Parameters)
{
	FVoxelLandM4SavedWorld Saved;
	Saved.Identity.CanonicalBytes.SetNumZeroed(8);
	FString Error;
	TestFalse(TEXT("legacy save is rejected before scheduling worker validation"),
		VoxelLandM4SavedWorldPreflight::Validate(Saved, Error));
	TestTrue(TEXT("legacy save rejection explains reauthoring"), Error.Contains(TEXT("reauthoring")));
	Saved.FormatVersion = FVoxelLandM4SavedWorld::AuthoredFormatVersion;
	Saved.Identity.FormatVersion = VoxelLandM4WorldIdentity::AuthoredHeightVersion;
	Saved.Snapshot.AuthoredHeightOutputId = FGuid(1, 2, 3, 4);
	Saved.Snapshot.AuthoredHeightSource.Nodes.AddDefaulted(); // Semantic validation belongs to the worker.
	TestTrue(TEXT("bounded authored metadata admitted for worker validation"),
		VoxelLandM4SavedWorldPreflight::Validate(Saved, Error));
	Saved.Snapshot.HorizontalRadius = 9;
	TestFalse(TEXT("snapshot-only check rejects excessive demand"),
		VoxelLandM4SavedWorldPreflight::ValidateSnapshot(Saved.Snapshot, Error));
	TestFalse(TEXT("excessive horizontal demand rejected"), VoxelLandM4SavedWorldPreflight::Validate(Saved, Error));
	Saved.Snapshot.HorizontalRadius = 1;
	Saved.Snapshot.AuthoredHeightSource.Functions.SetNum(17);
	TestFalse(TEXT("too many graph dependencies rejected"), VoxelLandM4SavedWorldPreflight::Validate(Saved, Error));
	Saved.Snapshot.AuthoredHeightSource.Functions.SetNum(1);
	Saved.Snapshot.AuthoredHeightSource.Functions[0].Nodes.SetNum(129);
	TestFalse(TEXT("nested dependency node limit enforced"), VoxelLandM4SavedWorldPreflight::Validate(Saved, Error));
	Saved.Snapshot.AuthoredHeightSource.Functions.Empty();
	Saved.Snapshot.Mountain.Payload.ExternalOperators.SetNum(1);
	TestFalse(TEXT("unsupported external provider rejected"), VoxelLandM4SavedWorldPreflight::Validate(Saved, Error));
	Saved.Snapshot.Mountain.Payload.ExternalOperators.Empty();
	Saved.Snapshot.Peaks.CookedCurve.SamplesQ8.SetNum(4098);
	TestFalse(TEXT("oversized legacy curve payload rejected"), VoxelLandM4SavedWorldPreflight::Validate(Saved, Error));
	return true;
}
#endif
