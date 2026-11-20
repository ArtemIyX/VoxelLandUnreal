#include "Generation/VoxelLandMeshing.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

namespace
{
constexpr int32 CellCount = 34;
constexpr int32 Corner[8][3] = {{0,0,0},{1,0,0},{0,1,0},{1,1,0},{0,0,1},{1,0,1},{0,1,1},{1,1,1}};
constexpr int32 Edges[12][2] = {{0,1},{2,3},{4,5},{6,7},{0,2},{1,3},{4,6},{5,7},{0,4},{1,5},{2,6},{3,7}};
int32 SampleIndex(int32 X, int32 Y, int32 Z) { return (X+1) + 35*((Y+1)+35*(Z+1)); }
int32 CellIndex(int32 X, int32 Y, int32 Z) { return (X+1) + CellCount*((Y+1)+CellCount*(Z+1)); }
int16 Sample(const FVoxelLandChunkData& Chunk, int32 X, int32 Y, int32 Z) { return Chunk.Density[SampleIndex(X,Y,Z)]; }
bool IsSolid(int16 Q) { return Q < 0; }
FVector3f Axis(int32 I) { return I == 0 ? FVector3f(1,0,0) : I == 1 ? FVector3f(0,1,0) : FVector3f(0,0,1); }

FVector3f Gradient(const int16* Q, const FVector3f& P)
{
    const float X=P.X, Y=P.Y, Z=P.Z;
    const float DX = (1-Y)*(1-Z)*(Q[1]-Q[0]) + Y*(1-Z)*(Q[3]-Q[2]) + (1-Y)*Z*(Q[5]-Q[4]) + Y*Z*(Q[7]-Q[6]);
    const float DY = (1-X)*(1-Z)*(Q[2]-Q[0]) + X*(1-Z)*(Q[3]-Q[1]) + (1-X)*Z*(Q[6]-Q[4]) + X*Z*(Q[7]-Q[5]);
    const float DZ = (1-X)*(1-Y)*(Q[4]-Q[0]) + X*(1-Y)*(Q[5]-Q[1]) + (1-X)*Y*(Q[6]-Q[2]) + X*Y*(Q[7]-Q[3]);
    return FVector3f(DX,DY,DZ);
}

void AddTriangle(FVoxelLandMeshData& Mesh, uint32 A, uint32 B, uint32 C, float Scale)
{
    if (A == B || B == C || C == A) return;
    const FVector3f E1 = (Mesh.Vertices[B].Position-Mesh.Vertices[A].Position)/Scale;
    const FVector3f E2 = (Mesh.Vertices[C].Position-Mesh.Vertices[A].Position)/Scale;
    if (FVector3f::CrossProduct(E1,E2).SizeSquared() == 0.f) return;
    Mesh.Indices.Add(A); Mesh.Indices.Add(B); Mesh.Indices.Add(C);
}
}

EVoxelLandChunkClassification VoxelLandMeshing::ClassifyBeforeSampling() { return EVoxelLandChunkClassification::NeedsSamples; }
EVoxelLandChunkClassification VoxelLandMeshing::Classify(const FVoxelLandChunkData& Chunk)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(VoxelLand_Classify);
    if (Chunk.Density.Num() != FVoxelLandChunkData::SampleCount) return EVoxelLandChunkClassification::NeedsSamples;
    bool bSolid=false, bAir=false;
    for (const int16 Q : Chunk.Density) { bSolid |= IsSolid(Q); bAir |= !IsSolid(Q); if (bSolid && bAir) return EVoxelLandChunkClassification::Surface; }
    return bSolid ? EVoxelLandChunkClassification::Solid : EVoxelLandChunkClassification::Empty;
}

