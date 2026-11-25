#pragma once

#include "CoreMinimal.h"

// Logical base-lattice cell and binary fractions in [0, 65536).
struct FVoxelLandM7NormalAnchor
{
	FInt64Vector BaseCell = FInt64Vector(0);
	FIntVector FractionQ16 = FIntVector::ZeroValue;

	friend bool operator==(const FVoxelLandM7NormalAnchor& A, const FVoxelLandM7NormalAnchor& B)
	{
		return A.BaseCell == B.BaseCell && A.FractionQ16 == B.FractionQ16;
	}
};
