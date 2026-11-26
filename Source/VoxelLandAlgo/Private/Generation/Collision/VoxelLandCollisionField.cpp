#include "Generation/Collision/VoxelLandCollisionField.h"

#include "Data/Field/Chunks/VoxelLandChunkData.h"

FVoxelLandCollisionField::FVoxelLandCollisionField(
	double InVoxelSizeCm, FChunkMap InChunks, TOptional<FVoxelLandDensityBounds> InFiniteDirectDensityBounds)
	: VoxelSizeCm(InVoxelSizeCm)
	, Chunks(MoveTemp(InChunks))
	, FiniteDirectDensityBounds(InFiniteDirectDensityBounds)
	, bIsValid(FMath::IsFinite(InVoxelSizeCm) && InVoxelSizeCm > 0.0
			   && (!FiniteDirectDensityBounds.IsSet() || FiniteDirectDensityBounds->IsValid()))
{
}

bool FVoxelLandCollisionField::TryLocatePoint(
	const FVector3d& LogicalCm, FVector3d& OutLattice, FInt64Vector& OutCell) const
{
	if (!IsValid())
	{
		return false;
	}
	constexpr double MinCell = static_cast<double>(MIN_int32) * FVoxelLandChunkData::Cells;
	constexpr double EndCell = (static_cast<double>(MAX_int32) + 1.0) * FVoxelLandChunkData::Cells;
	FVector3d Lattice;
	FInt64Vector Cell;
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		const double Coordinate = LogicalCm[Axis] / VoxelSizeCm;
		if (!FMath::IsFinite(Coordinate) || Coordinate < MinCell || Coordinate >= EndCell)
		{
			return false;
		}
		Lattice[Axis] = Coordinate;
		Cell[Axis] = FMath::FloorToInt64(Coordinate);
	}
	OutLattice = Lattice;
	OutCell = Cell;
	return true;
}

bool FVoxelLandCollisionField::TryGetCell(const FVector3d& LogicalCm, FInt64Vector& OutCell) const
{
	FVector3d Lattice;
	return TryLocatePoint(LogicalCm, Lattice, OutCell);
}

bool FVoxelLandCollisionField::TryGetCellOwner(const FInt64Vector& Cell, FVoxelLandChunkKey& OutOwner)
{
	constexpr int64 Width = FVoxelLandChunkData::Cells;
	FVoxelLandChunkKey Owner;
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		const int64 Quotient = Cell[Axis] / Width;
		const int64 ChunkCoordinate = Quotient - (Cell[Axis] % Width < 0 ? 1 : 0);
		if (ChunkCoordinate < MIN_int32 || ChunkCoordinate > MAX_int32)
		{
			return false;
		}
		Owner.Coordinate[Axis] = static_cast<int32>(ChunkCoordinate);
	}
	OutOwner = Owner;
	return true;
}

bool FVoxelLandCollisionField::TryGetCellDependency(
	const FInt64Vector& Cell, TOptional<FVoxelLandChunkKey>& OutOwner) const
{
	FVoxelLandChunkKey Owner;
	if (!IsValid() || !TryGetCellOwner(Cell, Owner))
	{
		return false;
	}
	if (FiniteDirectDensityBounds.IsSet())
	{
		const FVoxelLandDensityBounds& Bounds = FiniteDirectDensityBounds.GetValue();
		constexpr int64 Width = FVoxelLandChunkData::Cells;
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			const int64 Min = static_cast<int64>(Bounds.MinChunk[Axis]) * Width;
			const int64 Max = static_cast<int64>(Bounds.MaxChunkExclusive[Axis]) * Width;
			if (Cell[Axis] + 1 < Min || Cell[Axis] > Max)
			{
				OutOwner.Reset();
				return true;
			}
			// A cell touching the outer plane reads the clipped edge from the resident chunk's halo.
			Owner.Coordinate[Axis] =
				FMath::Clamp(Owner.Coordinate[Axis], Bounds.MinChunk[Axis], Bounds.MaxChunkExclusive[Axis] - 1);
		}
	}
	OutOwner = Owner;
	return true;
}

