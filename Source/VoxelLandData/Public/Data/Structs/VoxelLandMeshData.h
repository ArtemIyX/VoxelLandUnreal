#pragma once

#include "CoreMinimal.h"
#include <cmath>

// Plain worker-owned geometry; publish via a thread-safe const shared pointer.
struct FVoxelLandMeshVertex
{
    FVector3f Position = FVector3f::ZeroVector;
    FVector3f Normal = FVector3f::UpVector;
    FVector3f Tangent = FVector3f::ForwardVector;
    FVector2f UV = FVector2f::ZeroVector;
    FColor Color = FColor::White;
};

struct FVoxelLandMeshData
{
    TArray<FVoxelLandMeshVertex> Vertices;
    TArray<uint32> Indices;
    FBox3f Bounds = FBox3f(ForceInit);
    uint32 NormalFallbacks = 0;
};

enum class EVoxelLandChunkClassification : uint8
{
    NeedsSamples,
    Empty,
    Solid,
    Surface
};

// Mesh-local floats share an exactly representable binary grid relative to a
// similarly snapped double-precision component origin. The grid is chosen
// from the maximum local extent, not from an assumed voxel size.
namespace VoxelLandMeshCoordinates
{
    inline double Grid(float Size)
    {
        int Exponent=0;
        std::frexp(double(Size)*34.0,&Exponent);
        return std::ldexp(1.0,Exponent-24);
    }
    inline double Snap(double Value, double Step) { return std::round(Value/Step)*Step; }
    inline FVector RenderOrigin(const FIntVector& Chunk, float Size)
    {
        const double Step=Grid(Size), Width=double(Size)*32.0;
        return FVector(Snap(double(Chunk.X)*Width,Step),Snap(double(Chunk.Y)*Width,Step),Snap(double(Chunk.Z)*Width,Step));
    }
}
