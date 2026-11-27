#include "VoxelLandPCGGetVoxelTerrainElement.h"
#include "VoxelLandPCGCell.h"
#include "VoxelLandPCGNodes.h"
#include "VoxelLandPCGTerrainData.h"
#include "PCGComponent.h"
#include "PCGContext.h"

bool FVoxelLandPCGGetVoxelTerrainElement::ExecuteInternal(FPCGContext* Context) const
{
	check(Context && IsInGameThread());
	const UPCGComponent* Source = Cast<UPCGComponent>(Context->ExecutionSource.Get());
	const AVoxelLandPCGCell* Cell = Source ? Cast<AVoxelLandPCGCell>(Source->GetOwner()) : nullptr;
	const UPCGVoxelTerrainData* Data = Cell ? Cell->GetTerrainData() : nullptr;
	if (!Data)
	{
		return FVoxelLandPCGNodes::Fail(*Context,
			NSLOCTEXT("VoxelLandPCG", "MissingTerrainCell", "Get Voxel Terrain requires a ready VoxelLand PCG cell."));
	}
	FPCGTaggedData& Output = Context->OutputData.TaggedData.AddDefaulted_GetRef();
	Output.Pin = FVoxelLandPCGNodes::TerrainPinName;
	Output.Data = Data;
	return true;
}
