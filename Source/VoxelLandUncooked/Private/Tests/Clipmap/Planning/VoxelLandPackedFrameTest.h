#pragma once

#include "DynamicMeshBuilder.h"

class FVoxelLandM7PackedFrameTest final
{
public:
	static bool Matches(
		const FVector3f& NormalA, const FVector3f& TangentA, const FVector3f& NormalB, const FVector3f& TangentB)
	{
		FDynamicMeshVertex A, B;
		A.SetTangents(TangentA, FVector3f::CrossProduct(NormalA, TangentA), NormalA);
		B.SetTangents(TangentB, FVector3f::CrossProduct(NormalB, TangentB), NormalB);
		return SameBits(NormalA, NormalB) && SameBits(TangentA, TangentB)
			&& A.TangentX.Vector.Packed == B.TangentX.Vector.Packed
			&& A.TangentZ.Vector.Packed == B.TangentZ.Vector.Packed && A.TangentZ.Vector.W > 0;
	}

private:
	static bool SameBits(const FVector3f& A, const FVector3f& B)
	{
		return FMemory::Memcmp(&A.X, &B.X, sizeof(float)) == 0 && FMemory::Memcmp(&A.Y, &B.Y, sizeof(float)) == 0
			&& FMemory::Memcmp(&A.Z, &B.Z, sizeof(float)) == 0;
	}
};
