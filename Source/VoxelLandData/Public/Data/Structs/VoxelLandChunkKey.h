#pragma once

#include "CoreMinimal.h"

struct FVoxelLandChunkKey
{
    FIntVector Coordinate = FIntVector::ZeroValue;
    uint8 LOD = 0;

    friend bool operator==(const FVoxelLandChunkKey& A, const FVoxelLandChunkKey& B)
    {
        return A.Coordinate == B.Coordinate && A.LOD == B.LOD;
    }
    friend uint32 GetTypeHash(const FVoxelLandChunkKey& Key)
    {
        return HashCombine(GetTypeHash(Key.Coordinate), GetTypeHash(Key.LOD));
    }
};

namespace VoxelLandCoordinates
{
    // B must be positive. C++ division truncates, whereas region mapping needs floor.
    inline int32 FloorDivide(int32 A, int32 B)
    {
        check(B > 0);
        const int32 Quotient = A / B;
        return Quotient - ((A % B) < 0 ? 1 : 0);
    }

    inline FIntVector RegionOf(const FIntVector& Chunk, const FIntVector& Size)
    {
        return FIntVector(FloorDivide(Chunk.X, Size.X), FloorDivide(Chunk.Y, Size.Y), FloorDivide(Chunk.Z, Size.Z));
    }
}
