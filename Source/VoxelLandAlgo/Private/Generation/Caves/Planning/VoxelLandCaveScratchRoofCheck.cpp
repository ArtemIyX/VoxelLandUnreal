#include "Generation/Caves/Planning/VoxelLandCaveScratchRoofCheck.h"

#include "Data/Caves/Planning/VoxelLandCaveSettings.h"
#include "Data/Caves/Field/VoxelLandCaveChunkFootprintRecord.h"
#include "Data/Caves/Persistence/VoxelLandCaveStageManifest.h"
#include "Generation/Caves/Field/VoxelLandCaveBaselineWindow.h"
#include "Generation/Caves/Infrastructure/VoxelLandCaveBudget.h"
#include "Generation/Caves/Infrastructure/VoxelLandCaveBudgetLease.h"
#include "Generation/Caves/Planning/VoxelLandCaveCandidateResult.h"
#include "Generation/Caves/Infrastructure/VoxelLandCaveCheckedMath.h"
#include "Generation/Caves/Field/VoxelLandCaveChunkFootprint.h"
#include "Generation/Caves/Field/VoxelLandCaveFieldSafety.h"
#include "Generation/Caves/Field/VoxelLandCaveProtectionGrid.h"
#include "Generation/Caves/Field/VoxelLandCaveScratchChunks.h"

class FVoxelLandM5ScratchRoofCheckInternals final
{
public:
	static FVoxelLandM5Diagnostic Error(EVoxelLandM5ErrorCode Code, const TCHAR* Message)
	{
		FVoxelLandM5Diagnostic Result;
		Result.Code = Code;
		Result.Stage = TEXT("M5.ScratchRoof");
		Result.Message = Message;
		return Result;
	}

	static int64 Axis(const FVoxelLandM5PointCm& Point, int32 Index)
	{
		return Index == 0 ? Point.X : Index == 1 ? Point.Y : Point.Z;
	}

	static bool ValidOpening(const FVoxelLandM5Entrance& Entrance)
	{
		if (!Entrance.bHasId || !Entrance.bHasConnectionId || Entrance.bFlooded)
		{
			return false;
		}
		for (int32 AxisIndex = 0; AxisIndex < 3; ++AxisIndex)
		{
			const int64 Min = Axis(Entrance.OpeningBoundsCm.MinInclusive, AxisIndex);
			const int64 Max = Axis(Entrance.OpeningBoundsCm.MaxExclusive, AxisIndex);
			if (Min < -1048576 || Max > 1048577 || Min >= Max)
			{
				return false;
			}
		}
		return true;
	}
};

FVoxelLandM5Diagnostic VoxelLand::M5::ScratchRoofCheck::Check(const FVoxelLandM5CaveSettings& Settings, int64 WorldSeed,
	const FVoxelLandM5StageManifest& Manifest,
	TSharedRef<const FVoxelLandM5BaselineWindow, ESPMode::ThreadSafe> Baseline,
	TSharedRef<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> Candidate,
	TSharedPtr<const FVoxelLandM5ProtectionGrid, ESPMode::ThreadSafe> Protection,
	TSharedRef<const FVoxelLandM5ScratchChunks, ESPMode::ThreadSafe> Chunks,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled)
{
	return Check(Settings, WorldSeed, Manifest, Baseline,
		TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe>(Candidate), Protection, Chunks, Budget,
		IsCancelled);
}

