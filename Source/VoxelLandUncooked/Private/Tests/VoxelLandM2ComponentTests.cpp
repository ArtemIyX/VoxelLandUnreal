#if WITH_DEV_AUTOMATION_TESTS
#include "Rendering/VoxelLandChunkComponent.h"
#include "Materials/Material.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM2ComponentTest, "VoxelLand.M2.ComponentContract",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVoxelLandM2ComponentTest::RunTest(const FString& Parameters)
{
	UVoxelLandChunkComponent* Component = NewObject<UVoxelLandChunkComponent>();
	TestNotNull(TEXT("component"), Component);
	TestEqual(TEXT("no collision"), int32(Component->GetCollisionEnabled()), int32(ECollisionEnabled::NoCollision));
	TestEqual(TEXT("one material"), Component->GetNumMaterials(), 1);
	UMaterialInterface* Material = UMaterial::GetDefaultMaterial(MD_Surface);
	Component->SetMaterial(0, Material);
	TestEqual(TEXT("assigned material"), Component->GetMaterial(0), Material);
	auto Mesh = MakeShared<FVoxelLandMeshData, ESPMode::ThreadSafe>();
	FVoxelLandMeshVertex A, B, C;
	B.Position = FVector3f(1, 0, 0);
	C.Position = FVector3f(0, 1, 0);
	Mesh->Vertices = {A, B, C};
	Mesh->Indices = {0, 1, 2};
	Mesh->Bounds += A.Position;
	Mesh->Bounds += B.Position;
	Mesh->Bounds += C.Position;
	TestTrue(TEXT("valid mesh accepted"), Component->SetMesh(Mesh));
	auto Bad = MakeShared<FVoxelLandMeshData, ESPMode::ThreadSafe>(*Mesh);
	Bad->Indices[2] = 99;
	TestFalse(TEXT("invalid index rejected"), Component->SetMesh(Bad));
	Bad = MakeShared<FVoxelLandMeshData, ESPMode::ThreadSafe>(*Mesh);
	Bad->Bounds = FBox3f(FVector3f(0, 0, 0), FVector3f(0, 0, 0));
	TestFalse(TEXT("invalid bounds rejected"), Component->SetMesh(Bad));
	Component->SetVisibility(false);
	TestFalse(TEXT("hidden component"), Component->IsVisible());
	Component->SetVisibility(true);
	TestTrue(TEXT("visible component"), Component->IsVisible());
	Component->ClearMesh();
	TestNull(TEXT("empty mesh creates no proxy"), Component->CreateSceneProxy());
	return true;
}
#endif
