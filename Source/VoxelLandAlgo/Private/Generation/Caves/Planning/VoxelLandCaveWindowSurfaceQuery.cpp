#include "Generation/Caves/Planning/VoxelLandCaveWindowSurfaceQuery.h"

#include "Generation/Caves/Field/VoxelLandCaveBaselineWindow.h"
#include "Generation/Caves/Infrastructure/VoxelLandCaveBudget.h"
#include "Generation/Caves/Infrastructure/VoxelLandCaveBudgetLease.h"
#include "Generation/Caves/Infrastructure/VoxelLandCaveCheckedMath.h"
#include "Generation/Caves/Field/VoxelLandCaveProtectionGrid.h"
#include "Generation/Caves/Field/VoxelLandCaveProtectionSamples.h"
#include "Generation/Caves/Planning/VoxelLandCaveSurfaceQuery.h"

class FVoxelLandM5WindowSurfaceQueryInternals final
{
public:
	static FVoxelLandM5Diagnostic Error(EVoxelLandM5ErrorCode Code, const TCHAR* Message)
	{
		FVoxelLandM5Diagnostic Result;
		Result.Code = Code;
		Result.Stage = TEXT("M5.WindowSurfaceQuery");
		Result.Message = Message;
		return Result;
	}
};

FVoxelLandM5Diagnostic VoxelLand::M5::WindowSurfaceQuery::QueryFlat(const FVoxelLandM5BaselineWindow& Window,
	const FVoxelLandM5ProtectionGrid* Protection, bool bProtectAll, bool bDryWorld,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, int64 X, int64 Y, int32 RadiusCm,
	int64 MaxSamplesToInspect, TFunctionRef<bool()> IsCancelled, int64& OutSurfaceZ, int64& OutMinSurfaceZ,
	int64& OutMaxSurfaceZ, int32& OutSlopeDegrees, bool& bOutProtected, bool& bOutFlooded)
{
	using namespace VoxelLand::M5::CheckedMath;
	using E = EVoxelLandM5ErrorCode;
	if (IsCancelled())
	{
		return FVoxelLandM5WindowSurfaceQueryInternals::Error(E::Cancelled, TEXT("Window surface query cancelled"));
	}
	if (!Window.IsOwnedBy(*Budget) || (Protection && !Protection->IsOwnedBy(*Budget)))
	{
		return FVoxelLandM5WindowSurfaceQueryInternals::Error(
			E::InvalidInput, TEXT("Surface inputs belong to a different budget"));
	}
	if (!bDryWorld)
	{
		return FVoxelLandM5WindowSurfaceQueryInternals::Error(E::Unproven, TEXT("Dry world input is required"));
	}
	const FIntVector Size = Window.GetSize();
	const FVoxelLandM5PointCm Origin = Window.GetOriginCm();
	const int64 Spacing = Window.GetVoxelSizeCm();
	int64 Columns = 0;
	if (Size.X <= 0 || Size.Y <= 0 || Size.Z <= 0 || Spacing <= 0 || !TryMultiplyNonNegative(Size.X, Size.Y, Columns)
		|| Columns > MAX_int32)
	{
		return FVoxelLandM5WindowSurfaceQueryInternals::Error(
			E::InvalidInput, TEXT("Invalid baseline window dimensions"));
	}

	TSharedPtr<const FVoxelLandM5ProtectionSamples, ESPMode::ThreadSafe> Projected;
	if (Protection)
	{
		if (Protection->GetSpacingCm() != Spacing)
		{
			return FVoxelLandM5WindowSurfaceQueryInternals::Error(
				E::InvalidInput, TEXT("Protection and baseline spacing differ"));
		}
		int64 Delta = 0;
		int64 End = 0;
		if (!TrySubtract(Origin.Z, Protection->GetOriginCm().Z, Delta))
		{
			return FVoxelLandM5WindowSurfaceQueryInternals::Error(
				E::ArithmeticOverflow, TEXT("Protection vertical origin overflow"));
		}
		if (Delta % Spacing != 0)
		{
			return FVoxelLandM5WindowSurfaceQueryInternals::Error(
				E::InvalidInput, TEXT("Protection and baseline lattices differ"));
		}
		const int64 Start = Delta / Spacing;
		if (!TryAdd(Start, int64(Size.Z) - 1, End) || Start < 1 || End >= Protection->GetSize().Z)
		{
			return FVoxelLandM5WindowSurfaceQueryInternals::Error(
				E::OutOfDomain, TEXT("Protection grid lacks vertical incident cells"));
		}
		const FVoxelLandM5Diagnostic Result =
			Protection->ProjectColumns(FIntPoint(Size.X, Size.Y), Origin, Budget, IsCancelled, Projected);
		if (Result.Code != E::None)
		{
			return Result;
		}
	}
	int64 Bytes = 0;
	int64 Work = 0;
	if (!TryAdd(Columns, 4096, Bytes) || !TryAdd(Columns, 1, Work))
	{
		return FVoxelLandM5WindowSurfaceQueryInternals::Error(
			E::ArithmeticOverflow, TEXT("Protection column admission overflow"));
	}
	if (!Budget->TryChargeWork(Work))
	{
		return FVoxelLandM5WindowSurfaceQueryInternals::Error(
			E::BudgetExceeded, TEXT("Protection column work exhausted"));
	}
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Lease;
	if (!Budget->TryReserveBytes(Bytes, Lease))
	{
		return FVoxelLandM5WindowSurfaceQueryInternals::Error(
			E::BudgetExceeded, TEXT("Protection column memory exhausted"));
	}
	TArray<uint8> Flags;
	Flags.SetNumUninitialized(int32(Columns));
	const TConstArrayView<uint8> ProjectedFlags = Projected ? Projected->GetSamples() : TConstArrayView<uint8>();
	for (int32 Index = 0; Index < Columns; ++Index)
	{
		if ((Index & 1023) == 0 && IsCancelled())
		{
			return FVoxelLandM5WindowSurfaceQueryInternals::Error(E::Cancelled, TEXT("Window surface query cancelled"));
		}
		Flags[Index] = bProtectAll || (Projected && ProjectedFlags[Index] != 0) ? 1 : 0;
	}
	int64 Surface = 0, MinSurface = 0, MaxSurface = 0;
	int32 Slope = 0;
	bool bProtected = false, bFlooded = false;
	const FVoxelLandM5Diagnostic Result = VoxelLand::M5::SurfaceQuery::QueryFlatSurface(Budget, Size, Origin, Spacing,
		Window.GetDensity(), Flags, X, Y, RadiusCm, false, 0, MaxSamplesToInspect, IsCancelled, Surface, MinSurface,
		MaxSurface, Slope, bProtected, bFlooded);
	if (Result.Code != E::None)
	{
		return Result;
	}
	if (IsCancelled())
	{
		return FVoxelLandM5WindowSurfaceQueryInternals::Error(E::Cancelled, TEXT("Window surface query cancelled"));
	}
	OutSurfaceZ = Surface;
	OutMinSurfaceZ = MinSurface;
	OutMaxSurfaceZ = MaxSurface;
	OutSlopeDegrees = Slope;
	bOutProtected = bProtected;
	bOutFlooded = bFlooded;
	return {};
}
