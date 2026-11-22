#include "Generation/VoxelLandM5PocketMapping.h"

namespace VoxelLand::M5::PocketMapping
{
bool TryMap(
	bool bEnabled, int32 Sample, int32 ThresholdQ16, int32 AmplitudeCm, bool& bOutHasContribution, int64& OutDistanceQ8)
{
	if (Sample < -32768 || Sample > 32767 || ThresholdQ16 < 0 || ThresholdQ16 > 65535 || AmplitudeCm < 0
		|| AmplitudeCm > 1048576)
	{
		return false;
	}
	const int64 Excess = static_cast<int64>(Sample) - (static_cast<int64>(ThresholdQ16) - 32768);
	if (!bEnabled || AmplitudeCm == 0 || Excess <= 0)
	{
		bOutHasContribution = false;
		OutDistanceQ8 = 0;
		return true;
	}
	// Max numerator is 65535 * 1048576 * 256 < 2^44. Ceiling
	// preserves the sign even for a selected sample less than one Q8 unit in.
	const int64 Numerator = Excess * static_cast<int64>(AmplitudeCm) * 256;
	const int64 Magnitude = Numerator / 65535 + (Numerator % 65535 != 0 ? 1 : 0);
	bOutHasContribution = true;
	OutDistanceQ8 = -Magnitude;
	return true;
}
}
