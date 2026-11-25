#include "Generation/Field/VoxelLandGenerator.h"

#include "Data/Clipmap/Sampling/VoxelLandTerrainSampleGrid.h"
#include "Data/Meshing/VoxelLandMeshData.h"
#include "VoxelLandCompletedBox.h"
#include "VoxelLandLocalTerrainHeightSource.h"
#include "../../Field/VoxelLandQuantizedDensity.h"

class FVoxelLandM7CoarseField final
{
public:
	static constexpr int64 MaxPatchWork = 64 * 1024 * 1024;
	static constexpr int32 Axis = FVoxelLandChunkData::SamplesPerAxis;

	static FVoxelLandM7SampleGrid MakeGrid(const FVoxelLandChunkKey& Key)
	{
		const int32 Stride = 1 << Key.LOD;
		return {(FInt64Vector(Key.Coordinate) * int64(FVoxelLandChunkData::Cells) - FInt64Vector(1)) * int64(Stride),
			FIntVector(Axis), Stride};
	}

	static int32 Span(const FVoxelLandM7SampleGrid& Grid) { return (Axis - 1) * Grid.Stride + 1; }

	static int32 GroupSize(int32 Stride, int32 Halo)
	{
		// A dense tile only saves work if the source neighborhoods touch or overlap.
		constexpr int32 Groups[] = {7, 5, 1};
		for (int32 Group : Groups)
		{
			if (Group == 1 || (Stride <= 2 * Halo + 1 && (Group - 1) * Stride + 1 <= Axis))
			{
				return Group;
			}
		}
		return 1;
	}

	static void SelectPatch(const FIntVector& Origin, int32 GroupSize, int32 Stride, TConstArrayView<int16> Patch,
		TArray<int16>& Destination)
	{
		const int32 BoxAxis = (GroupSize - 1) * Stride + 1;
		for (int32 Z = 0; Z < GroupSize; ++Z)
		{
			for (int32 Y = 0; Y < GroupSize; ++Y)
			{
				const int32 Target = Origin.X + Axis * (Origin.Y + Y + Axis * (Origin.Z + Z));
				const int32 Source = BoxAxis * Stride * (Y + BoxAxis * Z);
				for (int32 X = 0; X < GroupSize; ++X)
				{
					Destination[Target + X] = Patch[Source + X * Stride];
				}
			}
		}
	}
};

FVoxelLandError FVoxelLandGenerator::ValidateM7CoarseDemand(const FVoxelLandChunkKey& Key) const
{
	if (!SupportsM7CompletedField())
	{
		return {EVoxelLandErrorCode::UnsupportedLod, TEXT("Coarse terrain requires ordinary finite or local M4")};
	}
	float LevelSpacing = 0.f;
	if (Key.LOD == 0 || !VoxelLandMeshCoordinates::TryGetLevelSpacing(Profile.VoxelSizeCm, Key.LOD, LevelSpacing))
	{
		return {EVoxelLandErrorCode::UnsupportedLod, TEXT("Unsupported M7 terrain LOD")};
	}
	if (!VoxelLandMeshCoordinates::IsMeshKeyInPrecisionRange(Key, Profile.VoxelSizeCm, M4Unlimited.IsValid()))
	{
		return {EVoxelLandErrorCode::NonFiniteCoordinate, TEXT("M7 coarse key exceeds the mesh precision range")};
	}
	const FVoxelLandM7SampleGrid Grid = FVoxelLandM7CoarseField::MakeGrid(Key);
	const FInt64Vector Last = Grid.First + FInt64Vector(FVoxelLandM7CoarseField::Span(Grid) - 1);
	const FInt64Vector CompatibilityMargin(GetM6CompatibilityHalo());
	if (const FVoxelLandError Error =
			ValidateM7RawFootprint(Grid.First - CompatibilityMargin, Last + CompatibilityMargin))
	{
		return Error;
	}
	return ValidateM7StampBox(Grid.First, Last);
}

FVoxelLandError FVoxelLandGenerator::GenerateM7StridedBaseline(const FVoxelLandM7SampleGrid& Grid,
	FVoxelLandWorkerContext& Worker, TArray<int16>& Density, TFunctionRef<bool()> IsCancelled,
	TFunctionRef<bool(int64, int64, int32&)>* LocalHeight) const
{
	if (const FVoxelLandError Error = GenerateM4SampleGrid(Grid, Worker, Density, IsCancelled, LocalHeight))
	{
		return Error;
	}
	if (GetM6CompatibilityHalo() == 0)
	{
		return {};
	}
	TArray<int32> Sums;
	Sums.Append(Density);
	TArray<int16> Neighbors;
	for (int32 Z = -1; Z <= 1; ++Z)
	{
		for (int32 Y = -1; Y <= 1; ++Y)
		{
			for (int32 X = -1; X <= 1; ++X)
			{
				if (X == 0 && Y == 0 && Z == 0)
				{
					continue;
				}
				const FVoxelLandM7SampleGrid OffsetGrid{Grid.First + FInt64Vector(X, Y, Z), Grid.Size, Grid.Stride};
				if (const FVoxelLandError Error =
						GenerateM4SampleGrid(OffsetGrid, Worker, Neighbors, IsCancelled, LocalHeight))
				{
					return Error;
				}
				for (int32 Index = 0; Index < Neighbors.Num(); ++Index)
				{
					Sums[Index] += Neighbors[Index];
				}
			}
		}
	}
	const int32 Strength = int32(double(SmoothStrength) * MAX_uint16 + 0.5);
	for (int32 Index = 0; Index < Density.Num(); ++Index)
	{
		const int32 Mean = FVoxelLandQuantizedDensity::RoundRatioAwayFromZero(Sums[Index], 27);
		Density[Index] = FVoxelLandQuantizedDensity::Blend(Density[Index], Mean, Strength);
	}
	return {};
}

