#if WITH_DEV_AUTOMATION_TESTS

#include "Data/Assets/Graphs/VoxelLandTerrainGraph.h"
#include "Data/Assets/Materials/VoxelLandMaterialPalette.h"
#include "Data/Assets/Materials/VoxelLandMaterialSetup.h"
#include "Data/Assets/Materials/VoxelLandSurfaceMaterial.h"
#include "Data/Graph/Schema/VoxelLandBuiltinCatalog.h"
#include "Data/Graph/Schema/VoxelLandBuiltinNodeSpec.h"
#include "Data/Terrain/Worlds/VoxelLandWorldConfig.h"
#include "Graph/Compilation/VoxelLandMaterialGraphPoint.h"
#include "Graph/Compilation/VoxelLandMaterialGraphProgram.h"
#include "Materials/VoxelLandMaterialPaletteBaker.h"
#include "Rendering/Materials/VoxelLandTerrainPresentationResolver.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "UObject/SavePackage.h"
#include "UObject/StrongObjectPtr.h"

class FVoxelLandGrassPatchesGraphBuilder final
{
public:
	static FVoxelLandGraphSnapshot Make(TConstArrayView<FGuid> SurfaceIds, bool bNoise,
		TConstArrayView<FSoftObjectPath> SurfaceAssets = {})
	{
		check(SurfaceIds.Num() == SurfaceCount);
		FVoxelLandGrassPatchesGraphBuilder Builder;
		Builder.Graph.GraphRole = EVoxelLandGraphRole::Material;
		Builder.AddSurface(SurfaceIds[MeadowSlot], SurfaceAssets.IsValidIndex(MeadowSlot) ? SurfaceAssets[MeadowSlot] : FSoftObjectPath());
		Builder.AddSurface(SurfaceIds[DryGrassSlot], SurfaceAssets.IsValidIndex(DryGrassSlot) ? SurfaceAssets[DryGrassSlot] : FSoftObjectPath());
		Builder.AddSurface(SurfaceIds[SlateSlot], SurfaceAssets.IsValidIndex(SlateSlot) ? SurfaceAssets[SlateSlot] : FSoftObjectPath());
		const int32 Grass = Builder.Add(EVoxelLandBuiltinNode::BlendSurfaces);
		Builder.Connect(MeadowSlot, Grass, TEXT("A"));
		Builder.Connect(DryGrassSlot, Grass, TEXT("B"));
		Builder.SetDefault(Grass, TEXT("Alpha"), ReferenceAlpha);
		if (bNoise)
		{
			const int32 Noise = Builder.Add(EVoxelLandBuiltinNode::WorldNoise2D);
			Builder.SetDefault(Noise, TEXT("Frequency"), 1.f / PatchSizeCm);
			const int32 Remap = Builder.Add(EVoxelLandBuiltinNode::MaterialRemap);
			Builder.SetDefault(Remap, TEXT("Lower"), -GrassTransition);
			Builder.SetDefault(Remap, TEXT("Upper"), GrassTransition);
			Builder.Connect(Noise, Remap, TEXT("Value"));
			Builder.Connect(Remap, Grass, TEXT("Alpha"));
		}
		const int32 Slope = Builder.Add(EVoxelLandBuiltinNode::SurfaceSlope);
		const int32 RockMask = Builder.Add(EVoxelLandBuiltinNode::MaterialRemap);
		Builder.SetDefault(RockMask, TEXT("Lower"), RockStartsDegrees);
		Builder.SetDefault(RockMask, TEXT("Upper"), AllRockDegrees);
		Builder.Connect(Slope, RockMask, TEXT("Value"));
		const int32 Blend = Builder.Add(EVoxelLandBuiltinNode::BlendSurfaces);
		Builder.Connect(Grass, Blend, TEXT("A"));
		Builder.Connect(SlateSlot, Blend, TEXT("B"));
		Builder.Connect(RockMask, Blend, TEXT("Alpha"));
		const int32 Output = Builder.Add(EVoxelLandBuiltinNode::SurfaceMaterialOutput);
		Builder.Connect(Blend, Output, TEXT("Surface"));
		return MoveTemp(Builder.Graph);
	}

