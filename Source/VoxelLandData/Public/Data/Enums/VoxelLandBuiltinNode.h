#pragma once

#include "CoreMinimal.h"

// Stable operator IDs are FGuid(0x564c4d33, 1, 0, ordinal).
enum class EVoxelLandBuiltinNode : uint32
{
    DensityOutput = 1, PositionX, PositionY, PositionZ, FloatConstant, FloatParameter,
    Add, Subtract, Multiply, Divide, Min, Max, Abs, Negate, Union, SubtractShape,
    Intersect, HeightToDensity, Clamp, Saturate, Lerp,
    IntConstant, BoolConstant, IntParameter, BoolParameter, IntToFloat,
    Less, LessEqual, Equal, BoolAnd, BoolOr, BoolNot,
    SelectFloat, SelectInt, SelectBool, Power, Plane, Sphere, Box,
    Perlin2, Perlin3, Fbm2, Fbm3, Value2, Value3, Simplex2, Simplex3,
    RerouteFloat, RerouteInt, RerouteBool, SmoothDensity
};

inline FGuid VoxelLandBuiltinNodeId(EVoxelLandBuiltinNode Node)
{
    return FGuid(0x564c4d33, 1, 0, uint32(Node));
}
