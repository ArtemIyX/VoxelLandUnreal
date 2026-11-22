#include "Generation/VoxelLandM4Domain.h"

#include <limits>

namespace VoxelLandM4Domain
{
class FVoxelLandM4DomainInternals final
{
public:
	static constexpr int32 M4DomainMaxCells = 1024;
	static constexpr int64 M4DomainMaxSamples = 1050625;
	static FVoxelLandError M4DomainError(const TCHAR* Message);
	static bool M4DomainAdd(int64 A, int64 B, int64& Out);
	static bool M4DomainMultiplyPositive(int64 A, int64 B, int64& Out);
	static bool M4DomainEndpoint(int64 Origin, int32 Cells, int64 Spacing, int64& Out);
};

FVoxelLandError FVoxelLandM4DomainInternals::M4DomainError(const TCHAR* Message)
{
	FVoxelLandError Error;
	Error.Code = EVoxelLandErrorCode::InvalidParameter;
	Error.Message = Message;
	return Error;
}

bool FVoxelLandM4DomainInternals::M4DomainAdd(int64 A, int64 B, int64& Out)
{
	if ((B > 0 && A > std::numeric_limits<int64>::max() - B) || (B < 0 && A < std::numeric_limits<int64>::min() - B))
	{
		return false;
	}
	Out = A + B;
	return true;
}

bool FVoxelLandM4DomainInternals::M4DomainMultiplyPositive(int64 A, int64 B, int64& Out)
{
	if (B <= 0 || A > std::numeric_limits<int64>::max() / B || A < std::numeric_limits<int64>::min() / B)
	{
		return false;
	}
	Out = A * B;
	return true;
}

bool FVoxelLandM4DomainInternals::M4DomainEndpoint(int64 Origin, int32 Cells, int64 Spacing, int64& Out)
{
	int64 Extent = 0;
	return M4DomainMultiplyPositive(int64(Cells), Spacing, Extent) && M4DomainAdd(Origin, Extent, Out);
}

FVoxelLandError Validate(const FVoxelLandM4DomainSettings& Settings)
{
	if (Settings.Mode == EVoxelLandM4DomainMode::Unlimited)
	{
		return Settings.bGlobalErosion
				 ? FVoxelLandM4DomainInternals::M4DomainError(TEXT("Unlimited M4 worlds cannot use global erosion"))
				 : FVoxelLandError{};
	}
	if (Settings.Mode != EVoxelLandM4DomainMode::Finite)
	{
		return FVoxelLandM4DomainInternals::M4DomainError(TEXT("Unknown M4 domain mode"));
	}
	if (Settings.CellsX < 1 || Settings.CellsX > FVoxelLandM4DomainInternals::M4DomainMaxCells || Settings.CellsY < 1
		|| Settings.CellsY > FVoxelLandM4DomainInternals::M4DomainMaxCells || Settings.GridSpacingCm <= 0)
	{
		return FVoxelLandM4DomainInternals::M4DomainError(TEXT("Invalid M4 finite domain dimensions or spacing"));
	}
	const int64 SampleCount = (int64(Settings.CellsX) + 1) * (int64(Settings.CellsY) + 1);
	if (SampleCount > FVoxelLandM4DomainInternals::M4DomainMaxSamples)
	{
		return FVoxelLandM4DomainInternals::M4DomainError(TEXT("M4 finite domain exceeds sample limit"));
	}
	int64 EndX = 0;
	int64 EndY = 0;
	if (!FVoxelLandM4DomainInternals::M4DomainEndpoint(
			Settings.OriginXCm, Settings.CellsX, Settings.GridSpacingCm, EndX)
		|| !FVoxelLandM4DomainInternals::M4DomainEndpoint(
			Settings.OriginYCm, Settings.CellsY, Settings.GridSpacingCm, EndY))
	{
		return FVoxelLandM4DomainInternals::M4DomainError(TEXT("M4 finite domain endpoint overflows"));
	}
	return {};
}

FVoxelLandError ValidateFootprint(
	const FVoxelLandM4DomainSettings& Settings, const FVoxelLandM4DomainFootprint& Footprint)
{
	if (const FVoxelLandError Error = Validate(Settings))
	{
		return Error;
	}
	if (Footprint.MinX > Footprint.MaxX || Footprint.MinY > Footprint.MaxY || Footprint.VoxelSizeCm <= 0
	    || Footprint.PaddingVoxels < 0 || Footprint.FilterHaloVoxels < 0 || Footprint.SourceMarginVoxels < 0)
	{
		return FVoxelLandM4DomainInternals::M4DomainError(TEXT("Invalid M4 sample footprint or margin"));
	}
	const int64 Margin = int64(Footprint.PaddingVoxels) + Footprint.FilterHaloVoxels + Footprint.SourceMarginVoxels;
	int64 MinX = 0, MaxX = 0, MinY = 0, MaxY = 0;
	if (!FVoxelLandM4DomainInternals::M4DomainAdd(Footprint.MinX, -Margin, MinX)
		|| !FVoxelLandM4DomainInternals::M4DomainAdd(Footprint.MaxX, Margin, MaxX)
		|| !FVoxelLandM4DomainInternals::M4DomainAdd(Footprint.MinY, -Margin, MinY)
		|| !FVoxelLandM4DomainInternals::M4DomainAdd(Footprint.MaxY, Margin, MaxY)
		|| !FVoxelLandM4DomainInternals::M4DomainMultiplyPositive(MinX, Footprint.VoxelSizeCm, MinX)
		|| !FVoxelLandM4DomainInternals::M4DomainMultiplyPositive(MaxX, Footprint.VoxelSizeCm, MaxX)
		|| !FVoxelLandM4DomainInternals::M4DomainMultiplyPositive(MinY, Footprint.VoxelSizeCm, MinY)
		|| !FVoxelLandM4DomainInternals::M4DomainMultiplyPositive(MaxY, Footprint.VoxelSizeCm, MaxY))
	{
		return FVoxelLandM4DomainInternals::M4DomainError(TEXT("M4 sample footprint arithmetic overflows"));
	}
	if (Settings.Mode == EVoxelLandM4DomainMode::Finite)
	{
		int64 EndX = 0, EndY = 0;
		if (!FVoxelLandM4DomainInternals::M4DomainEndpoint(
				Settings.OriginXCm, Settings.CellsX, Settings.GridSpacingCm, EndX)
			|| !FVoxelLandM4DomainInternals::M4DomainEndpoint(
				Settings.OriginYCm, Settings.CellsY, Settings.GridSpacingCm, EndY)
			|| MinX < Settings.OriginXCm || MaxX > EndX || MinY < Settings.OriginYCm || MaxY > EndY)
		{
			return FVoxelLandM4DomainInternals::M4DomainError(
				TEXT("M4 sample footprint extends outside finite domain"));
		}
	}
	return {};
}
}