#include "Generation/VoxelLandM5ScratchProtectionCheck.h"

#include "Data/Structs/VoxelLandM5ChunkFootprint.h"
#include "Generation/VoxelLandM5BaselineWindow.h"
#include "Generation/VoxelLandM5Budget.h"
#include "Generation/VoxelLandM5BudgetLease.h"
#include "Generation/VoxelLandM5CheckedMath.h"
#include "Generation/VoxelLandM5ChunkFootprint.h"
#include "Generation/VoxelLandM5FieldSafety.h"
#include "Generation/VoxelLandM5ProtectionGrid.h"
#include "Generation/VoxelLandM5ScratchChunks.h"

class FVoxelLandM5ScratchProtectionCheckInternals final
{
public:
	static FVoxelLandM5Diagnostic Error(EVoxelLandM5ErrorCode Code, const TCHAR* Message)
	{
		FVoxelLandM5Diagnostic Result;
		Result.Code = Code;
		Result.Stage = TEXT("M5.ScratchProtection");
		Result.Message = Message;
		return Result;
	}
};

FVoxelLandM5Diagnostic VoxelLand::M5::ScratchProtectionCheck::Check(
	const FVoxelLandM5BaselineWindow& Baseline, const FVoxelLandM5ScratchChunks& Chunks,
	const FVoxelLandM5ProtectionGrid* Protection, bool bRequireMask, bool bProtectAll,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled)
{
	using namespace VoxelLand::M5::CheckedMath;
	using E = EVoxelLandM5ErrorCode;
	if (IsCancelled())
	{
		return FVoxelLandM5ScratchProtectionCheckInternals::Error(E::Cancelled, TEXT("Protection check cancelled"));
	}
	const int64 Spacing = Baseline.GetVoxelSizeCm();
	if (Spacing <= 0 || Chunks.GetVoxelSizeCm() != Spacing || !Baseline.IsOwnedBy(*Budget)
		|| !Chunks.IsOwnedBy(*Budget) || (Protection && (!Protection->IsOwnedBy(*Budget)
			|| Protection->GetSpacingCm() != Spacing)))
	{
		return FVoxelLandM5ScratchProtectionCheckInternals::Error(E::InvalidInput, TEXT("Protection inputs have different budgets or spacing"));
	}
	if (bRequireMask && !Protection)
	{
		return FVoxelLandM5ScratchProtectionCheckInternals::Error(E::Unproven, TEXT("Required protection grid is missing"));
	}
	if (!Chunks.MatchesProtectionInputs(Baseline, Protection, bRequireMask, bProtectAll))
	{
		return FVoxelLandM5ScratchProtectionCheckInternals::Error(
			E::InvalidInput, TEXT("Protection inputs do not match scratch chunk sources"));
	}
	constexpr int64 Axis = FVoxelLandChunkData::Cells;
	constexpr int64 CellCount = Axis * Axis * Axis;
	constexpr int64 SampleAxis = FVoxelLandChunkData::SamplesPerAxis;
	constexpr int64 SampleCount = FVoxelLandChunkData::SampleCount;
	const TConstArrayView<FVoxelLandChunkData> ChunkView = Chunks.GetChunks();
	int64 Work = 0;
	if (!TryMultiplyNonNegative(ChunkView.Num(), CellCount * 70 + SampleCount + 1, Work))
	{
		return FVoxelLandM5ScratchProtectionCheckInternals::Error(E::ArithmeticOverflow, TEXT("Protection work overflow"));
	}
	if (!Budget->TryChargeWork(Work))
	{
		return FVoxelLandM5ScratchProtectionCheckInternals::Error(E::BudgetExceeded, TEXT("Protection work budget exhausted"));
	}
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Lease;
	if (!Budget->TryReserveBytes(SampleCount * int64(sizeof(int16)) * 2 + CellCount * 4 + 4096, Lease))
	{
		return FVoxelLandM5ScratchProtectionCheckInternals::Error(E::BudgetExceeded, TEXT("Protection scratch budget exhausted"));
	}
	TArray<int16> BaselineSamples;
	TArray<uint8> Protected;
	TArray<uint8> Empty;
	BaselineSamples.SetNumUninitialized(int32(SampleCount));
	Protected.SetNumUninitialized(int32(CellCount));
	Empty.SetNumUninitialized(int32(CellCount));
	const FIntVector WindowSize = Baseline.GetSize();
	const FVoxelLandM5PointCm WindowOrigin = Baseline.GetOriginCm();
	const TConstArrayView<int16> WindowSamples = Baseline.GetDensity();
	int64 WindowXY = 0, WindowCount = 0;
	if (WindowSize.X <= 0 || WindowSize.Y <= 0 || WindowSize.Z <= 0
		|| !TryMultiplyNonNegative(WindowSize.X, WindowSize.Y, WindowXY)
		|| !TryMultiplyNonNegative(WindowXY, WindowSize.Z, WindowCount) || WindowCount != WindowSamples.Num())
	{
		return FVoxelLandM5ScratchProtectionCheckInternals::Error(E::InvalidInput, TEXT("Invalid baseline grid"));
	}
	for (const FVoxelLandChunkData& Chunk : ChunkView)
	{
		if (IsCancelled())
		{
			return FVoxelLandM5ScratchProtectionCheckInternals::Error(E::Cancelled, TEXT("Protection check cancelled"));
		}
		FVoxelLandM5ChunkFootprint Footprint;
		if (Chunk.Key.LOD != 0 || Chunk.Density.Num() != SampleCount
			|| !VoxelLand::M5::ChunkFootprint::TryBuild(Chunk.Key, int32(Spacing), false, false, Footprint))
		{
			return FVoxelLandM5ScratchProtectionCheckInternals::Error(E::InvalidInput, TEXT("Invalid scratch chunk"));
		}
		const int64 Stored[] = {Footprint.StoredMin.X, Footprint.StoredMin.Y, Footprint.StoredMin.Z};
		const int64 WindowMin[] = {WindowOrigin.X, WindowOrigin.Y, WindowOrigin.Z};
		int64 Start[3] = {}, GridStart[3] = {};
		const FIntVector GridSize = Protection ? Protection->GetSize() : FIntVector::ZeroValue;
		const FVoxelLandM5PointCm GridOrigin = Protection ? Protection->GetOriginCm() : FVoxelLandM5PointCm{};
		const int64 GridMin[] = {GridOrigin.X, GridOrigin.Y, GridOrigin.Z};
		for (int32 A = 0; A < 3; ++A)
		{
			int64 Delta = 0, End = 0;
			if (!TrySubtract(Stored[A], WindowMin[A], Delta) || Delta < 0 || Delta % Spacing != 0
				|| !TryAdd(Delta / Spacing, SampleAxis, End) || End > WindowSize[A])
			{
				return FVoxelLandM5ScratchProtectionCheckInternals::Error(E::OutOfDomain, TEXT("Baseline chunk halo is missing"));
			}
			Start[A] = Delta / Spacing;
			if (Protection)
			{
				int64 OwnedMin = 0;
				if (!TryAdd(Stored[A], Spacing, OwnedMin) || !TrySubtract(OwnedMin, GridMin[A], Delta)
					|| Delta < 0 || Delta % Spacing != 0 || !TryAdd(Delta / Spacing, Axis, End)
					|| End > GridSize[A])
				{
					return FVoxelLandM5ScratchProtectionCheckInternals::Error(E::OutOfDomain, TEXT("Protection grid lacks owned cells"));
				}
				GridStart[A] = Delta / Spacing;
			}
		}
		for (int64 Z = 0; Z < SampleAxis; ++Z)
		{
			for (int64 Y = 0; Y < SampleAxis; ++Y)
			{
				for (int64 X = 0; X < SampleAxis; ++X)
				{
					const int64 I = X + SampleAxis * (Y + SampleAxis * Z);
					if ((I & 1023) == 0 && IsCancelled())
					{
						return FVoxelLandM5ScratchProtectionCheckInternals::Error(E::Cancelled, TEXT("Protection check cancelled"));
					}
					const int64 Source = Start[0] + X + int64(WindowSize.X) * (Start[1] + Y)
						+ WindowXY * (Start[2] + Z);
					BaselineSamples[int32(I)] = WindowSamples[int32(Source)];
				}
			}
		}
		const TConstArrayView<uint8> GridCells = Protection ? Protection->GetCells() : TConstArrayView<uint8>();
		for (int64 Z = 0; Z < Axis; ++Z)
		{
			for (int64 Y = 0; Y < Axis; ++Y)
			{
				for (int64 X = 0; X < Axis; ++X)
				{
					const int64 I = X + Axis * (Y + Axis * Z);
					if ((I & 1023) == 0 && IsCancelled())
					{
						return FVoxelLandM5ScratchProtectionCheckInternals::Error(E::Cancelled, TEXT("Protection check cancelled"));
					}
					const int64 GI = GridStart[0] + X + int64(GridSize.X) * (GridStart[1] + Y)
						+ int64(GridSize.X) * GridSize.Y * (GridStart[2] + Z);
					Protected[int32(I)] = bProtectAll || (Protection && GridCells[int32(GI)] != 0) ? 1 : 0;
					Empty[int32(I)] = 0;
				}
			}
		}
		const FVoxelLandM5Diagnostic Result = VoxelLand::M5::FieldSafety::CheckUnchangedProtectedCells(
			FIntVector(int32(Axis)), BaselineSamples, Chunk.Density, Protected, Empty, Empty, CellCount * 64, IsCancelled);
		if (Result.Code != E::None)
		{
			return Result;
		}
	}
	return IsCancelled() ? FVoxelLandM5ScratchProtectionCheckInternals::Error(E::Cancelled, TEXT("Protection check cancelled"))
						 : FVoxelLandM5Diagnostic{};
}
