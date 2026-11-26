#if WITH_DEV_AUTOMATION_TESTS
#include "Data/Assets/Graphs/VoxelLandTerrainGraph.h"
#include "Data/Graph/Schema/VoxelLandBuiltinNode.h"
#include "Data/Graph/Sources/VoxelLandGraphOutputs.h"
#include "Data/Meshing/VoxelLandMaterialAppearance.h"
#include "Data/Meshing/VoxelLandMaterialSignalSource.h"
#include "Data/Meshing/VoxelLandWorldSignalSnapshot.h"
#include "Data/Terrain/Worlds/VoxelLandWorldConfig.h"
#include "Graph/Compilation/VoxelLandMaterialGraphPoint.h"
#include "Graph/Compilation/VoxelLandMaterialGraphProgram.h"
#include "Rendering/Materials/VoxelLandTerrainPresentationResolver.h"
#include "Async/Async.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "RHI.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandMaterialWorldSignalsTest, "VoxelLand.Materials.WorldSignals",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelLandMaterialWorldSignalsTest::RunTest(const FString& Parameters)
{
	FVoxelLandWorldSignalGrid Blend;
	Blend.Name = TEXT("Moisture");
	Blend.OriginCm = FVector2d(-200.0, -100.0);
	Blend.SpacingCm = 100.0;
	Blend.Size = FIntPoint(2, 2);
	Blend.Samples = {0.0, 0.0, 0.0, 1.0};
	FVoxelLandWorldSignalGrid Biome = Blend;
	Biome.Name = TEXT("Biome");
	Biome.Type = EVoxelLandGraphValueType::Int32;
	Biome.Samples = {1.0, 3.0, 5.0, 7.0};

	FString Error;
	FVoxelLandWorldSignalSnapshot::FPtr Snapshot;
	if (!TestTrue(TEXT("Build immutable biome map"),
			FVoxelLandWorldSignalSnapshot::Create({Blend, Biome}, 1, Snapshot, Error)))
	{
		AddError(Error);
		return false;
	}
	const FVector2d Midpoint(-150.0, -50.0);
	double Value = 0.0;
	TestTrue(TEXT("Sample continuous biome weight"), Snapshot->Sample(Blend.Name, Midpoint, Value));
	TestEqual(TEXT("Bilinear interpolation blends the four corners"), Value, 0.25);
	TestTrue(TEXT("Sample categorical biome ID"), Snapshot->Sample(Biome.Name, Midpoint, Value));
	TestEqual(TEXT("Nearest categorical sample resolves half-cell ties consistently"), Value, 7.0);
	TestFalse(TEXT("A position outside coverage uses the caller default"),
		Snapshot->Sample(Biome.Name, FVector2d(-200.01, -50.0), Value));
	TestEqual(TEXT("Outside coverage preserves caller output"), Value, 7.0);
	const FVoxelLandWorldSignalSnapshot::FPtr Original = Snapshot;
	TestFalse(TEXT("Reject duplicate channel names"),
		FVoxelLandWorldSignalSnapshot::Create({Blend, Blend}, 2, Snapshot, Error));
	TestTrue(TEXT("A failed replacement preserves the prior snapshot"), Snapshot == Original);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandMaterialTerrainSignalsTest, "VoxelLand.Materials.TerrainSignals",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelLandMaterialTerrainSignalsTest::RunTest(const FString& Parameters)
{
	const UVoxelLandWorldConfig* Example = LoadObject<UVoxelLandWorldConfig>(
		nullptr, TEXT("/VoxelLand/Examples/TerrainMaterials/WC_GrassPatches.WC_GrassPatches"));
	if (!TestNotNull(TEXT("Grass Patches example"), Example))
	{
		return false;
	}
	TStrongObjectPtr<UVoxelLandWorldConfig> Config(DuplicateObject(Example, GetTransientPackage()));
	TStrongObjectPtr<UVoxelLandTerrainGraph> Terrain(DuplicateObject(Example->Graph.Get(), GetTransientPackage()));
	TStrongObjectPtr<UVoxelLandTerrainGraph> Material(
		DuplicateObject(Example->MaterialGraph.LoadSynchronous(), GetTransientPackage()));
	Config->Graph = Terrain.Get();
	Config->MaterialGraph = Material.Get();
	FVoxelLandGraphNodeRecord* Noise = Material->Nodes.FindByPredicate([](const FVoxelLandGraphNodeRecord& Node) {
		return Node.OperatorId == VoxelLandBuiltinNodeId(EVoxelLandBuiltinNode::WorldNoise2D);
	});
	if (!TestNotNull(TEXT("Starter noise node"), Noise))
	{
		return false;
	}
	const FVoxelLandGraphPinRecord Output = Noise->Pins.Last();
	Noise->OperatorId = VoxelLandBuiltinNodeId(EVoxelLandBuiltinNode::TerrainSignal);
	Noise->Pins = {Output};
	FVoxelLandMaterialGraphProgram::WriteGuid(Config->TerrainOutputId, Noise->Configuration);
	const FVoxelLandTerrainPresentation First =
		FVoxelLandTerrainPresentationResolver::Resolve(GetTransientPackage(), *Config);
	if (!TestTrue(*First.Diagnostic, First.Appearance.IsValid() && First.Appearance->Signals.IsValid()))
	{
		return false;
	}
	const FGuid SampleId = Config->TerrainOutputId;
	const bool bSampled = Async(EAsyncExecution::ThreadPool, [Appearance = First.Appearance, SampleId] {
		double Value = 0.0;
		return Appearance->Signals->SampleTerrain(SampleId, FVector3d(-100.0, 700.0, 999.0), Value);
	}).Get();
	TestTrue(TEXT("Imported Height sample is available"), bSampled);

	FVoxelLandGraphNodeRecord* Named = Terrain->Nodes.FindByPredicate(
		[&Config](const FVoxelLandGraphNodeRecord& Node) { return Node.Id == Config->TerrainOutputId; });
	if (!TestNotNull(TEXT("Terrain output bound by ID"), Named))
	{
		return false;
	}
	FVoxelLandGraphOutputs::SetName(*Named, TEXT("Renamed terrain shape"));
	const FVoxelLandTerrainPresentation Renamed =
		FVoxelLandTerrainPresentationResolver::Resolve(GetTransientPackage(), *Config);
	TestTrue(*Renamed.Diagnostic, Renamed.Appearance.IsValid());
	Named->OperatorId = VoxelLandBuiltinNodeId(EVoxelLandBuiltinNode::BoolOutput);
	const FVoxelLandTerrainPresentation Retyped =
		FVoxelLandTerrainPresentationResolver::Resolve(GetTransientPackage(), *Config);
	TestFalse(TEXT("Retyping an imported Float output invalidates the binding"), Retyped.Appearance.IsValid());
	TestTrue(TEXT("Import diagnostic names the terrain graph"), Retyped.Diagnostic.Contains(Terrain->GetPathName()));
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandProceduralMaterialsCookedTest, "VoxelLand.Materials.ProceduralCooked",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelLandProceduralMaterialsCookedTest::RunTest(const FString& Parameters)
{
	FString Root;
	if (!FParse::Value(FCommandLine::Get(), TEXT("VoxelLandCookedProceduralRoot="), Root))
	{
		return true;
	}
	if (!TestTrue(TEXT("Cooked data verification runs with NullRHI"), GUsingNullRHI))
	{
		return false;
	}
	const FString CookedPlugin = Root / TEXT("Plugins/VoxelLand/Content/");
	if (!TestTrue(TEXT("Cooked VoxelLand content directory exists"), FPaths::DirectoryExists(CookedPlugin)))
	{
		return false;
	}
	FPackageName::RegisterMountPoint(TEXT("/VoxelLand/"), CookedPlugin);
	ON_SCOPE_EXIT { FPackageName::UnRegisterMountPoint(TEXT("/VoxelLand/"), CookedPlugin); };
	const TGuardValue<int32> AllowCooked(GAllowCookedDataInEditorBuilds, 1);
	const TCHAR* Paths[] = {
		TEXT("/VoxelLand/Examples/TerrainMaterials/WC_GrassPatches.WC_GrassPatches"),
		TEXT("/VoxelLand/Examples/TerrainMaterials/WC_MountainGrassPatches.WC_MountainGrassPatches")};
	for (const TCHAR* Path : Paths)
	{
		const UVoxelLandWorldConfig* Config = LoadObject<UVoxelLandWorldConfig>(nullptr, Path);
		if (!TestNotNull(TEXT("Load the cooked material example"), Config))
		{
			return false;
		}
		TestTrue(TEXT("Verify the example is the cooked package"), Config->GetOutermost()->HasAnyPackageFlags(PKG_Cooked));
		const UVoxelLandTerrainGraph* Graph = Config->MaterialGraph.LoadSynchronous();
		if (!TestNotNull(TEXT("Load the cooked procedural material graph"), Graph))
		{
			return false;
		}
		TestTrue(TEXT("Graph role and source records survive cooking"),
			Graph->GetOutermost()->HasAnyPackageFlags(PKG_Cooked) && Graph->GraphRole == EVoxelLandGraphRole::Material
				&& !Graph->Nodes.IsEmpty() && !Graph->EditorGraph);
		const FVoxelLandTerrainPresentation Presentation =
			FVoxelLandTerrainPresentationResolver::Resolve(GetTransientPackage(), *Config);
		TestTrue(*Presentation.Diagnostic, Presentation.Appearance.IsValid() && Presentation.Appearance->Program.IsValid());
	}
	return true;
}
#endif