FVoxelLandError FVoxelLandGenerator::ApplyM7CoarsePatches(const FVoxelLandM7SampleGrid& Grid, int32 GroupSize,
	bool bPlanOnly, FVoxelLandWorkerContext& Worker, TArray<int16>& Density, TFunctionRef<bool()> IsCancelled,
	TFunctionRef<bool(int64, int64, int32&)>* LocalHeight) const
{
	const int32 Axis = FVoxelLandM7CoarseField::Axis;
	const int32 BoxAxis = (GroupSize - 1) * Grid.Stride + 1;
	int64 Work = 0;
	TArray<int16> Patch;
	for (int32 Z = 0; Z < Axis; Z += GroupSize)
	{
		for (int32 Y = 0; Y < Axis; Y += GroupSize)
		{
			if (IsCancelled())
			{
				return {EVoxelLandErrorCode::Cancelled, TEXT("M7 coarse field patch processing cancelled")};
			}
			for (int32 X = 0; X < Axis; X += GroupSize)
			{
				const FIntVector Origin(X, Y, Z);
				const FInt64Vector First = Grid.First + FInt64Vector(Origin) * int64(Grid.Stride);
				if (!MakeM6FilterPlan(First, BoxAxis).IsActive())
				{
					continue;
				}
				FVoxelLandM7CompletedBox Box;
				if (const FVoxelLandError Error = PlanM7CompletedBox(First, BoxAxis, Box))
				{
					return Error;
				}
				Work += Box.Work;
				if (Work > FVoxelLandM7CoarseField::MaxPatchWork)
				{
					return {EVoxelLandErrorCode::InvalidParameter,
						TEXT("M7 coarse field exceeds the processed tile work budget")};
				}
				if (!bPlanOnly)
				{
					if (const FVoxelLandError Error = ReadM7CompletedBox(Box, Worker, Patch, IsCancelled, LocalHeight))
					{
						return Error;
					}
					FVoxelLandM7CoarseField::SelectPatch(Origin, GroupSize, Grid.Stride, Patch, Density);
				}
			}
		}
	}
	return {};
}

FVoxelLandError FVoxelLandGenerator::GenerateM7CoarseChunk(const FVoxelLandChunkKey& Key,
	FVoxelLandWorkerContext& Worker, TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe>& OutChunk,
	TFunctionRef<bool()> IsCancelled) const
{
	if (IsInGameThread())
	{
		return {EVoxelLandErrorCode::GameThreadNotAllowed, TEXT("M7 coarse generation requires a worker thread")};
	}
	if (!OwnsWorkerOnCurrentThread(Worker))
	{
		return {
			EVoxelLandErrorCode::InvalidContext, TEXT("M7 coarse worker belongs to a different generator or thread")};
	}
	if (const FVoxelLandError Error = ValidateM7CoarseDemand(Key))
	{
		return Error;
	}
	if (IsCancelled())
	{
		return {EVoxelLandErrorCode::Cancelled, TEXT("M7 coarse generation cancelled")};
	}

	const FVoxelLandM7SampleGrid Grid = FVoxelLandM7CoarseField::MakeGrid(Key);
	const FVoxelLandM6FilterPlan BroadPlan = MakeM6FilterPlan(Grid.First, FVoxelLandM7CoarseField::Span(Grid));
	const int32 GroupSize = FVoxelLandM7CoarseField::GroupSize(Grid.Stride, BroadPlan.Halo);
	const bool bNeighborhood = BroadPlan.Halo > 0;
	TArray<int16> Density;
	if (bNeighborhood)
	{
		if (const FVoxelLandError Error =
				ApplyM7CoarsePatches(Grid, GroupSize, true, Worker, Density, IsCancelled, nullptr))
		{
			return Error;
		}
	}

	FVoxelLandM7LocalHeightSource LocalSource(M4Unlimited.Get());
	TFunctionRef<bool(int64, int64, int32&)> LocalHeight(LocalSource);
	TFunctionRef<bool(int64, int64, int32&)>* Height = M4Unlimited ? &LocalHeight : nullptr;
	if (const FVoxelLandError Error = GenerateM7StridedBaseline(Grid, Worker, Density, IsCancelled, Height))
	{
		return LocalSource.PreferSourceError(Error);
	}
	const FInt64Vector FirstCm = Grid.First * int64(Profile.VoxelSizeCm);
	if (!bNeighborhood)
	{
		if (const FVoxelLandError Error =
				ApplyM6Box(FirstCm, FVoxelLandM7CoarseField::Axis, BroadPlan, Density, IsCancelled, Grid.Stride))
		{
			return Error;
		}
	}
	else
	{
		if (!M6Stamps.IsEmpty())
		{
			if (const FVoxelLandError Error =
					ApplyM6Stamps(FirstCm, FVoxelLandM7CoarseField::Axis, Density, IsCancelled, Grid.Stride))
			{
				return Error;
			}
		}
		if (const FVoxelLandError Error =
				ApplyM7CoarsePatches(Grid, GroupSize, false, Worker, Density, IsCancelled, Height))
		{
			return LocalSource.PreferSourceError(Error);
		}
	}
	const TSharedRef<FVoxelLandChunkData, ESPMode::ThreadSafe> Chunk =
		MakeShared<FVoxelLandChunkData, ESPMode::ThreadSafe>();
	Chunk->Key = Key;
	Chunk->Density = MoveTemp(Density);
	return FinalizeDensityChunk(Chunk, Worker, OutChunk, IsCancelled, false);
}
