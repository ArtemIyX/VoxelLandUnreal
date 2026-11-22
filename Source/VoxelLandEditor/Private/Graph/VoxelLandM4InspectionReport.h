#pragma once
#include "CoreMinimal.h"
struct FVoxelLandM4WorldSnapshot;

namespace VoxelLandM4Inspection
{
// Text for the captured input descriptor. Compiled programs and LUTs are not
// embedded; retain the graph/config assets and matching source revisions.
FString DescribeSnapshot(const FVoxelLandM4WorldSnapshot& Snapshot);
}
