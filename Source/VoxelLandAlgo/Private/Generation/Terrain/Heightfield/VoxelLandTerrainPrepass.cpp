#include "Generation/Terrain/Heightfield/VoxelLandTerrainPrepass.h"

#include "Data/Terrain/Domains/VoxelLandTerrainLatticeGeometry.h"
#include "Generation/Terrain/Domains/VoxelLandTerrainDomain.h"
#include "Generation/Terrain/Erosion/VoxelLandErosion.h"
#include "Generation/Terrain/Erosion/VoxelLandErosionRandom.h"
#include "Generation/Terrain/Heightfield/VoxelLandHeightGrid.h"
#include "Hash/xxhash.h"

namespace VoxelLandM4Prepass
{
class FVoxelLandM4PrepassInternals final
{
public:
	static FVoxelLandError M4PrepassError(EVoxelLandErrorCode Code, const TCHAR* Message)
	{
		FVoxelLandError Error;
		Error.Code = Code;
		Error.Message = Message;
		return Error;
	}

	static void M4PrepassWrite32(TArray<uint8>& Bytes, uint32 Value)
	{
		for (int32 Shift = 0; Shift < 32; Shift += 8)
		{
			Bytes.Add(uint8(Value >> Shift));
		}
	}

	static void M4PrepassWrite64(TArray<uint8>& Bytes, uint64 Value)
	{
		for (int32 Shift = 0; Shift < 64; Shift += 8)
		{
			Bytes.Add(uint8(Value >> Shift));
		}
	}
};

FVoxelLandError Generate(const FVoxelLandM4DomainSettings& Domain, TConstArrayView<int32> BaseHeightsQ8,
	uint64 WorldSeed, const FGuid& StageId, uint32 Salt, int32 DropletCount, const FVoxelLandM4ErosionSettings& Erosion,
	TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe>& OutGrid, uint64& OutHash,
	TFunctionRef<bool()> IsCancelled)
{
	return Generate(
		Domain, BaseHeightsQ8, WorldSeed, StageId, Salt, DropletCount, Erosion, OutGrid, OutHash, IsCancelled, nullptr);
}

FVoxelLandError Generate(const FVoxelLandM4DomainSettings& Domain, TConstArrayView<int32> BaseHeightsQ8,
	uint64 WorldSeed, const FGuid& StageId, uint32 Salt, int32 DropletCount, const FVoxelLandM4ErosionSettings& Erosion,
	TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe>& OutGrid, uint64& OutHash,
	TFunctionRef<bool()> IsCancelled, TSharedPtr<const FVoxelLandM4GridLease, ESPMode::ThreadSafe> Lease)
{
	return Generate(Domain, BaseHeightsQ8, WorldSeed, StageId, Salt, DropletCount, Erosion, OutGrid, OutHash,
		IsCancelled, MoveTemp(Lease), nullptr);
}

FVoxelLandError Generate(const FVoxelLandM4DomainSettings& Domain, TConstArrayView<int32> BaseHeightsQ8,
	uint64 WorldSeed, const FGuid& StageId, uint32 Salt, int32 DropletCount, const FVoxelLandM4ErosionSettings& Erosion,
	TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe>& OutGrid, uint64& OutHash,
	TFunctionRef<bool()> IsCancelled, TSharedPtr<const FVoxelLandM4GridLease, ESPMode::ThreadSafe> Lease,
	const FVoxelLandM4StageStats* StageStats)
{
	if (const FVoxelLandError Error = VoxelLandM4Domain::Validate(Domain))
	{
		return Error;
	}
	if (Domain.Mode != EVoxelLandM4DomainMode::Finite)
	{
		return FVoxelLandM4PrepassInternals::M4PrepassError(
			EVoxelLandErrorCode::InvalidParameter, TEXT("M4 global prepass requires a finite domain"));
	}
	const int64 Count = (int64(Domain.CellsX) + 1) * (int64(Domain.CellsY) + 1);
	if (BaseHeightsQ8.Num() != Count || DropletCount < 0 || DropletCount > 65536)
	{
		return FVoxelLandM4PrepassInternals::M4PrepassError(
			EVoxelLandErrorCode::InvalidParameter, TEXT("Invalid M4 prepass sample or droplet count"));
	}
	if (IsCancelled())
	{
		return FVoxelLandM4PrepassInternals::M4PrepassError(
			EVoxelLandErrorCode::Cancelled, TEXT("M4 prepass cancelled"));
	}

	TArray<int32> FinalHeights;
	if (Domain.bGlobalErosion)
	{
		TArray<FIntPoint> Starts;
		if (const FVoxelLandError Error = VoxelLandM4ErosionRandom::Starts(
				WorldSeed, StageId, Salt, DropletCount, Domain.CellsX, Domain.CellsY, Starts, IsCancelled))
		{
			return Error;
		}
		if (const FVoxelLandError Error = VoxelLandM4Erosion::Apply(
				Domain.CellsX, Domain.CellsY, BaseHeightsQ8, Starts, Erosion, FinalHeights, IsCancelled))
		{
			return Error;
		}
	}
	else
	{
		// Copy in bounded rows so a large base grid remains cancellable.
		FinalHeights.Reserve(int32(Count));
		const int32 Width = Domain.CellsX + 1;
		for (int32 Y = 0; Y <= Domain.CellsY; ++Y)
		{
			if (IsCancelled())
			{
				return FVoxelLandM4PrepassInternals::M4PrepassError(
					EVoxelLandErrorCode::Cancelled, TEXT("M4 prepass cancelled"));
			}
			FinalHeights.Append(BaseHeightsQ8.GetData() + Y * Width, Width);
		}
	}

	// A bounded comparison of existing buffers; no image or per-cell diagnostics
	// are retained. int32 differences fit int64 and at most 1,050,625 such
	// differences fit uint64 even for arbitrary int32 inputs.
	TOptional<FVoxelLandM4StageStats> Observed;
	if (StageStats)
	{
		Observed = *StageStats;
		Observed->ErosionChangedCount = 0;
		Observed->ErosionDeltaMinQ8 = MAX_int64;
		Observed->ErosionDeltaMaxQ8 = MIN_int64;
		Observed->ErosionAbsDeltaSumQ8 = 0;
		const int32 Width = Domain.CellsX + 1;
		for (int32 Y = 0; Y <= Domain.CellsY; ++Y)
		{
			if (IsCancelled())
			{
				return FVoxelLandM4PrepassInternals::M4PrepassError(
					EVoxelLandErrorCode::Cancelled, TEXT("M4 erosion statistics cancelled"));
			}
			for (int32 X = 0; X < Width; ++X)
			{
				const int32 Index = Y * Width + X;
				const int64 Delta = int64(FinalHeights[Index]) - BaseHeightsQ8[Index];
				Observed->ErosionChangedCount += Delta != 0;
				Observed->ErosionDeltaMinQ8 = FMath::Min(Observed->ErosionDeltaMinQ8, Delta);
				Observed->ErosionDeltaMaxQ8 = FMath::Max(Observed->ErosionDeltaMaxQ8, Delta);
				Observed->ErosionAbsDeltaSumQ8 += uint64(Delta < 0 ? -Delta : Delta);
			}
		}
	}

	TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe> Grid;
	bool bGridCancelled = false;
	auto CheckGridCancellation = [&IsCancelled, &bGridCancelled]() {
		bGridCancelled = bGridCancelled || IsCancelled();
		return bGridCancelled;
	};
	const FVoxelLandM4LatticeGeometry Geometry{
		Domain.OriginXCm, Domain.OriginYCm, Domain.GridSpacingCm, Domain.CellsX, Domain.CellsY};
	if (!FVoxelLandM4HeightGrid::Create(Geometry, FinalHeights, Grid, CheckGridCancellation, Lease,
			Observed.IsSet() ? &Observed.GetValue() : nullptr))
	{
		return bGridCancelled ? FVoxelLandM4PrepassInternals::M4PrepassError(
									EVoxelLandErrorCode::Cancelled, TEXT("M4 prepass cancelled"))
							  : FVoxelLandM4PrepassInternals::M4PrepassError(
									EVoxelLandErrorCode::InvalidParameter, TEXT("Invalid M4 height grid"));
	}
	if (IsCancelled())
	{
		return FVoxelLandM4PrepassInternals::M4PrepassError(
			EVoxelLandErrorCode::Cancelled, TEXT("M4 prepass cancelled"));
	}

	// Canonical little-endian result identity, distinct from a reusable prepass
	// dependency key. Include geometry, algorithm version and active settings.
	TArray<uint8> Bytes;
	Bytes.Reserve(96 + FinalHeights.Num() * 4);
	FVoxelLandM4PrepassInternals::M4PrepassWrite32(Bytes, 1); // result-identity format
	FVoxelLandM4PrepassInternals::M4PrepassWrite64(Bytes, uint64(Domain.OriginXCm));
	FVoxelLandM4PrepassInternals::M4PrepassWrite64(Bytes, uint64(Domain.OriginYCm));
	FVoxelLandM4PrepassInternals::M4PrepassWrite32(Bytes, uint32(Domain.CellsX));
	FVoxelLandM4PrepassInternals::M4PrepassWrite32(Bytes, uint32(Domain.CellsY));
	FVoxelLandM4PrepassInternals::M4PrepassWrite64(Bytes, uint64(Domain.GridSpacingCm));
	FVoxelLandM4PrepassInternals::M4PrepassWrite32(Bytes, Domain.bGlobalErosion ? 1u : 0u);
	if (Domain.bGlobalErosion)
	{
		FVoxelLandM4PrepassInternals::M4PrepassWrite32(Bytes, 1); // hydraulic/thermal reference algorithm version
		FVoxelLandM4PrepassInternals::M4PrepassWrite32(Bytes, VoxelLandM4ErosionRandom::AlgorithmVersion);
		FVoxelLandM4PrepassInternals::M4PrepassWrite64(Bytes, WorldSeed);
		FVoxelLandM4PrepassInternals::M4PrepassWrite32(Bytes, StageId.A);
		FVoxelLandM4PrepassInternals::M4PrepassWrite32(Bytes, StageId.B);
		FVoxelLandM4PrepassInternals::M4PrepassWrite32(Bytes, StageId.C);
		FVoxelLandM4PrepassInternals::M4PrepassWrite32(Bytes, StageId.D);
		FVoxelLandM4PrepassInternals::M4PrepassWrite32(Bytes, Salt);
		FVoxelLandM4PrepassInternals::M4PrepassWrite32(Bytes, uint32(DropletCount));
		FVoxelLandM4PrepassInternals::M4PrepassWrite32(Bytes, uint32(Erosion.MaxSteps));
		FVoxelLandM4PrepassInternals::M4PrepassWrite32(Bytes, uint32(Erosion.ThermalIterations));
		FVoxelLandM4PrepassInternals::M4PrepassWrite64(Bytes, uint64(Erosion.CapacityMaxQ8));
		FVoxelLandM4PrepassInternals::M4PrepassWrite64(Bytes, uint64(Erosion.TalusQ8));
		FVoxelLandM4PrepassInternals::M4PrepassWrite64(Bytes, uint64(Erosion.MaxTransferQ8));
	}
	const int32 Width = Domain.CellsX + 1;
	for (int32 Y = 0; Y <= Domain.CellsY; ++Y)
	{
		if (IsCancelled())
		{
			return FVoxelLandM4PrepassInternals::M4PrepassError(
				EVoxelLandErrorCode::Cancelled, TEXT("M4 prepass cancelled"));
		}
		for (int32 X = 0; X < Width; ++X)
		{
			FVoxelLandM4PrepassInternals::M4PrepassWrite32(Bytes, uint32(FinalHeights[Y * Width + X]));
		}
	}
	const uint64 Hash = FXxHash64::HashBuffer(Bytes.GetData(), Bytes.Num()).Hash;
	if (IsCancelled())
	{
		return FVoxelLandM4PrepassInternals::M4PrepassError(
			EVoxelLandErrorCode::Cancelled, TEXT("M4 prepass cancelled"));
	}
	OutGrid = MoveTemp(Grid);
	OutHash = Hash;
	return {};
}
}
