#include "Generation/VoxelLandM4HeightPrepass.h"

#include "Generation/VoxelLandM4CurveLut.h"
#include "Generation/VoxelLandM4Domain.h"
#include "Generation/VoxelLandM4Prepass.h"

namespace VoxelLandM4HeightPrepass
{
namespace
{
FVoxelLandError M4HeightPrepassError(EVoxelLandErrorCode Code, const TCHAR* Message)
{
	FVoxelLandError Error;
	Error.Code = Code;
	Error.Message = Message;
	return Error;
}
}

FVoxelLandError BuildBaseHeights(const FVoxelLandM4DomainSettings& Domain, TConstArrayView<uint16> Continental,
	const FVoxelLandM4CurveLut& ContinentalCurve, TConstArrayView<uint16> Mountain,
	const FVoxelLandM4CurveLut& MountainCurve, TConstArrayView<uint16> Peaks, const FVoxelLandM4CurveLut& PeaksCurve,
	int32 BaseQ8, TArray<int32>& OutHeights, TFunctionRef<bool()> IsCancelled)
{
	if (const FVoxelLandError Error = VoxelLandM4Domain::Validate(Domain))
	{
		return Error;
	}
	if (Domain.Mode != EVoxelLandM4DomainMode::Finite)
	{
		return M4HeightPrepassError(
			EVoxelLandErrorCode::InvalidParameter, TEXT("M4 height prepass requires a finite domain"));
	}
	const int64 Width = int64(Domain.CellsX) + 1;
	const int64 Count = Width * (int64(Domain.CellsY) + 1);
	if (Continental.Num() != Count || Mountain.Num() != Count || Peaks.Num() != Count)
	{
		return M4HeightPrepassError(
			EVoxelLandErrorCode::InvalidParameter, TEXT("M4 control map sizes must match the domain"));
	}
	if (IsCancelled())
	{
		return M4HeightPrepassError(EVoxelLandErrorCode::Cancelled, TEXT("M4 height prepass cancelled"));
	}
	TArray<int32> Heights;
	Heights.Reserve(int32(Count));
	for (int32 Y = 0; Y <= Domain.CellsY; ++Y)
	{
		if (IsCancelled())
		{
			return M4HeightPrepassError(EVoxelLandErrorCode::Cancelled, TEXT("M4 height prepass cancelled"));
		}
		for (int32 X = 0; X <= Domain.CellsX; ++X)
		{
			const int32 Index = Y * int32(Width) + X;
			int64 Height = int64(BaseQ8);
			Height += int64(ContinentalCurve.Evaluate(Continental[Index]));
			Height += int64(MountainCurve.Evaluate(Mountain[Index]));
			Height += int64(PeaksCurve.Evaluate(Peaks[Index]));
			if (Height < -512000000 || Height > 512000000)
			{
				return M4HeightPrepassError(
					EVoxelLandErrorCode::InvalidMathDomain, TEXT("M4 base height exceeds Q8 range"));
			}
			Heights.Add(int32(Height));
		}
	}
	if (IsCancelled())
	{
		return M4HeightPrepassError(EVoxelLandErrorCode::Cancelled, TEXT("M4 height prepass cancelled"));
	}
	OutHeights = MoveTemp(Heights);
	return {};
}

FVoxelLandError Generate(const FVoxelLandM4DomainSettings& Domain, TConstArrayView<uint16> Continental,
	const FVoxelLandM4CurveLut& ContinentalCurve, TConstArrayView<uint16> Mountain,
	const FVoxelLandM4CurveLut& MountainCurve, TConstArrayView<uint16> Peaks, const FVoxelLandM4CurveLut& PeaksCurve,
	int32 BaseQ8, uint64 WorldSeed, const FGuid& StageId, uint32 Salt, int32 DropletCount,
	const FVoxelLandM4ErosionSettings& Erosion, TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe>& OutGrid,
	uint64& OutHash, TFunctionRef<bool()> IsCancelled)
{
	TArray<int32> BaseHeights;
	if (const FVoxelLandError Error = BuildBaseHeights(Domain, Continental, ContinentalCurve, Mountain, MountainCurve,
			Peaks, PeaksCurve, BaseQ8, BaseHeights, IsCancelled))
	{
		return Error;
	}
	return VoxelLandM4Prepass::Generate(
		Domain, BaseHeights, WorldSeed, StageId, Salt, DropletCount, Erosion, OutGrid, OutHash, IsCancelled);
}
}
