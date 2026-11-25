#include "VoxelLandHeightGraphStageMap.h"
#include "Generation/Terrain/Heightfield/VoxelLandHeightGrid.h"

bool VoxelLandM4StageMap::Build(
	const FVoxelLandM4HeightGrid& Grid, TFunctionRef<bool()> IsCancelled, TArray<uint8>& OutPixels)
{
	int64 MinX, MinY, MaxX, MaxY;
	int32 MinHeight, MaxHeight;
	Grid.GetBoundsCm(MinX, MinY, MaxX, MaxY);
	Grid.GetHeightRangeQ8(MinHeight, MaxHeight);
	// HeightGrid restricts spacing squared times 512000000 to int64 and
	// each axis to 1024 cells. Thus each extent times 127 fits in int64;
	// every computed position remains within its already-validated endpoints.
	const int64 SpanX = MaxX - MinX;
	const int64 SpanY = MaxY - MinY;
	const int64 HeightSpan = int64(MaxHeight) - MinHeight;
	TArray<uint8> Pixels;
	Pixels.SetNumUninitialized(Resolution * Resolution * 4);
	for (int32 Y = 0; Y < Resolution; ++Y)
	{
		if (IsCancelled())
		{
			return false;
		}
		const int64 WorldY = MaxY - (SpanY * Y / (Resolution - 1));
		for (int32 X = 0; X < Resolution; ++X)
		{
			const int64 WorldX = MinX + (SpanX * X / (Resolution - 1));
			int32 Height;
			if (!Grid.Sample(WorldX, WorldY, Height))
			{
				return false;
			}
			const uint8 Gray =
				HeightSpan == 0 ? 128 : uint8(((int64(Height) - MinHeight) * 255 + HeightSpan / 2) / HeightSpan);
			const int32 Offset = (Y * Resolution + X) * 4;
			Pixels[Offset] = Pixels[Offset + 1] = Pixels[Offset + 2] = Gray;
			Pixels[Offset + 3] = 255;
		}
	}
	if (IsCancelled())
	{
		return false;
	}
	OutPixels = MoveTemp(Pixels);
	return true;
}
