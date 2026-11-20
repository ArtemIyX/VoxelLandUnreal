#pragma once

#include "CoreMinimal.h"

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
