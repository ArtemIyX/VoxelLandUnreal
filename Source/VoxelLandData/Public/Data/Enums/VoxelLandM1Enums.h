#pragma once

#include "CoreMinimal.h"

// Values are part of program format version 1.
enum class EVoxelLandOpcode : uint16
{
	LoadX,
	LoadY,
	LoadZ,
	LoadConstant,
	LoadFloatParameter,
	Add,
	Subtract,
	Multiply,
	Divide,
	Min,
	Max,
	Abs,
	Negate,
	Plane,
	Sphere,
	Box,
	Union,
	SubtractShape,
	Intersect,
	Perlin2,
	Perlin3,
	Fbm2,
	Fbm3,
	StoreDensity
};

enum class EVoxelLandParameterType : uint8
{
	Float,
	Int32
};
enum class EVoxelLandVMBackend : uint8
{
	Auto,
	Scalar,
	Simd4
};
enum class EVoxelLandNoiseBackend : uint8
{
	Auto,
	Scalar,
	SSE2
};
enum class EVoxelLandErrorCode : uint8
{
	None,
	GameThreadNotAllowed,
	InvalidProgram,
	InvalidParameter,
	InvalidProfile,
	UnsupportedLod,
	BackendUnavailable,
	NativeLibraryUnavailable,
	InvalidContext,
	NonFiniteCoordinate,
	NonFiniteDensity
};
