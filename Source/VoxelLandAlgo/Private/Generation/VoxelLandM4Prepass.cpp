#include "Generation/VoxelLandM4Prepass.h"

#include "Generation/VoxelLandM4Domain.h"
#include "Generation/VoxelLandM4Erosion.h"
#include "Generation/VoxelLandM4ErosionRandom.h"
#include "Generation/VoxelLandM4HeightGrid.h"
#include "Hash/xxhash.h"

namespace VoxelLandM4Prepass
{
namespace
{
FVoxelLandError M4PrepassError(EVoxelLandErrorCode Code, const TCHAR* Message)
{
	FVoxelLandError Error;
	Error.Code = Code;
	Error.Message = Message;
	return Error;
}

void M4PrepassWrite32(TArray<uint8>& Bytes, uint32 Value)
{
	for (int32 Shift = 0; Shift < 32; Shift += 8)
	{
		Bytes.Add(uint8(Value >> Shift));
	}
}

void M4PrepassWrite64(TArray<uint8>& Bytes, uint64 Value)
{
	for (int32 Shift = 0; Shift < 64; Shift += 8)
	{
		Bytes.Add(uint8(Value >> Shift));
	}
}
}

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
		return M4PrepassError(
			EVoxelLandErrorCode::InvalidParameter, TEXT("M4 global prepass requires a finite domain"));
	}
	const int64 Count = (int64(Domain.CellsX) + 1) * (int64(Domain.CellsY) + 1);
	if (BaseHeightsQ8.Num() != Count || DropletCount < 0 || DropletCount > 65536)
	{
		return M4PrepassError(
			EVoxelLandErrorCode::InvalidParameter, TEXT("Invalid M4 prepass sample or droplet count"));
	}
	if (IsCancelled())
	{
		return M4PrepassError(EVoxelLandErrorCode::Cancelled, TEXT("M4 prepass cancelled"));
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
				return M4PrepassError(EVoxelLandErrorCode::Cancelled, TEXT("M4 prepass cancelled"));
			}
			FinalHeights.Append(BaseHeightsQ8.GetData() + Y * Width, Width);
		}
	}

	TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe> Grid;
	bool bGridCancelled = false;
	auto CheckGridCancellation = [&IsCancelled, &bGridCancelled]() {
		bGridCancelled = bGridCancelled || IsCancelled();
		return bGridCancelled;
	};
	if (!FVoxelLandM4HeightGrid::Create(Domain.OriginXCm, Domain.OriginYCm, Domain.CellsX, Domain.CellsY,
			Domain.GridSpacingCm, FinalHeights, Grid, CheckGridCancellation, Lease, StageStats))
	{
		return bGridCancelled ? M4PrepassError(EVoxelLandErrorCode::Cancelled, TEXT("M4 prepass cancelled"))
							  : M4PrepassError(EVoxelLandErrorCode::InvalidParameter, TEXT("Invalid M4 height grid"));
	}
	if (IsCancelled())
	{
		return M4PrepassError(EVoxelLandErrorCode::Cancelled, TEXT("M4 prepass cancelled"));
	}

	// Canonical little-endian result identity, distinct from a reusable prepass
	// dependency key. Include geometry, algorithm version and active settings.
	TArray<uint8> Bytes;
	Bytes.Reserve(96 + FinalHeights.Num() * 4);
	M4PrepassWrite32(Bytes, 1); // result-identity format
	M4PrepassWrite64(Bytes, uint64(Domain.OriginXCm));
	M4PrepassWrite64(Bytes, uint64(Domain.OriginYCm));
	M4PrepassWrite32(Bytes, uint32(Domain.CellsX));
	M4PrepassWrite32(Bytes, uint32(Domain.CellsY));
	M4PrepassWrite64(Bytes, uint64(Domain.GridSpacingCm));
	M4PrepassWrite32(Bytes, Domain.bGlobalErosion ? 1u : 0u);
	if (Domain.bGlobalErosion)
	{
		M4PrepassWrite32(Bytes, 1); // hydraulic/thermal reference algorithm version
		M4PrepassWrite32(Bytes, VoxelLandM4ErosionRandom::AlgorithmVersion);
		M4PrepassWrite64(Bytes, WorldSeed);
		M4PrepassWrite32(Bytes, StageId.A);
		M4PrepassWrite32(Bytes, StageId.B);
		M4PrepassWrite32(Bytes, StageId.C);
		M4PrepassWrite32(Bytes, StageId.D);
		M4PrepassWrite32(Bytes, Salt);
		M4PrepassWrite32(Bytes, uint32(DropletCount));
		M4PrepassWrite32(Bytes, uint32(Erosion.MaxSteps));
		M4PrepassWrite32(Bytes, uint32(Erosion.ThermalIterations));
		M4PrepassWrite64(Bytes, uint64(Erosion.CapacityMaxQ8));
		M4PrepassWrite64(Bytes, uint64(Erosion.TalusQ8));
		M4PrepassWrite64(Bytes, uint64(Erosion.MaxTransferQ8));
	}
	const int32 Width = Domain.CellsX + 1;
	for (int32 Y = 0; Y <= Domain.CellsY; ++Y)
	{
		if (IsCancelled())
		{
			return M4PrepassError(EVoxelLandErrorCode::Cancelled, TEXT("M4 prepass cancelled"));
		}
		for (int32 X = 0; X < Width; ++X)
		{
			M4PrepassWrite32(Bytes, uint32(FinalHeights[Y * Width + X]));
		}
	}
	const uint64 Hash = FXxHash64::HashBuffer(Bytes.GetData(), Bytes.Num()).Hash;
	if (IsCancelled())
	{
		return M4PrepassError(EVoxelLandErrorCode::Cancelled, TEXT("M4 prepass cancelled"));
	}
	OutGrid = MoveTemp(Grid);
	OutHash = Hash;
	return {};
}
}
