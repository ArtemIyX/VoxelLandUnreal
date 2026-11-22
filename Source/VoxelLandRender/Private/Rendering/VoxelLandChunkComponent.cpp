#include "Rendering/VoxelLandChunkComponent.h"
#include "DynamicMeshBuilder.h"
#include "Engine/CollisionProfile.h"
#include "LocalVertexFactory.h"
#include "Materials/Material.h"
#include "PrimitiveSceneProxy.h"
#include "PrimitiveViewRelevance.h"
#include "RHIResourceUtils.h"
#include "SceneManagement.h"
#include "StaticMeshResources.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#if WITH_EDITOR
#include "Rendering/VoxelLandPreviewUploadObservation.h"
#endif

class FVoxelLandChunkComponentInternals final
{
public:
	class FVoxelLandIndexBuffer final : public FIndexBuffer
	{
	public:
		TArray<uint32> Indices;

		virtual void InitRHI(FRHICommandListBase& RHICmdList) override
		{
			IndexBufferRHI = UE::RHIResourceUtils::CreateIndexBufferFromArray(RHICmdList, TEXT("VoxelLandChunkIndices"),
				EBufferUsageFlags::Static | EBufferUsageFlags::ShaderResource, MakeConstArrayView(Indices));
		}
	};

	class FVoxelLandChunkProxy final : public FPrimitiveSceneProxy
	{
	public:
		FVoxelLandChunkProxy(
			UVoxelLandChunkComponent* Component, TSharedPtr<const FVoxelLandMeshData, ESPMode::ThreadSafe> InMesh
#if WITH_EDITOR
			,
			TSharedPtr<FVoxelLandPreviewUploadObservation, ESPMode::ThreadSafe> InObservation
#endif
			)
			: FPrimitiveSceneProxy(Component)
			, Mesh(MoveTemp(InMesh))
			, Factory(GetScene().GetFeatureLevel(), "VoxelLandChunk")
#if WITH_EDITOR
			, UploadObservation(MoveTemp(InObservation))
#endif
		{
			UMaterialInterface* Material = Component->GetMaterial(0);
			if (!Material || !Material->GetMaterial() || Material->GetMaterial()->MaterialDomain != MD_Surface)
			{
				Material = UMaterial::GetDefaultMaterial(MD_Surface);
			}
			MaterialProxy = Material->GetRenderProxy();
			Relevance = Material->GetRelevance_Concurrent(GetScene().GetShaderPlatform());
		}

		virtual ~FVoxelLandChunkProxy() override
		{
			Buffers.PositionVertexBuffer.ReleaseResource();
			Buffers.StaticMeshVertexBuffer.ReleaseResource();
			Buffers.ColorVertexBuffer.ReleaseResource();
			IndexBuffer.ReleaseResource();
			Factory.ReleaseResource();
		}

		virtual void CreateRenderThreadResources(FRHICommandListBase& RHICmdList) override
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(VoxelLand_RenderResourceSetup);
			TArray<FDynamicMeshVertex> Vertices;
			Vertices.Reserve(Mesh->Vertices.Num());
			for (const FVoxelLandMeshVertex& Source : Mesh->Vertices)
			{
				FDynamicMeshVertex Vertex;
				Vertex.Position = Source.Position;
				Vertex.TextureCoordinate[0] = Source.UV;
				Vertex.Color = Source.Color;
				Vertex.SetTangents(
					Source.Tangent, FVector3f::CrossProduct(Source.Normal, Source.Tangent), Source.Normal);
				Vertices.Add(Vertex);
			}
			Buffers.InitFromDynamicVertex(RHICmdList, &Factory, Vertices);
			IndexBuffer.Indices = Mesh->Indices;
			IndexBuffer.InitResource(RHICmdList);
#if WITH_EDITOR
			if (UploadObservation)
			{
				UploadObservation->RecordUpload(RHICmdList);
				UploadObservation.Reset();
			}
#endif
		}

		virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily,
			uint32 VisibilityMap, FMeshElementCollector& Collector) const override
		{
			for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
			{
				if (!(VisibilityMap & (1u << ViewIndex)))
				{
					continue;
				}
				FMeshBatch& Batch = Collector.AllocateMesh();
				Batch.VertexFactory = &Factory;
				Batch.MaterialRenderProxy = MaterialProxy;
				// Surface Nets triangles follow the outward density gradient. Unreal's
				// rasterizer front-face convention requires the opposite cull mode.
				Batch.ReverseCulling = !IsLocalToWorldDeterminantNegative();
				Batch.CastShadow = IsShadowCast(Views[ViewIndex]);
				Batch.Type = PT_TriangleList;
				Batch.DepthPriorityGroup = SDPG_World;
				FMeshBatchElement& Element = Batch.Elements[0];
				Element.PrimitiveUniformBuffer = GetUniformBuffer();
				Element.IndexBuffer = &IndexBuffer;
				Element.FirstIndex = 0;
				Element.MinVertexIndex = 0;
				Element.MaxVertexIndex = Mesh->Vertices.Num() - 1;
				Element.NumPrimitives = Mesh->Indices.Num() / 3;
				Collector.AddMesh(ViewIndex, Batch);
			}
		}

		virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override
		{
			FPrimitiveViewRelevance Result;
			Result.bDrawRelevance = IsShown(View);
			Result.bDynamicRelevance = true;
			Result.bShadowRelevance = IsShadowCast(View);
			Result.bRenderInMainPass = ShouldRenderInMainPass();
			Result.bRenderCustomDepth = ShouldRenderCustomDepth();
			Result.bUsesLightingChannels = GetLightingChannelMask() != GetDefaultLightingChannelMask();
			Relevance.SetPrimitiveViewRelevance(Result);
			return Result;
		}

		virtual SIZE_T GetTypeHash() const override
		{
			static size_t Unique;
			return reinterpret_cast<SIZE_T>(&Unique);
		}

		virtual uint32 GetMemoryFootprint() const override { return sizeof(*this) + GetAllocatedSize(); }

	private:
		TSharedPtr<const FVoxelLandMeshData, ESPMode::ThreadSafe> Mesh;
		FStaticMeshVertexBuffers Buffers;
		FVoxelLandIndexBuffer IndexBuffer;
		FLocalVertexFactory Factory;
