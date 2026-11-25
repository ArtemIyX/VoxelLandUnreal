#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandError.h"

class FVoxelLandM6Redistance final
{
public:
	static constexpr int32 HaloForBand(int32 Band) { return Band > 0 ? Band + 1 : 0; }
	static constexpr int32 EdgeChecksPerSample(int32 Band)
	{
		return Band > 0 ? 3 * (2 * Band + 2) * (2 * Band + 1) * (2 * Band + 1) : 0;
	}
	static FVoxelLandError Apply(TConstArrayView<int16> Input, int32 InputAxis, int32 Band,
		TArray<int16>& Output, TFunctionRef<bool()> IsCancelled);

private:
	static constexpr int32 UnitsPerVoxel = 1024;

	static int16 ProcessSample(TConstArrayView<int16> Input, int32 CenterIndex, int32 InputAxis, int32 Band);
	static int32 NearestDistance(int32 Squared, int32 BandQ);
};
