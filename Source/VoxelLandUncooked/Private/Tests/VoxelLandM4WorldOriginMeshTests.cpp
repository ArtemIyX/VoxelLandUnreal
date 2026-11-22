#if WITH_DEV_AUTOMATION_TESTS && !UE_SERVER
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Data/Structs/VoxelLandMeshData.h"
#include "Materials/Material.h"
#include "Misc/AutomationTest.h"
#include "Rendering/VoxelLandChunkComponent.h"
#include "World/VoxelLandWorld.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM4WorldOriginMeshTest, "VoxelLand.M4.World.RebasedMeshPublication",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVoxelLandM4WorldOriginMeshTest::RunTest(const FString& Parameters)
{
	if (!TestNotNull(TEXT("engine exists"), GEngine))
	{
		return false;
	}
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("world created"), World))
	{
		return false;
	}
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	World->AddToRoot();
	Context.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	AVoxelLandRegion* Region = World->SpawnActor<AVoxelLandRegion>();
	if (!TestNotNull(TEXT("region created"), Region))
	{
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
		return false;
	}
	TSharedPtr<FVoxelLandMeshData, ESPMode::ThreadSafe> Mesh = MakeShared<FVoxelLandMeshData, ESPMode::ThreadSafe>();
	FVoxelLandMeshVertex A, B, C;
	B.Position = FVector3f(100, 0, 0);
	C.Position = FVector3f(0, 100, 0);
	Mesh->Vertices = {A, B, C};
	Mesh->Indices = {0, 1, 2};
	Mesh->Bounds += A.Position;
	Mesh->Bounds += B.Position;
	Mesh->Bounds += C.Position;
	UMaterialInterface* Material = UMaterial::GetDefaultMaterial(MD_Surface);
	const FVector GlobalFirst(32000000.0, -16000000.0, 1000.0), GlobalSecond(32000100.0, -16000000.0, 1000.0);
	bool bPublished = Region->Publish(FIntVector::ZeroValue, GlobalFirst, Mesh, Material);
	TestTrue(TEXT("first global mesh publishes"), bPublished);
	TInlineComponentArray<UVoxelLandChunkComponent*> Components;
	Region->GetComponents(Components);
	if (TestEqual(TEXT("one registered component"), Components.Num(), 1))
	{
		TestTrue(TEXT("initial component has global position"),
			Components[0]->GetComponentLocation().Equals(GlobalFirst, 0.0));
	}
	TestTrue(TEXT("world origin changes"), World->SetNewWorldOrigin(FIntVector(32000000, -16000000, 1000)));
	Components.Reset();
	Region->GetComponents(Components);
	if (TestEqual(TEXT("existing component remains registered"), Components.Num(), 1))
	{
		TestTrue(TEXT("Unreal shifts existing component"),
			Components[0]->GetComponentLocation().Equals(FVector::ZeroVector, 0.0));
	}
	TestTrue(TEXT("second global mesh publishes after rebase"),
		Region->Publish(FIntVector(1, 0, 0), GlobalSecond, Mesh, Material));
	Components.Reset();
	Region->GetComponents(Components);
	bool bAtZero = false, bAtHundred = false;
	for (const UVoxelLandChunkComponent* Component : Components)
	{
		bAtZero |= Component->GetComponentLocation().Equals(FVector::ZeroVector, 0.0);
		bAtHundred |= Component->GetComponentLocation().Equals(FVector(100, 0, 0), 0.0);
	}
	TestEqual(TEXT("two components retained"), Components.Num(), 2);
	TestTrue(TEXT("new and old components share rebased coordinates"), bAtZero && bAtHundred);
	World->DestroyWorld(false);
	GEngine->DestroyWorldContext(World);
	World->RemoveFromRoot();
	return true;
}
#endif
