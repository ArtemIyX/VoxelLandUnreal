#include "VoxelLandChaosTerrainComponentProxy.h"

#if !UE_SERVER && !UE_BUILD_SHIPPING
#include "Data/Meshing/VoxelLandChaosMesh.h"
#include "World/Collision/Chaos/VoxelLandChaosTerrainComponent.h"
#include "DynamicMeshBuilder.h"
#include "Engine/Engine.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialRenderProxy.h"
#include "PrimitiveViewRelevance.h"
#include "SceneInterface.h"
#include "SceneManagement.h"
#include "SceneView.h"

FVoxelLandChaosTerrainComponentProxy::FVoxelLandChaosTerrainComponentProxy(
	const UVoxelLandChaosTerrainComponent& Component, TSharedRef<const FVoxelLandChaosMesh, ESPMode::ThreadSafe> InMesh)
	: FPrimitiveSceneProxy(&Component)
	, Mesh(MoveTemp(InMesh))
	, bBlocksPawn(Component.IsQueryCollisionEnabled() && Component.GetCollisionResponseToChannel(ECC_Pawn) == ECR_Block)
	, bBlocksVisibility(
		  Component.IsQueryCollisionEnabled() && Component.GetCollisionResponseToChannel(ECC_Visibility) == ECR_Block)
{
}

SIZE_T FVoxelLandChaosTerrainComponentProxy::GetTypeHash() const
{
	static const uint8 TypeTag = 0;
	return reinterpret_cast<SIZE_T>(&TypeTag);
}

uint32 FVoxelLandChaosTerrainComponentProxy::GetMemoryFootprint() const
{
	return sizeof(*this) + GetAllocatedSize();
}

bool FVoxelLandChaosTerrainComponentProxy::IsCollisionView(const FSceneViewFamily& ViewFamily) const
{
	return (bBlocksPawn && ViewFamily.EngineShowFlags.CollisionPawn)
		|| (bBlocksVisibility && ViewFamily.EngineShowFlags.CollisionVisibility);
}

FPrimitiveViewRelevance FVoxelLandChaosTerrainComponentProxy::GetViewRelevance(const FSceneView* View) const
{
	FPrimitiveViewRelevance Relevance;
	Relevance.bDrawRelevance = IsShown(View) && IsCollisionView(*View->Family);
	Relevance.bDynamicRelevance = Relevance.bDrawRelevance;
	Relevance.bRenderInMainPass = Relevance.bDrawRelevance;
	return Relevance;
}

void FVoxelLandChaosTerrainComponentProxy::GetDynamicMeshElements(const TArray<const FSceneView*>& Views,
	const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const
{
	if (!IsCollisionView(ViewFamily) || !GEngine || !GEngine->ShadedLevelColorationUnlitMaterial)
	{
		return;
	}

	// Use the engine's static collision coloration. The immutable payload is the one installed by Chaos.
	const FLinearColor TerrainCollisionColor(FColor(0, 255, 0));
	FColoredMaterialRenderProxy* Material = MakeUnique<FColoredMaterialRenderProxy>(
		GEngine->ShadedLevelColorationUnlitMaterial->GetRenderProxy(), TerrainCollisionColor)
												.Release();
	Collector.RegisterOneFrameMaterialProxy(Material);
	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
	{
		if ((VisibilityMap & (1u << ViewIndex)) == 0)
		{
			continue;
		}
		FDynamicMeshBuilder Builder(GetScene().GetFeatureLevel());
		for (const FVector3f& Vertex : Mesh->Vertices)
		{
			Builder.AddVertex(FDynamicMeshVertex(Vertex));
		}
		for (int32 Index = 0; Index < Mesh->Indices.Num(); Index += 3)
		{
			Builder.AddTriangle(Mesh->Indices[Index], Mesh->Indices[Index + 1], Mesh->Indices[Index + 2]);
		}
		Builder.GetMesh(GetLocalToWorld(), Material, SDPG_World, true, false, ViewIndex, Collector);
	}
}
#endif
