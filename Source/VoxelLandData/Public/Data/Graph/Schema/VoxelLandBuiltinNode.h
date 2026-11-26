#pragma once

#include "CoreMinimal.h"

// Serialized operator IDs are FGuid(0x564c4d33, 1, 0, ordinal); never renumber or reuse them.
#define VOXELLAND_BUILTIN_NODE_LIST(X) \
	X(DensityOutput, 1)                \
	X(PositionX, 2)                    \
	X(PositionY, 3)                    \
	X(PositionZ, 4)                    \
	X(FloatConstant, 5)                \
	X(FloatParameter, 6)               \
	X(Add, 7)                          \
	X(Subtract, 8)                     \
	X(Multiply, 9)                     \
	X(Divide, 10)                      \
	X(Min, 11)                         \
	X(Max, 12)                         \
	X(Abs, 13)                         \
	X(Negate, 14)                      \
	X(Union, 15)                       \
	X(SubtractShape, 16)               \
	X(Intersect, 17)                   \
	X(HeightToDensity, 18)             \
	X(Clamp, 19)                       \
	X(Saturate, 20)                    \
	X(Lerp, 21)                        \
	X(IntConstant, 22)                 \
	X(BoolConstant, 23)                \
	X(IntParameter, 24)                \
	X(BoolParameter, 25)               \
	X(IntToFloat, 26)                  \
	X(Less, 27)                        \
	X(LessEqual, 28)                   \
	X(Equal, 29)                       \
	X(BoolAnd, 30)                     \
	X(BoolOr, 31)                      \
	X(BoolNot, 32)                     \
	X(SelectFloat, 33)                 \
	X(SelectInt, 34)                   \
	X(SelectBool, 35)                  \
	X(Power, 36)                       \
	X(Plane, 37)                       \
	X(Sphere, 38)                      \
	X(Box, 39)                         \
	X(Perlin2, 40)                     \
	X(Perlin3, 41)                     \
	X(Fbm2, 42)                        \
	X(Fbm3, 43)                        \
	X(Value2, 44)                      \
	X(Value3, 45)                      \
	X(Simplex2, 46)                    \
	X(Simplex3, 47)                    \
	X(RerouteFloat, 48)                \
	X(RerouteInt, 49)                  \
	X(RerouteBool, 50)                 \
	X(SmoothDensity, 51)               \
	X(M4HeightOutput, 52)              \
	X(FloatOutput, 53)                 \
	X(IntOutput, 54)                   \
	X(BoolOutput, 55)                  \
	X(WorldNoise2D, 56)                \
	X(Surface, 57)                     \
	X(BlendSurfaces, 58)               \
	X(SurfaceMaterialOutput, 59)       \
	X(SurfaceSlope, 60)                \
	X(TerrainSignal, 61)               \
	X(WorldSignal, 62)                 \
	X(MaterialRemap, 63)

enum class EVoxelLandBuiltinNode : uint32
{
#define VOXELLAND_DECLARE_BUILTIN(Name, Value) Name = Value,
	VOXELLAND_BUILTIN_NODE_LIST(VOXELLAND_DECLARE_BUILTIN)
#undef VOXELLAND_DECLARE_BUILTIN
};

inline FGuid VoxelLandBuiltinNodeId(EVoxelLandBuiltinNode Node)
{
	return FGuid(0x564c4d33, 1, 0, uint32(Node));
}
