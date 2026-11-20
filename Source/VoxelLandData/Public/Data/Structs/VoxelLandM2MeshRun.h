#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandMeshData.h"

namespace VoxelLandM2TestData
{
struct FMeshRun
{
	uint64 DensityHash = 0;
	FVoxelLandMeshData Mesh;
	bool bSuccess = false;
};
}