	static constexpr int32 MeadowSlot = 0;
	static constexpr int32 DryGrassSlot = 1;
	static constexpr int32 SlateSlot = 2;
	static constexpr int32 SurfaceCount = 3;
	static constexpr float ReferenceAlpha = 0.25f;
	static constexpr float RockStartsDegrees = 30.f;
	static constexpr float AllRockDegrees = 55.f;

private:
	static constexpr float PatchSizeCm = 1600.f;
	static constexpr float GrassTransition = 0.18f;

	FGuid MakeId()
	{
		return FGuid(0x47524153, 0x53504154, 0x43484553, ++NextId);
	}

	int32 Add(EVoxelLandBuiltinNode Kind)
	{
		FVoxelLandBuiltinNodeSpec Spec;
		verify(FVoxelLandBuiltinCatalog::Find(Kind, Spec));
		const int32 Index = Graph.Nodes.AddDefaulted();
		FVoxelLandGraphNodeRecord& Node = Graph.Nodes[Index];
		Node.Id = MakeId();
		Node.OperatorId = Spec.OperatorId;
		Node.OperatorVersion = Spec.OperatorVersion;
		Node.Pins.Reserve(Spec.Pins.Num());
		for (const FVoxelLandBuiltinPinSpec& Source : Spec.Pins)
		{
			FVoxelLandGraphPinRecord& Pin = Node.Pins.AddDefaulted_GetRef();
			Pin.Id = MakeId();
			Pin.Name = Source.Name;
			Pin.Type = Source.Type;
			Pin.bOutput = Source.bOutput;
			Pin.FloatDefault = FCString::Atof(*Source.DefaultLiteral);
			Pin.IntDefault = FCString::Atoi(*Source.DefaultLiteral);
		}
		return Index;
	}

	void AddSurface(FGuid Id, const FSoftObjectPath& DisplayAsset)
	{
		FVoxelLandGraphNodeRecord& Surface = Graph.Nodes[Add(EVoxelLandBuiltinNode::Surface)];
		FVoxelLandMaterialGraphProgram::WriteGuid(Id, Surface.Configuration);
		Surface.SurfaceAsset = DisplayAsset;
	}

	static FVoxelLandGraphPinRecord& FindInput(FVoxelLandGraphNodeRecord& Node, const TCHAR* Name)
	{
		for (FVoxelLandGraphPinRecord& Pin : Node.Pins)
		{
			if (!Pin.bOutput && Pin.Name == Name)
			{
				return Pin;
			}
		}
		checkNoEntry();
		return Node.Pins[0];
	}

	void SetDefault(int32 Node, const TCHAR* Input, float Value)
	{
		FindInput(Graph.Nodes[Node], Input).FloatDefault = Value;
	}

	void Connect(int32 From, int32 To, const TCHAR* Input)
	{
		check(Graph.Nodes[From].Pins.Last().bOutput);
		FVoxelLandGraphLinkRecord& Link = Graph.Links.AddDefaulted_GetRef();
		Link.SourceNode = Graph.Nodes[From].Id;
		Link.SourcePin = Graph.Nodes[From].Pins.Last().Id;
		Link.TargetNode = Graph.Nodes[To].Id;
		Link.TargetPin = FindInput(Graph.Nodes[To], Input).Id;
	}

	FVoxelLandGraphSnapshot Graph;
	uint32 NextId = 0;
};