EVoxelLandCollisionFieldStatus FVoxelLandCollisionField::ReadCell(
	const FInt64Vector& Cell, double (&OutDensity)[2][2][2], FVoxelLandChunkKey* OutMissingKey) const
{
	constexpr double DensityUnitsPerVoxel = 1024.0;
	TOptional<FVoxelLandChunkKey> Owner;
	if (!TryGetCellDependency(Cell, Owner))
	{
		return EVoxelLandCollisionFieldStatus::Invalid;
	}
	if (!Owner.IsSet())
	{
		for (double (&Slab)[2][2] : OutDensity)
		{
			for (double (&Row)[2] : Slab)
			{
				for (double& Corner : Row)
				{
					Corner = static_cast<double>(MAX_int16) / DensityUnitsPerVoxel;
				}
			}
		}
		return EVoxelLandCollisionFieldStatus::Available;
	}
	const FChunkPtr* Chunk = Chunks.Find(Owner.GetValue());
	if (!Chunk || !Chunk->IsValid())
	{
		if (OutMissingKey)
		{
			*OutMissingKey = Owner.GetValue();
		}
		return EVoxelLandCollisionFieldStatus::Missing;
	}
	const FVoxelLandChunkData& Data = **Chunk;
	if (!(Data.Key == Owner.GetValue()) || Data.Density.Num() != FVoxelLandChunkData::SampleCount)
	{
		return EVoxelLandCollisionFieldStatus::Invalid;
	}

	constexpr int32 Axis = FVoxelLandChunkData::SamplesPerAxis;
	const FInt64Vector Local =
		Cell - FInt64Vector(Data.Key.Coordinate) * static_cast<int64>(FVoxelLandChunkData::Cells) + FInt64Vector(1);
	const int32 BaseIndex = static_cast<int32>(Local.X + Axis * (Local.Y + Axis * Local.Z));
	for (int32 Z = 0; Z < 2; ++Z)
	{
		for (int32 Y = 0; Y < 2; ++Y)
		{
			const int32 Index = BaseIndex + Axis * (Y + Axis * Z);
			OutDensity[Z][Y][0] = static_cast<double>(Data.Density[Index]) / DensityUnitsPerVoxel;
			OutDensity[Z][Y][1] = static_cast<double>(Data.Density[Index + 1]) / DensityUnitsPerVoxel;
		}
	}
	return EVoxelLandCollisionFieldStatus::Available;
}

EVoxelLandCollisionFieldStatus FVoxelLandCollisionField::Sample(
	const FVector3d& LogicalCm, double& OutDensity, FVector3d* OutGradient) const
{
	FVector3d Lattice;
	FInt64Vector Cell;
	if (!TryLocatePoint(LogicalCm, Lattice, Cell))
	{
		return EVoxelLandCollisionFieldStatus::Invalid;
	}
	double Density[2][2][2];
	const EVoxelLandCollisionFieldStatus Status = ReadCell(Cell, Density);
	if (Status == EVoxelLandCollisionFieldStatus::Available)
	{
		OutDensity = EvaluateCell(Density, Lattice - FVector3d(Cell), OutGradient);
	}
	return Status;
}

double FVoxelLandCollisionField::EvaluateCell(
	const double (&Density)[2][2][2], const FVector3d& Fraction, FVector3d* OutGradient)
{
	const double LowZLowY = FMath::Lerp(Density[0][0][0], Density[0][0][1], Fraction.X);
	const double LowZHighY = FMath::Lerp(Density[0][1][0], Density[0][1][1], Fraction.X);
	const double HighZLowY = FMath::Lerp(Density[1][0][0], Density[1][0][1], Fraction.X);
	const double HighZHighY = FMath::Lerp(Density[1][1][0], Density[1][1][1], Fraction.X);
	const double LowZ = FMath::Lerp(LowZLowY, LowZHighY, Fraction.Y);
	const double HighZ = FMath::Lerp(HighZLowY, HighZHighY, Fraction.Y);
	if (OutGradient)
	{
		const double DxLowZ =
			FMath::Lerp(Density[0][0][1] - Density[0][0][0], Density[0][1][1] - Density[0][1][0], Fraction.Y);
		const double DxHighZ =
			FMath::Lerp(Density[1][0][1] - Density[1][0][0], Density[1][1][1] - Density[1][1][0], Fraction.Y);
		*OutGradient = FVector3d(FMath::Lerp(DxLowZ, DxHighZ, Fraction.Z),
			FMath::Lerp(LowZHighY - LowZLowY, HighZHighY - HighZLowY, Fraction.Z), HighZ - LowZ);
	}
	return FMath::Lerp(LowZ, HighZ, Fraction.Z);
}