FVoxelLandM5Diagnostic VoxelLand::M5::ScratchRoofCheck::Check(const FVoxelLandM5CaveSettings& Settings, int64 WorldSeed,
	const FVoxelLandM5StageManifest& Manifest,
	TSharedRef<const FVoxelLandM5BaselineWindow, ESPMode::ThreadSafe> Baseline,
	TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> Candidate,
	TSharedPtr<const FVoxelLandM5ProtectionGrid, ESPMode::ThreadSafe> Protection,
	TSharedRef<const FVoxelLandM5ScratchChunks, ESPMode::ThreadSafe> Chunks,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled)
{
	using namespace VoxelLand::M5::CheckedMath;
	using E = EVoxelLandM5ErrorCode;
	using I = FVoxelLandM5ScratchRoofCheckInternals;
	if (IsCancelled())
	{
		return I::Error(E::Cancelled, TEXT("Roof check cancelled"));
	}
	const int64 Spacing = Baseline->GetVoxelSizeCm();
	if (Settings.PlanningMode != EVoxelLandM5PlanningMode::Finite || Settings.RoofThicknessCm < 0 || Spacing <= 0
		|| Chunks->GetVoxelSizeCm() != Spacing || !Baseline->IsOwnedBy(*Budget) || !Chunks->IsOwnedBy(*Budget)
		|| (Candidate && !Candidate->UsesBudget(*Budget)) || Settings.bEnabled != Candidate.IsValid()
		|| (!Settings.bEnabled && !Settings.Pockets.bEnabled) || (Protection && !Protection->IsOwnedBy(*Budget)))
	{
		return I::Error(E::InvalidInput, TEXT("Invalid roof inputs or budget ownership"));
	}
	if (Settings.Water.Policy != EVoxelLandM5WaterPolicy::DryWorld || (Settings.Protection.bRequireMask && !Protection))
	{
		return I::Error(E::Unproven, TEXT("Dry world and required protection input are required"));
	}
	if (!Budget->TryChargeWork(128))
	{
		return I::Error(E::BudgetExceeded, TEXT("Roof source validation work exhausted"));
	}
	if (!Chunks->MatchesDependencies(Baseline.Get(), Candidate.Get(), Protection.Get(), WorldSeed, Manifest)
		|| !Chunks->MatchesSettings(Settings)
		|| (Candidate && !Candidate->MatchesSource(Settings, WorldSeed, Candidate->GetPlan().Region)))
	{
		return I::Error(E::InvalidInput, TEXT("Roof sources do not match scratch composition"));
	}
	const TConstArrayView<FVoxelLandM5Entrance> Entrances =
		Candidate ? TConstArrayView<FVoxelLandM5Entrance>(Candidate->GetPlan().Entrances)
				  : TConstArrayView<FVoxelLandM5Entrance>();
	if (Entrances.Num() > 4096 || (Candidate && Entrances.Num() != Settings.Topology.EntranceCount))
	{
		return I::Error(E::Unproven, TEXT("Candidate entrance set is incomplete"));
	}
	const int64 Radius = (int64(Settings.RoofThicknessCm) + Spacing - 1) / Spacing + 2;
	int64 Side = 0, NeighborXY = 0, Neighbors = 0, Scan = 0, Openings = 0, PerCell = 0, Work = 0;
	constexpr int64 Axis = FVoxelLandChunkData::Cells;
	constexpr int64 Cells = Axis * Axis * Axis;
	constexpr int64 SampleAxis = FVoxelLandChunkData::SamplesPerAxis;
	constexpr int64 Samples = FVoxelLandChunkData::SampleCount;
	const auto ChunkView = Chunks->GetChunks();
	if (ChunkView.IsEmpty())
	{
		return I::Error(E::InvalidInput, TEXT("Scratch chunk rectangle is empty"));
	}
	if (!TryMultiplyNonNegative(Radius, 2, Side) || !TryAdd(Side, 1, Side)
		|| !TryMultiplyNonNegative(Side, Side, NeighborXY) || !TryMultiplyNonNegative(NeighborXY, Side, Neighbors)
		|| !TryMultiplyNonNegative(Neighbors, 10, Scan) || !TryMultiplyNonNegative(Entrances.Num(), 16, Openings)
		|| !TryAdd(Scan, Openings, PerCell) || !TryAdd(PerCell, 80, PerCell)
		|| !TryMultiplyNonNegative(PerCell, Cells, Work) || !TryAdd(Work, Samples + 1, Work)
		|| !TryMultiplyNonNegative(Work, ChunkView.Num(), Work))
	{
		return I::Error(E::ArithmeticOverflow, TEXT("Roof check work overflow"));
	}
	if (!Budget->TryChargeWork(Work))
	{
		return I::Error(E::BudgetExceeded, TEXT("Roof check work exhausted"));
	}
	for (const FVoxelLandM5Entrance& Entrance : Entrances)
	{
		if (IsCancelled())
		{
			return I::Error(E::Cancelled, TEXT("Roof check cancelled"));
		}
		if (!I::ValidOpening(Entrance))
		{
			return I::Error(E::Unproven, TEXT("Candidate opening bounds are invalid"));
		}
	}
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Lease;
	if (!Budget->TryReserveBytes(2 * Samples * int64(sizeof(int16)) + 6 * Cells + 4096, Lease))
	{
		return I::Error(E::BudgetExceeded, TEXT("Roof scratch admission exhausted"));
	}
	TArray<int16> Original;
	TArray<uint8> Roof, EntranceCells, Empty;
	Original.SetNumUninitialized(int32(Samples));
	Roof.SetNumUninitialized(int32(Cells));
	EntranceCells.SetNumUninitialized(int32(Cells));
	Empty.SetNumUninitialized(int32(Cells));
	const FIntVector Size = Baseline->GetSize();
	const FVoxelLandM5PointCm Origin = Baseline->GetOriginCm();
	const auto Values = Baseline->GetDensity();
	int64 XY = 0, Total = 0;
	if (Size.X <= 0 || Size.Y <= 0 || Size.Z <= 0 || !TryMultiplyNonNegative(Size.X, Size.Y, XY)
		|| !TryMultiplyNonNegative(XY, Size.Z, Total) || Total != Values.Num())
	{
		return I::Error(E::InvalidInput, TEXT("Invalid baseline window"));
	}
	int64 Visits = 0;
	for (const FVoxelLandChunkData& Chunk : ChunkView)
	{
		if (IsCancelled())
		{
			return I::Error(E::Cancelled, TEXT("Roof check cancelled"));
		}
		FVoxelLandM5ChunkFootprint Footprint;
		if (Chunk.Key.LOD != 0 || Chunk.Density.Num() != Samples
			|| !VoxelLand::M5::ChunkFootprint::TryBuild(Chunk.Key, int32(Spacing), false, false, Footprint))
		{
			return I::Error(E::InvalidInput, TEXT("Invalid scratch chunk"));
		}
		int64 Start[3] = {};
		for (int32 A = 0; A < 3; ++A)
		{
			int64 Delta = 0, Low = 0, High = 0;
			if (!TrySubtract(I::Axis(Footprint.StoredMin, A), I::Axis(Origin, A), Delta) || Delta < 0
				|| Delta % Spacing != 0 || !TryAdd(Delta / Spacing, 1, Start[A]) || !TrySubtract(Start[A], Radius, Low)
				|| !TryAdd(Start[A], Axis - 1, High) || !TryAdd(High, Radius + 1, High) || Low < 0 || High >= Size[A]
				|| Start[A] < 1 || Start[A] + Axis + 1 >= Size[A])
			{
				return I::Error(E::OutOfDomain, TEXT("Baseline roof search halo is missing"));
			}
		}
		for (int64 Z = 0; Z < SampleAxis; ++Z)
		{
			for (int64 Y = 0; Y < SampleAxis; ++Y)
			{
				for (int64 X = 0; X < SampleAxis; ++X)
				{
					const int64 Index = X + SampleAxis * (Y + SampleAxis * Z);
					if ((Index & 1023) == 0 && IsCancelled())
					{
						return I::Error(E::Cancelled, TEXT("Roof check cancelled"));
					}
					const int64 Source =
						Start[0] - 1 + X + int64(Size.X) * (Start[1] - 1 + Y) + XY * (Start[2] - 1 + Z);
					Original[int32(Index)] = Values[int32(Source)];
				}
			}
		}
		for (int64 Z = 0; Z < Axis; ++Z)
		{
			for (int64 Y = 0; Y < Axis; ++Y)
			{
				for (int64 X = 0; X < Axis; ++X)
				{
					const int64 Index = X + Axis * (Y + Axis * Z);
					const int64 Cell[3] = {Start[0] + X, Start[1] + Y, Start[2] + Z};
					bool bRoof = false;
					for (int64 DZ = -Radius; DZ <= Radius; ++DZ)
					{
						for (int64 DY = -Radius; DY <= Radius; ++DY)
						{
							for (int64 DX = -Radius; DX <= Radius; ++DX)
							{
								if ((Visits++ & 63) == 0 && IsCancelled())
								{
									return I::Error(E::Cancelled, TEXT("Roof check cancelled"));
								}
								bool bSolid = false, bAir = false, bZero = false;
								for (int64 K = 0; K < 2; ++K)
								{
									for (int64 J = 0; J < 2; ++J)
									{
										for (int64 H = 0; H < 2; ++H)
										{
											const int64 Source = Cell[0] + DX + H + int64(Size.X) * (Cell[1] + DY + J)
															   + XY * (Cell[2] + DZ + K);
											const int16 Q = Values[int32(Source)];
											bSolid |= Q < 0;
											bAir |= Q >= 0;
											bZero |= Q == 0;
										}
									}
								}
								// Zero is air to the mesher, but protect all zero-containing
								// cells even when all eight samples are nonnegative.
								bRoof |= (bSolid && bAir) || bZero;
							}
						}
					}
					Roof[int32(Index)] = bRoof ? 1 : 0;
					Empty[int32(Index)] = 0;
					bool bInside = false;
					int64 Min[3] = {}, Max[3] = {};
					for (int32 A = 0; A < 3; ++A)
					{
						int64 Delta = 0, Position = 0;
						if (!TryMultiply(Cell[A], Spacing, Delta) || !TryAdd(I::Axis(Origin, A), Delta, Position)
							|| !TrySubtract(Position, Spacing, Min[A]) || !TryAdd(Position, 2 * Spacing, Max[A]))
						{
							return I::Error(E::ArithmeticOverflow, TEXT("Roof influence coordinates overflow"));
						}
					}
					for (const FVoxelLandM5Entrance& Opening : Entrances)
					{
						bool bContains = true;
						for (int32 A = 0; A < 3; ++A)
						{
							bContains &= Min[A] >= I::Axis(Opening.OpeningBoundsCm.MinInclusive, A)
									  && Max[A] < I::Axis(Opening.OpeningBoundsCm.MaxExclusive, A);
						}
						bInside |= bContains;
					}
					EntranceCells[int32(Index)] = bInside ? 1 : 0;
				}
			}
		}
		const FVoxelLandM5Diagnostic Result = VoxelLand::M5::FieldSafety::CheckUnchangedProtectedCells(
			FIntVector(int32(Axis)), Original, Chunk.Density, Empty, Roof, EntranceCells, Cells * 64, IsCancelled);
		if (Result.Code != E::None)
		{
			return Result;
		}
	}
	return IsCancelled() ? I::Error(E::Cancelled, TEXT("Roof check cancelled")) : FVoxelLandM5Diagnostic{};
}