class FVoxelLandGrassPatchesAssets final
{
public:
	static bool Generate(FAutomationTestBase& Test)
	{
		const TCHAR* const AssetNames[] = {DryGrassName, PaletteName, SetupName, GraphName, ConfigName};
		for (const TCHAR* Name : AssetNames)
		{
			const FString PackageName = MakePackageName(Name);
			if (FPackageName::DoesPackageExist(PackageName) || FindPackage(nullptr, *PackageName))
			{
				Test.AddError(FString::Printf(TEXT("Grass Patches package '%s' exists. Existing examples were left intact."), *PackageName));
				return false;
			}
		}

		UVoxelLandSurfaceMaterial* Meadow = LoadObject<UVoxelLandSurfaceMaterial>(
			nullptr, TEXT("/VoxelLand/Examples/TerrainMaterials/DA_Surface_Meadow.DA_Surface_Meadow"));
		UVoxelLandSurfaceMaterial* Slate = LoadObject<UVoxelLandSurfaceMaterial>(
			nullptr, TEXT("/VoxelLand/Examples/TerrainMaterials/DA_Surface_Slate.DA_Surface_Slate"));
		UVoxelLandWorldConfig* Rolling = LoadObject<UVoxelLandWorldConfig>(
			nullptr, TEXT("/VoxelLand/Examples/WC_RollingHills.WC_RollingHills"));
		if (!Meadow || !Slate || !Rolling || !Rolling->Graph)
		{
			Test.AddError(TEXT("Grass Patches requires the shipped Meadow, Slate and Rolling Hills examples."));
			return false;
		}

		TStrongObjectPtr<UVoxelLandSurfaceMaterial> DryGrass(DuplicateAsset(*Meadow, DryGrassName));
		DryGrass->Tint *= FLinearColor(0.86f, 0.62f, 0.39f);
		const FGuid SurfaceIds[] = {Meadow->SurfaceId, DryGrass->SurfaceId, Slate->SurfaceId};
		if (!DryGrass->SurfaceId.IsValid() || DryGrass->SurfaceId == Meadow->SurfaceId)
		{
			Test.AddError(TEXT("Duplicating Meadow did not produce a distinct surface ID."));
			return false;
		}
		const FSoftObjectPath SurfaceAssets[] = {FSoftObjectPath(Meadow), FSoftObjectPath(DryGrass.Get()), FSoftObjectPath(Slate)};
		FVoxelLandGraphSnapshot Source = FVoxelLandGrassPatchesGraphBuilder::Make(SurfaceIds, true, SurfaceAssets);
		TStrongObjectPtr<UVoxelLandTerrainGraph> Graph(NewAsset<UVoxelLandTerrainGraph>(GraphName));
		Graph->GraphRole = Source.GraphRole;
		Graph->Nodes = MoveTemp(Source.Nodes);
		Graph->Links = MoveTemp(Source.Links);
		if (!CompileGraph(Test, *Graph, SurfaceIds))
		{
			return false;
		}

		TStrongObjectPtr<UVoxelLandMaterialPalette> Palette(NewAsset<UVoxelLandMaterialPalette>(PaletteName));
		Palette->Surfaces = {Meadow, DryGrass.Get(), Slate};
		FText BakeError;
		if (!FVoxelLandMaterialPaletteBaker::Bake(*Palette, BakeError))
		{
			Test.AddError(BakeError.ToString());
			return false;
		}
		TStrongObjectPtr<UVoxelLandMaterialSetup> Setup(NewAsset<UVoxelLandMaterialSetup>(SetupName));
		Setup->Palette = Palette.Get();
		TStrongObjectPtr<UVoxelLandWorldConfig> Config(DuplicateAsset(*Rolling, ConfigName));
		Config->MaterialSetup = Setup.Get();
		Config->MaterialGraph = Graph.Get();
		const FVoxelLandTerrainPresentation Presentation =
			FVoxelLandTerrainPresentationResolver::Resolve(GetTransientPackage(), *Config);
		if (!Presentation.Appearance)
		{
			Test.AddError(Presentation.Diagnostic);
			return false;
		}
		return Test.TestTrue(TEXT("Save the editable grass surface, palette, setup, graph and config"),
			Save(*DryGrass) && Save(*Palette) && Save(*Setup) && Save(*Graph) && Save(*Config));
	}

private:
	static constexpr const TCHAR* ExampleRoot = TEXT("/VoxelLand/Examples/TerrainMaterials/");
	static constexpr const TCHAR* DryGrassName = TEXT("DA_Surface_DryGrass");
	static constexpr const TCHAR* PaletteName = TEXT("DA_Palette_GrassPatches");
	static constexpr const TCHAR* SetupName = TEXT("DA_MaterialSetup_GrassPatches");
	static constexpr const TCHAR* GraphName = TEXT("MG_GrassPatches");
	static constexpr const TCHAR* ConfigName = TEXT("WC_GrassPatches");
	static constexpr EObjectFlags EditableAssetFlags = RF_Public | RF_Standalone | RF_Transactional;