#if WITH_EDITOR
		TSharedPtr<FVoxelLandPreviewUploadObservation, ESPMode::ThreadSafe> UploadObservation;
#endif
		const FMaterialRenderProxy* MaterialProxy = nullptr;
		FMaterialRelevance Relevance;
	};
};

UVoxelLandChunkComponent::UVoxelLandChunkComponent()
{
	BodyInstance.SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	SetGenerateOverlapEvents(false);
	SetCanEverAffectNavigation(false);
}

bool UVoxelLandChunkComponent::SetMesh(TSharedPtr<const FVoxelLandMeshData, ESPMode::ThreadSafe> InMesh)
{
	check(IsInGameThread());
	if (!InMesh || InMesh->Vertices.IsEmpty() || InMesh->Indices.IsEmpty() || InMesh->Indices.Num() % 3 != 0
	    || !InMesh->Bounds.IsValid)
	{
		return false;
	}
	for (uint32 Index : InMesh->Indices)
	{
		if (Index >= static_cast<uint32>(InMesh->Vertices.Num()))
		{
			return false;
		}
	}
	for (const FVoxelLandMeshVertex& Vertex : InMesh->Vertices)
	{
		if (Vertex.Position.ContainsNaN() || Vertex.Normal.ContainsNaN() || Vertex.Tangent.ContainsNaN()
		    || !InMesh->Bounds.IsInsideOrOn(Vertex.Position))
		{
			return false;
		}
	}
	Mesh = MoveTemp(InMesh);
	UpdateBounds();
	MarkRenderStateDirty();
	return true;
}

void UVoxelLandChunkComponent::ClearMesh()
{
	check(IsInGameThread());
	Mesh.Reset();
	UpdateBounds();
	MarkRenderStateDirty();
}

FPrimitiveSceneProxy* UVoxelLandChunkComponent::CreateSceneProxy()
{
	if (!Mesh || Mesh->Indices.IsEmpty())
	{
		return nullptr;
	}
	return new FVoxelLandChunkComponentInternals::FVoxelLandChunkProxy(this, Mesh
#if WITH_EDITOR
		,
		MoveTemp(PreviewUploadObservation)
#endif
	);
}

#if WITH_EDITOR
void UVoxelLandChunkComponent::SetPreviewUploadObservation(
	TSharedPtr<FVoxelLandPreviewUploadObservation, ESPMode::ThreadSafe> InObservation)
{
	check(IsInGameThread());
	PreviewUploadObservation = MoveTemp(InObservation);
}
#endif

FBoxSphereBounds UVoxelLandChunkComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	if (!Mesh || !Mesh->Bounds.IsValid)
	{
		return FBoxSphereBounds(FSphere(FVector::ZeroVector, 0.f)).TransformBy(LocalToWorld);
	}
	const FBox Box(FVector(Mesh->Bounds.Min), FVector(Mesh->Bounds.Max));
	return FBoxSphereBounds(Box.ExpandBy(0.1)).TransformBy(LocalToWorld);
}

UMaterialInterface* UVoxelLandChunkComponent::GetMaterial(int32 ElementIndex) const
{
	return ElementIndex == 0 ? TerrainMaterial.Get() : nullptr;
}

void UVoxelLandChunkComponent::SetMaterial(int32 ElementIndex, UMaterialInterface* Material)
{
	if (ElementIndex == 0 && TerrainMaterial != Material)
	{
		TerrainMaterial = Material;
		MarkRenderStateDirty();
	}
}

void UVoxelLandChunkComponent::GetUsedMaterials(
	TArray<UMaterialInterface*>& OutMaterials, bool bGetDebugMaterials) const
{
	UMaterialInterface* Material = TerrainMaterial.Get();
	if (!Material || !Material->GetMaterial() || Material->GetMaterial()->MaterialDomain != MD_Surface)
	{
		Material = UMaterial::GetDefaultMaterial(MD_Surface);
	}
	OutMaterials.Add(Material);
}