#if WITH_DEV_AUTOMATION_TESTS

#include "Data/Terrain/Worlds/VoxelLandWorldConfig.h"
#include "Data/Assets/Materials/VoxelLandMaterialSetup.h"
#include "Data/Assets/Graphs/VoxelLandTerrainGraph.h"
#include "Data/Terrain/Worlds/VoxelLandTerrainWorldSnapshot.h"
#include "Misc/AutomationTest.h"
#include <limits>

namespace
{
constexpr EAutomationTestFlags M4AssetTestsFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

void M4AssetSetValidCurve(FVoxelLandM4ControlConfig& Control)
{
	FVoxelLandM4CurveAuthoringKey First;
	First.InputQ16 = 0;
	First.OutputQ8 = -256;
	FVoxelLandM4CurveAuthoringKey Last;
	Last.InputQ16 = 65535;
	Last.OutputQ8 = 256;
	Control.CurveKeys = {First, Last};
}

UVoxelLandWorldConfig* M4AssetMakeValid()
{
	UVoxelLandWorldConfig* Config = NewObject<UVoxelLandWorldConfig>();
	Config->Continental.Graph = NewObject<UVoxelLandTerrainGraph>(Config);
	Config->Mountain.Graph = NewObject<UVoxelLandTerrainGraph>(Config);
	Config->Peaks.Graph = NewObject<UVoxelLandTerrainGraph>(Config);
	M4AssetSetValidCurve(Config->Continental);
	M4AssetSetValidCurve(Config->Mountain);
	M4AssetSetValidCurve(Config->Peaks);
	return Config;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM4AssetSnapshotTest, "VoxelLand.M4.Asset.Snapshot", M4AssetTestsFlags)
bool FVoxelLandM4AssetSnapshotTest::RunTest(const FString& Parameters)
{
	UVoxelLandWorldConfig* Config = M4AssetMakeValid();
	FVoxelLandM4WorldSnapshot Snapshot;
	FString Error;
	if (!TestTrue(TEXT("valid editable settings snapshot"), Config->CaptureSnapshot(Snapshot, Error)))
	{
		AddError(Error);
		return false;
	}
	TestTrue(TEXT("snapshot matches untouched authored values"), Config->MatchesSnapshot(Snapshot));
	TestEqual(TEXT("default X origin"), Snapshot.Domain.OriginXCm, int64(-819200));
	TestEqual(TEXT("control key copied"), Snapshot.Mountain.CurveKeys[1].OutputQ8, 256);
	Config->MaterialSetup = NewObject<UVoxelLandMaterialSetup>(Config);
	TestTrue(TEXT("material-only edit does not invalidate density inputs"), Config->MatchesSnapshot(Snapshot));
	Config->Mountain.CurveKeys[1].OutputQ8 = 512;
	TestFalse(TEXT("curve change invalidates request"), Config->MatchesSnapshot(Snapshot));
	TestEqual(TEXT("snapshot owns prior curve"), Snapshot.Mountain.CurveKeys[1].OutputQ8, 256);
	Config->Mountain.CurveKeys[1].OutputQ8 = 256;
	Config->Seed += 1;
	TestFalse(TEXT("seed change invalidates request"), Config->MatchesSnapshot(Snapshot));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM4AssetValidationTest, "VoxelLand.M4.Asset.Validation", M4AssetTestsFlags)
bool FVoxelLandM4AssetValidationTest::RunTest(const FString& Parameters)
{
	UVoxelLandWorldConfig* Config = M4AssetMakeValid();
	FVoxelLandM4WorldSnapshot Snapshot;
	Snapshot.Seed = 999;
	FString Error;
	Config->Continental.CurveKeys[1].InputQ16 = 0;
	TestFalse(TEXT("duplicate curve input rejected"), Config->CaptureSnapshot(Snapshot, Error));
	TestEqual(TEXT("failure leaves output unchanged"), Snapshot.Seed, int64(999));
	M4AssetSetValidCurve(Config->Continental);
	Config->GridSpacingCm = 134218;
	TestFalse(TEXT("unrepresentable exact interpolation rejected"), Config->ValidateSettings(Error));
	Config->GridSpacingCm = 6400;
	Config->OriginXCm = std::numeric_limits<int64>::min();
	TestFalse(TEXT("unsupported exact VM coordinate rejected"), Config->ValidateSettings(Error));
	Config->OriginXCm = -819200;
	Config->OverhangFrequencyPerCm = std::numeric_limits<float>::infinity();
	TestFalse(TEXT("nonfinite overhang frequency rejected"), Config->ValidateSettings(Error));
	Config->OverhangFrequencyPerCm = 0.001f;
	Config->Continental.Graph = nullptr;
	TestFalse(TEXT("missing graph rejected"), Config->ValidateSettings(Error));
	return true;
}

#endif

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM4AssetCaptureLimitsTest, "VoxelLand.M4.Asset.CaptureLimits", M4AssetTestsFlags)
bool FVoxelLandM4AssetCaptureLimitsTest::RunTest(const FString& Parameters)
{
	UVoxelLandWorldConfig* Config = M4AssetMakeValid();
	FVoxelLandM4WorldSnapshot Snapshot;
	Snapshot.Seed = 999;
	FString Error;
	Config->Peaks.Graph->Nodes.SetNum(129);
	TestFalse(TEXT("oversized later graph rejected before capture"), Config->CaptureSnapshot(Snapshot, Error));
	TestEqual(TEXT("rejection leaves destination unchanged"), Snapshot.Seed, int64(999));
	TestTrue(TEXT("oversized graph reports capture limit"), Error.Contains(TEXT("capture limits")));
	Config->Peaks.Graph->Nodes.Reset();
	Config->Mountain.Graph->CompiledProgram.Constants.SetNumZeroed(4097);
	TestFalse(TEXT("oversized cooked constants rejected"), Config->CaptureSnapshot(Snapshot, Error));
	Config->Mountain.Graph->CompiledProgram.Constants.Reset();
	FVoxelLandGraphNodeRecord Node;
	Node.FunctionPath = FSoftObjectPath(TEXT("/Game/Unavailable/Control.Control"));
	Config->Continental.Graph->Nodes.Add(Node);
	TestFalse(TEXT("function path rejected before any load"), Config->CaptureSnapshot(Snapshot, Error));
	TestTrue(TEXT("function rejection explains unsupported feature"), Error.Contains(TEXT("function")));
	Config->Continental.Graph->Nodes.Reset();
	FVoxelLandGraphParameter Parameter;
	Parameter.Name = FString::ChrN(129, TEXT('x'));
	Config->Peaks.Graph->Parameters.Add(Parameter);
	TestFalse(TEXT("oversized parameter name rejected before copy"), Config->CaptureSnapshot(Snapshot, Error));
	TestEqual(TEXT("all failures preserve previous snapshot"), Snapshot.Seed, int64(999));
	return true;
}
#endif

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM4AssetOverrideTest, "VoxelLand.M4.Asset.Overrides", M4AssetTestsFlags)
bool FVoxelLandM4AssetOverrideTest::RunTest(const FString& Parameters)
{
	UVoxelLandWorldConfig* Config = M4AssetMakeValid();
	FVoxelLandGraphParameter Parameter;
	Parameter.Id = FGuid(1, 2, 3, 4);
	Parameter.Name = TEXT("Height");
	Parameter.Type = EVoxelLandGraphValueType::Float;
	Parameter.FloatDefault = 0.0f;
	Parameter.bHasRange = true;
	Parameter.FloatMin = -1.0f;
	Parameter.FloatMax = 1.0f;
	Parameter.Tooltip = FText::FromString(TEXT("Authoring-only tooltip"));
	Config->Continental.Graph->Parameters.Add(Parameter);
	FVoxelLandParameterOverride Override;
	Override.ParameterId = Parameter.Id;
	Override.Type = EVoxelLandGraphValueType::Float;
	Override.FloatValue = 0.25f;
	Config->Continental.Overrides.Add(Override);
	FVoxelLandM4WorldSnapshot Snapshot;
	FString Error;
	if (!TestTrue(TEXT("typed override accepted"), Config->CaptureSnapshot(Snapshot, Error)))
	{
		AddError(Error);
		return false;
	}
	TestEqual(TEXT("override value copied"), Snapshot.Continental.Overrides[0].FloatValue, 0.25f);
	TestTrue(
		TEXT("worker snapshot excludes editor tooltip"), Snapshot.Continental.Source.Parameters[0].Tooltip.IsEmpty());
	TestFalse(TEXT("source keeps editor tooltip"), Config->Continental.Graph->Parameters[0].Tooltip.IsEmpty());
	Config->Continental.Overrides[0].IntValue = 42;
	TestTrue(TEXT("inactive typed value does not invalidate snapshot"), Config->MatchesSnapshot(Snapshot));
	Config->Continental.Overrides[0].FloatValue = 0.5f;
	TestFalse(TEXT("override change invalidates snapshot"), Config->MatchesSnapshot(Snapshot));
	Config->Continental.Overrides[0].FloatValue = 2.0f;
	TestFalse(TEXT("range violation rejected"), Config->ValidateSettings(Error));
	Config->Continental.Overrides[0].FloatValue = 0.5f;
	const FVoxelLandParameterOverride DuplicateOverride = Config->Continental.Overrides[0];
	Config->Continental.Overrides.Add(DuplicateOverride);
	TestFalse(TEXT("duplicate override rejected"), Config->ValidateSettings(Error));
	return true;
}
#endif
