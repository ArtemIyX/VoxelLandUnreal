#pragma once

#include "CoreMinimal.h"

// Serialized operator IDs are FGuid(0x564c4d33, 1, 0, ordinal); never renumber or reuse them.
enum class EVoxelLandBuiltinNode : uint32
{
    DensityOutput = 1,
    PositionX = 2,
    PositionY = 3,
    PositionZ = 4,
    FloatConstant = 5,
    FloatParameter = 6,
    Add = 7,
    Subtract = 8,
    Multiply = 9,
    Divide = 10,
    Min = 11,
    Max = 12,
    Abs = 13,
    Negate = 14,
    Union = 15,
    SubtractShape = 16,
    Intersect = 17,
    HeightToDensity = 18,
    Clamp = 19,
    Saturate = 20,
    Lerp = 21,
    IntConstant = 22,
    BoolConstant = 23,
    IntParameter = 24,
    BoolParameter = 25,
    IntToFloat = 26,
    Less = 27,
    LessEqual = 28,
    Equal = 29,
    BoolAnd = 30,
    BoolOr = 31,
    BoolNot = 32,
    SelectFloat = 33,
    SelectInt = 34,
    SelectBool = 35,
    Power = 36,
    Plane = 37,
    Sphere = 38,
    Box = 39,
    Perlin2 = 40,
    Perlin3 = 41,
    Fbm2 = 42,
    Fbm3 = 43,
    Value2 = 44,
    Value3 = 45,
    Simplex2 = 46,
    Simplex3 = 47,
    RerouteFloat = 48,
    RerouteInt = 49,
    RerouteBool = 50,
    SmoothDensity = 51,
};

inline FGuid VoxelLandBuiltinNodeId(EVoxelLandBuiltinNode Node)
{
    return FGuid(0x564c4d33, 1, 0, uint32(Node));
}
