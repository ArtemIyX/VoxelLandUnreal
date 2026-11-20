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

namespace
{
class FVoxelLandIndexBuffer final : public FIndexBuffer
{
public:
    TArray<uint32> Indices;
    virtual void InitRHI(FRHICommandListBase& RHICmdList) override
    {
        IndexBufferRHI=UE::RHIResourceUtils::CreateIndexBufferFromArray(RHICmdList,TEXT("VoxelLandChunkIndices"),EBufferUsageFlags::Static | EBufferUsageFlags::ShaderResource,MakeConstArrayView(Indices));
    }
};
class FVoxelLandChunkProxy final : public FPrimitiveSceneProxy
{
public:
    FVoxelLandChunkProxy(UVoxelLandChunkComponent* Component, TSharedPtr<const FVoxelLandMeshData,ESPMode::ThreadSafe> InMesh)
        : FPrimitiveSceneProxy(Component), Mesh(MoveTemp(InMesh)), Factory(GetScene().GetFeatureLevel(),"VoxelLandChunk")
    {
        UMaterialInterface* Material=Component->GetMaterial(0);
        if (!Material || !Material->GetMaterial() || Material->GetMaterial()->MaterialDomain != MD_Surface) Material=UMaterial::GetDefaultMaterial(MD_Surface);
        MaterialProxy=Material->GetRenderProxy();
        Relevance=Material->GetRelevance_Concurrent(GetScene().GetShaderPlatform());
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
        TArray<FDynamicMeshVertex> Vertices; Vertices.Reserve(Mesh->Vertices.Num());
        for (const FVoxelLandMeshVertex& Source : Mesh->Vertices)
        {
            FDynamicMeshVertex Vertex;
            Vertex.Position=Source.Position; Vertex.TextureCoordinate[0]=Source.UV; Vertex.Color=Source.Color;
            Vertex.SetTangents(Source.Tangent,FVector3f::CrossProduct(Source.Normal,Source.Tangent),Source.Normal);
            Vertices.Add(Vertex);
        }
        Buffers.InitFromDynamicVertex(RHICmdList,&Factory,Vertices);
        IndexBuffer.Indices=Mesh->Indices;
        IndexBuffer.InitResource(RHICmdList);
    }
    virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views,const FSceneViewFamily& ViewFamily,uint32 VisibilityMap,FMeshElementCollector& Collector) const override
    {
        for (int32 ViewIndex=0; ViewIndex<Views.Num(); ++ViewIndex)
        {
            if (!(VisibilityMap & (1u<<ViewIndex))) continue;
            FMeshBatch& Batch=Collector.AllocateMesh();
            Batch.VertexFactory=&Factory; Batch.MaterialRenderProxy=MaterialProxy;
            Batch.ReverseCulling=IsLocalToWorldDeterminantNegative(); Batch.CastShadow=IsShadowCast(Views[ViewIndex]);
            Batch.Type=PT_TriangleList; Batch.DepthPriorityGroup=SDPG_World;
            FMeshBatchElement& Element=Batch.Elements[0];
            Element.IndexBuffer=&IndexBuffer; Element.FirstIndex=0;
            Element.MinVertexIndex=0; Element.MaxVertexIndex=Mesh->Vertices.Num()-1;
            Element.NumPrimitives=Mesh->Indices.Num()/3;
            Collector.AddMesh(ViewIndex,Batch);
        }
    }
    virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override
    {
        FPrimitiveViewRelevance Result;
        Result.bDrawRelevance=IsShown(View); Result.bDynamicRelevance=true;
        Result.bShadowRelevance=IsShadowCast(View); Result.bRenderInMainPass=ShouldRenderInMainPass();
        Result.bRenderCustomDepth=ShouldRenderCustomDepth();
        Result.bUsesLightingChannels=GetLightingChannelMask()!=GetDefaultLightingChannelMask();
        Relevance.SetPrimitiveViewRelevance(Result);
        return Result;
    }
    virtual SIZE_T GetTypeHash() const override { static size_t Unique; return reinterpret_cast<SIZE_T>(&Unique); }
    virtual uint32 GetMemoryFootprint() const override { return sizeof(*this)+GetAllocatedSize(); }
private:
    TSharedPtr<const FVoxelLandMeshData,ESPMode::ThreadSafe> Mesh;
    FStaticMeshVertexBuffers Buffers;
    FVoxelLandIndexBuffer IndexBuffer;
    FLocalVertexFactory Factory;
    const FMaterialRenderProxy* MaterialProxy=nullptr;
    FMaterialRelevance Relevance;
};
}

UVoxelLandChunkComponent::UVoxelLandChunkComponent()
{
    SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
    SetGenerateOverlapEvents(false); SetCanEverAffectNavigation(false);
}
bool UVoxelLandChunkComponent::SetMesh(TSharedPtr<const FVoxelLandMeshData,ESPMode::ThreadSafe> InMesh)
{
    check(IsInGameThread());
    if (!InMesh || InMesh->Vertices.IsEmpty() || InMesh->Indices.IsEmpty() || InMesh->Indices.Num()%3 != 0 || !InMesh->Bounds.IsValid) return false;
    for (uint32 I : InMesh->Indices) if (I>=uint32(InMesh->Vertices.Num())) return false;
    for (const auto& V : InMesh->Vertices)
        if (V.Position.ContainsNaN() || V.Normal.ContainsNaN() || V.Tangent.ContainsNaN() || !InMesh->Bounds.IsInsideOrOn(V.Position)) return false;
    Mesh=MoveTemp(InMesh); UpdateBounds(); MarkRenderStateDirty(); return true;
}
void UVoxelLandChunkComponent::ClearMesh() { check(IsInGameThread()); Mesh.Reset(); UpdateBounds(); MarkRenderStateDirty(); }
FPrimitiveSceneProxy* UVoxelLandChunkComponent::CreateSceneProxy()
{
    return Mesh && !Mesh->Indices.IsEmpty() ? new FVoxelLandChunkProxy(this,Mesh) : nullptr;
}
FBoxSphereBounds UVoxelLandChunkComponent::CalcBounds(const FTransform& LocalToWorld) const
{
    if (!Mesh || !Mesh->Bounds.IsValid) return FBoxSphereBounds(FSphere(FVector::ZeroVector,0.f)).TransformBy(LocalToWorld);
    const FBox Box(FVector(Mesh->Bounds.Min),FVector(Mesh->Bounds.Max));
    return FBoxSphereBounds(Box.ExpandBy(0.1)).TransformBy(LocalToWorld);
}
UMaterialInterface* UVoxelLandChunkComponent::GetMaterial(int32 ElementIndex) const { return ElementIndex==0 ? TerrainMaterial.Get() : nullptr; }
void UVoxelLandChunkComponent::SetMaterial(int32 ElementIndex,UMaterialInterface* Material)
{
    if (ElementIndex==0 && TerrainMaterial!=Material) { TerrainMaterial=Material; MarkRenderStateDirty(); }
}
void UVoxelLandChunkComponent::GetUsedMaterials(TArray<UMaterialInterface*>& OutMaterials,bool bGetDebugMaterials) const
{
    UMaterialInterface* Material=TerrainMaterial.Get();
    if (!Material || !Material->GetMaterial() || Material->GetMaterial()->MaterialDomain != MD_Surface) Material=UMaterial::GetDefaultMaterial(MD_Surface);
    OutMaterials.Add(Material);
}
