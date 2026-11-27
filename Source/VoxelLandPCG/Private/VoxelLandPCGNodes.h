#pragma once

#include "CoreMinimal.h"
#include "Data/PCGBasePointData.h"
#include "PCGPin.h"

class UPCGBasePointData;
class UPCGVoxelTerrainData;
struct FPCGContext;
struct FPCGTaggedData;
struct FVoxelLandTerrainPlacementPoint;

class FVoxelLandPCGNodes final
{
public:
	static const FName TerrainPinName;
	static FPCGPinProperties TerrainPin(bool bRequired);
	static const UPCGVoxelTerrainData* GetTerrain(const FPCGContext& Context);
	static UPCGBasePointData* MakePoints(FPCGContext& Context, int32 Count, const UPCGBasePointData* Input = nullptr);
	static void PublishPoints(FPCGContext& Context, UPCGBasePointData& Points, const FPCGTaggedData* Input = nullptr);
	static bool Fail(FPCGContext& Context, const FText& Reason);
	static EPCGPointNativeProperties SurfaceProperties();
	static bool WriteSurfaceMetadata(
		UPCGBasePointData& Points, TConstArrayView<FVoxelLandTerrainPlacementPoint> Samples);
};
