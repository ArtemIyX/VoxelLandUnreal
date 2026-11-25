#pragma once
#include "CoreMinimal.h"
#include "Data/Meshing/VoxelLandMeshData.h"
#include "Generation/Caves/Infrastructure/VoxelLandCaveCheckedMath.h"
#include "Generation/Caves/Geometry/VoxelLandCaveFixedTriangleQ18.h"
#include "Generation/Caves/Geometry/VoxelLandCaveValidationTriangle.h"
#include <cmath>

namespace VoxelLand::M5::MeshExactSource
{
inline bool ToQ18(double Value, int64& Out)
{
	constexpr double Limit = double(int64(1) << 39);
	const double Scaled = Value * 262144.0;
	if (!std::isfinite(Scaled) || Scaled < -Limit || Scaled > Limit)
	{
		return false;
	}
	const int64 Fixed = int64(Scaled);
	if (double(Fixed) != Scaled)
	{
		return false;
	}
	Out = Fixed;
	return true;
}
inline bool Check(const FVoxelLandM5ValidationTriangle& Triangle, const FVoxelLandM5FixedTriangleQ18& Fixed,
	const FVoxelLandMeshData& Mesh, const FVector& Origin)
{
	if (Triangle.SourceTriangle < 0 || int64(Triangle.SourceTriangle) * 3 + 2 >= Mesh.Indices.Num())
	{
		return false;
	}
	const FVoxelLandM5FixedPointQ18* Points[] = {&Fixed.A, &Fixed.B, &Fixed.C};
	for (int32 Vertex = 0; Vertex < 3; ++Vertex)
	{
		const uint32 Index = Mesh.Indices[Triangle.SourceTriangle * 3 + Vertex];
		if (Index >= uint32(Mesh.Vertices.Num()))
		{
			return false;
		}
		const FVector3f& Local = Mesh.Vertices[Index].Position;
		const int64 Values[] = {Points[Vertex]->X, Points[Vertex]->Y, Points[Vertex]->Z};
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			int64 A = 0, B = 0, Sum = 0;
			if (!ToQ18(double(Origin[Axis]), A) || !ToQ18(double(Local[Axis]), B) || !CheckedMath::TryAdd(A, B, Sum)
				|| Sum != Values[Axis])
			{
				return false;
			}
		}
	}
	return true;
}
}