EVoxelLandMeshResult VoxelLandMeshing::BuildSurfaceNets(const FVoxelLandChunkData& Chunk, float VoxelSizeCm,
    FVoxelLandMeshingScratch& Scratch, FVoxelLandMeshData& OutMesh, TFunctionRef<bool()> IsCancelled)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(VoxelLand_SurfaceNets);
    if (Chunk.Key.LOD != 0 || Chunk.Density.Num() != FVoxelLandChunkData::SampleCount || !FMath::IsFinite(VoxelSizeCm) || VoxelSizeCm <= 0)
        return EVoxelLandMeshResult::InvalidInput;
    FVoxelLandMeshData Mesh;
    Scratch.CellVertices.Init(INDEX_NONE, CellCount*CellCount*CellCount);
    for (int32 Z=-1; Z<=32; ++Z)
    {
        if (IsCancelled()) return EVoxelLandMeshResult::Cancelled;
        for (int32 Y=-1; Y<=32; ++Y) for (int32 X=-1; X<=32; ++X)
        {
            int16 Q[8]; bool bSolid=false, bAir=false;
            for (int32 I=0; I<8; ++I) { Q[I]=Sample(Chunk,X+Corner[I][0],Y+Corner[I][1],Z+Corner[I][2]); bSolid |= IsSolid(Q[I]); bAir |= !IsSolid(Q[I]); }
            if (!bSolid || !bAir) continue;
            FVector3f Sum=FVector3f::ZeroVector, Fallback=FVector3f::ZeroVector; int32 Crossings=0;
            for (const auto& Edge : Edges)
            {
                const int32 A=Edge[0], B=Edge[1];
                if (IsSolid(Q[A]) == IsSolid(Q[B])) continue;
                const float T=float(Q[A])/float(int32(Q[A])-int32(Q[B]));
                const FVector3f PA(float(Corner[A][0]),float(Corner[A][1]),float(Corner[A][2]));
                const FVector3f PB(float(Corner[B][0]),float(Corner[B][1]),float(Corner[B][2]));
                Sum += PA+(PB-PA)*T; ++Crossings;
                Fallback += (PB-PA) * (Q[B] > Q[A] ? 1.f : -1.f);
            }
            const FVector3f Average=Sum/float(Crossings);
            // A 1/1024-cell binary grid keeps the 50/100 cm profiles exact after chunk-origin translation.
            const FVector3f P(FMath::RoundToFloat(Average.X*1024.f)/1024.f, FMath::RoundToFloat(Average.Y*1024.f)/1024.f, FMath::RoundToFloat(Average.Z*1024.f)/1024.f);
            FVector3f N=Gradient(Q,P).GetSafeNormal();
            if (N.IsNearlyZero()) { ++Mesh.NormalFallbacks; N=Fallback.GetSafeNormal(); if (N.IsNearlyZero()) N=FVector3f::UpVector; }
            const FVector3f Abs= N.GetAbs();
            const FVector3f Reference = Abs.X <= Abs.Y && Abs.X <= Abs.Z ? Axis(0) : (Abs.Y <= Abs.Z ? Axis(1) : Axis(2));
            FVoxelLandMeshVertex V;
            V.Position=(FVector3f(float(X),float(Y),float(Z))+P)*VoxelSizeCm;
            V.Normal=N; V.Tangent=FVector3f::CrossProduct(Reference,N).GetSafeNormal();
            V.UV=FVector2f(V.Position.X,V.Position.Y)/100.f;
            Mesh.Bounds += V.Position;
            Scratch.CellVertices[CellIndex(X,Y,Z)]=Mesh.Vertices.Add(V);
        }
    }
    // Each edge is owned by the chunk containing its lower endpoint on all axes.
    for (int32 Z=0; Z<32; ++Z)
    {
        if (IsCancelled()) return EVoxelLandMeshResult::Cancelled;
        for (int32 Y=0; Y<32; ++Y) for (int32 X=0; X<32; ++X) for (int32 AxisIndex=0; AxisIndex<3; ++AxisIndex)
        {
            int32 P[3]={X,Y,Z}; int32 End[3]={X,Y,Z}; ++End[AxisIndex];
            const bool bStartSolid=IsSolid(Sample(Chunk,P[0],P[1],P[2]));
            if (bStartSolid==IsSolid(Sample(Chunk,End[0],End[1],End[2]))) continue;
            const int32 U=(AxisIndex+1)%3, V=(AxisIndex+2)%3;
            constexpr int32 Offsets[4][2]={{-1,-1},{0,-1},{0,0},{-1,0}};
            uint32 Vertices[4];
            for (int32 I=0; I<4; ++I)
            {
                int32 C[3]={X,Y,Z}; C[U]+=Offsets[I][0]; C[V]+=Offsets[I][1];
                const int32 Id=Scratch.CellVertices[CellIndex(C[0],C[1],C[2])];
                if (Id == INDEX_NONE) return EVoxelLandMeshResult::InvalidInput;
                Vertices[I]=uint32(Id);
            }
            // Cyclic transverse axes give +axis; flip when density decreases along edge.
            if (!bStartSolid) Swap(Vertices[1],Vertices[3]);
            AddTriangle(Mesh,Vertices[0],Vertices[1],Vertices[2],VoxelSizeCm);
            AddTriangle(Mesh,Vertices[0],Vertices[2],Vertices[3],VoxelSizeCm);
        }
    }
    OutMesh=MoveTemp(Mesh);
    return EVoxelLandMeshResult::Success;
}