	static FString MakePackageName(const TCHAR* Name)
	{
		return FString(ExampleRoot) + Name;
	}

	template <typename AssetType>
	static AssetType* NewAsset(const TCHAR* Name)
	{
		return NewObject<AssetType>(CreatePackage(*MakePackageName(Name)), FName(Name), EditableAssetFlags);
	}

	template <typename AssetType>
	static AssetType* DuplicateAsset(AssetType& Source, const TCHAR* Name)
	{
		AssetType* Copy = DuplicateObject<AssetType>(&Source, CreatePackage(*MakePackageName(Name)), FName(Name));
		Copy->SetFlags(EditableAssetFlags);
		return Copy;
	}

	static bool CompileGraph(FAutomationTestBase& Test, const UVoxelLandTerrainGraph& Graph, TConstArrayView<FGuid> Palette)
	{
		FVoxelLandGraphSnapshot Source;
		FString Error;
		TSharedPtr<const FVoxelLandMaterialGraphProgram, ESPMode::ThreadSafe> Program;
		if (!Graph.CaptureSnapshot(Source, Error) || !FVoxelLandMaterialGraphProgram::Compile(Source, Palette, Program, Error))
		{
			Test.AddError(Error);
			return false;
		}
		return true;
	}

	static bool Save(UObject& Asset)
	{
		UPackage* Package = Asset.GetOutermost();
		const FString Filename =
			FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		FAssetRegistryModule::AssetCreated(&Asset);
		Package->MarkPackageDirty();
		return UPackage::SavePackage(Package, &Asset, *Filename, Args);
	}
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandCreateGrassPatches, "VoxelLand.Materials.CreateGrassPatches",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelLandCreateGrassPatches::RunTest(const FString&)
{
	if (!FParse::Param(FCommandLine::Get(), TEXT("VoxelLandCreateGrassPatches")))
	{
		AddInfo(TEXT("Supply -VoxelLandCreateGrassPatches to create the editable example."));
		return true;
	}
	return FVoxelLandGrassPatchesAssets::Generate(*this);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandMaterialGraphProgramTests, "VoxelLand.Materials.GraphProgram",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelLandMaterialGraphProgramTests::RunTest(const FString&)
{
	using FBuilder = FVoxelLandGrassPatchesGraphBuilder;
	using FProgramPtr = TSharedPtr<const FVoxelLandMaterialGraphProgram, ESPMode::ThreadSafe>;
	const FGuid Palette[] = {FGuid(9, 7, 1, 1), FGuid(9, 7, 1, 2), FGuid(9, 7, 1, 3)};
	FString Error;
	FProgramPtr Program;
	if (!TestTrue(TEXT("Compile a material graph with two blended grasses and slope rock"),
			FVoxelLandMaterialGraphProgram::Compile(FBuilder::Make(Palette, false), Palette, Program, Error)))
	{
		AddError(Error);
		return false;
	}
	FVoxelLandMaterialGraphPoint Point;
	Point.Position = FVector3d(-3200.0, 4800.0, 0.0);
	Point.Seed = 2468;
	double Weights[FBuilder::SurfaceCount] = {};
	TestTrue(TEXT("Sample flat terrain"), Program->Evaluate(Point, Weights));
	TestEqual(TEXT("Meadow receives its complement"), Weights[FBuilder::MeadowSlot], 1.0 - FBuilder::ReferenceAlpha);
	TestEqual(TEXT("Dry grass receives the authored alpha"), Weights[FBuilder::DryGrassSlot], double(FBuilder::ReferenceAlpha));
	TestEqual(TEXT("Slate stays absent from flat terrain"), Weights[FBuilder::SlateSlot], 0.0);
	Point.SlopeDegrees = 0.5 * (FBuilder::RockStartsDegrees + FBuilder::AllRockDegrees);
	TestTrue(TEXT("Sample the middle of the slope transition"), Program->Evaluate(Point, Weights));
	TestEqual(TEXT("Slate receives half the contribution at midpoint"), Weights[FBuilder::SlateSlot], 0.5);
	TestEqual(TEXT("Weight mass is preserved"), Weights[0] + Weights[1] + Weights[2], 1.0);
	Point.SlopeDegrees = 85.0;
	TestTrue(TEXT("Sample the steep slope"), Program->Evaluate(Point, Weights));
	TestEqual(TEXT("Slate replaces grass on steep terrain"), Weights[FBuilder::SlateSlot], 1.0);

	FVoxelLandGraphSnapshot NoiseSource = FBuilder::Make(Palette, true);
	FProgramPtr NoiseProgram;
	if (!TestTrue(TEXT("Compile the noise-driven example"),
			FVoxelLandMaterialGraphProgram::Compile(NoiseSource, Palette, NoiseProgram, Error)))
	{
		AddError(Error);
		return false;
	}
	Point.SlopeDegrees = 0.0;
	double First[FBuilder::SurfaceCount] = {};
	double Repeat[FBuilder::SurfaceCount] = {};
	TestTrue(TEXT("Sample negative logical coordinates"), NoiseProgram->Evaluate(Point, First));
	const FVector3d FirstPosition = Point.Position;
	bool bDifferentPatch = false;
	constexpr int32 SampleCount = 16;
	for (int32 Index = 0; Index < SampleCount; ++Index)
	{
		Point.Position.X += 997.0;
		TestTrue(TEXT("Sample a second logical point"), NoiseProgram->Evaluate(Point, Repeat));
		bDifferentPatch |= !FMath::IsNearlyEqual(First[FBuilder::DryGrassSlot], Repeat[FBuilder::DryGrassSlot]);
	}
	TestTrue(TEXT("The noise creates more than one patch weight"), bDifferentPatch);
	Point.Position = FirstPosition;
	TestTrue(TEXT("Resample the first point in a different order"), NoiseProgram->Evaluate(Point, Repeat));
	for (int32 Slot = 0; Slot < FBuilder::SurfaceCount; ++Slot)
	{
		TestEqual(TEXT("Fixed seed and position reproduce the exact slot weight"), Repeat[Slot], First[Slot]);
	}

	const FGuid MissingSurfacePalette[] = {Palette[0], Palette[2]};
	FProgramPtr Rejected;
	TestFalse(TEXT("Missing surface ID is rejected"),
		FVoxelLandMaterialGraphProgram::Compile(NoiseSource, MissingSurfacePalette, Rejected, Error));
	TestTrue(TEXT("Missing surface diagnostic contains its ID"), Error.Contains(Palette[1].ToString()));
	TestFalse(TEXT("Failure never publishes a partial program"), Rejected.IsValid());
	NoiseSource.GraphRole = EVoxelLandGraphRole::Terrain;
	TestFalse(TEXT("A terrain-role graph cannot compile as a material graph"),
		FVoxelLandMaterialGraphProgram::Compile(NoiseSource, Palette, Rejected, Error));
	return true;
}

#endif
