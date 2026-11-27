#pragma once

#include "Data/Registry/PCGDataType.h"

#include "VoxelLandPCGTerrainDataType.generated.h"

USTRUCT(meta = (PCG_DataTypeDisplayName = "Voxel Terrain"))
struct FPCGDataTypeInfoVoxelTerrain final : public FPCGDataTypeInfo
{
	GENERATED_BODY()

	PCG_DECLARE_TYPE_INFO(VOXELLANDPCG_API);
};
