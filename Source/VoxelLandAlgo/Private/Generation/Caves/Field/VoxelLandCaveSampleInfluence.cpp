#include "Generation/Caves/Field/VoxelLandCaveSampleInfluence.h"

#include "Data/Caves/Field/VoxelLandCaveEdgeSamples.h"
#include "Data/Caves/Geometry/VoxelLandCavePointCm.h"
#include "Data/Caves/Field/VoxelLandCaveSampleInfluenceRecord.h"
#include "Generation/Caves/Infrastructure/VoxelLandCaveCheckedMath.h"

class FVoxelLandM5SampleInfluenceInternals final
{
public:
	static constexpr int64 CellsPerChunk = 32;
	static constexpr int32 AxisCount = 3;

	static bool Offset(const FVoxelLandM5LatticePoint& Base, int32 X, int32 Y, int32 Z, FVoxelLandM5LatticePoint& Out)
	{
		FVoxelLandM5LatticePoint Value;
		if (!VoxelLand::M5::CheckedMath::TryAdd(Base.X, X, Value.X)
			|| !VoxelLand::M5::CheckedMath::TryAdd(Base.Y, Y, Value.Y)
			|| !VoxelLand::M5::CheckedMath::TryAdd(Base.Z, Z, Value.Z))
		{
			return false;
		}
		Out = Value;
		return true;
	}

	static bool OwnerAxis(int64 Coordinate, int32& Out)
	{
		int64 Quotient = 0;
		if (!VoxelLand::M5::CheckedMath::TryFloorDivide(Coordinate, CellsPerChunk, Quotient) || Quotient < MIN_int32
			|| Quotient > MAX_int32)
		{
			return false;
		}
		Out = static_cast<int32>(Quotient);
		return true;
	}

	static bool HasCorrectOwner(const FVoxelLandM5OwnedLatticeEdge& Edge)
	{
		FIntVector Expected;
		return OwnerAxis(Edge.Lower.X, Expected.X) && OwnerAxis(Edge.Lower.Y, Expected.Y)
			&& OwnerAxis(Edge.Lower.Z, Expected.Z) && Edge.Owner.LOD == 0 && Edge.Owner.Coordinate == Expected;
	}

	static bool FillEdge(const FVoxelLandM5LatticePoint& Sample, int32 Axis, int32 X, int32 Y, int32 Z,
		FVoxelLandM5OwnedLatticeEdge& Out)
	{
		FVoxelLandM5OwnedLatticeEdge Value;
		if (!Offset(Sample, X, Y, Z, Value.Lower) || !OwnerAxis(Value.Lower.X, Value.Owner.Coordinate.X)
			|| !OwnerAxis(Value.Lower.Y, Value.Owner.Coordinate.Y)
			|| !OwnerAxis(Value.Lower.Z, Value.Owner.Coordinate.Z))
		{
			return false;
		}
		Value.Axis = static_cast<uint8>(Axis);
		FVoxelLandM5EdgeSamples Dependencies;
		if (!VoxelLand::M5::SampleInfluence::TryBuildEdgeSamples(Value, Dependencies))
		{
			return false;
		}
		Out = Value;
		return true;
	}

	static bool FillAxisEdges(
		const FVoxelLandM5LatticePoint& Sample, int32 Axis, FVoxelLandM5SampleInfluence& Result, int32& EdgeIndex)
	{
		for (int32 Index = 0; Index < 27; ++Index)
		{
			const int32 X = Index % 3 - 1;
			const int32 Y = (Index / 3) % 3 - 1;
			const int32 Z = Index / 9 - 1;
			const int32 Delta[3] = {X, Y, Z};
			if (Delta[Axis] > 0)
			{
				continue;
			}
			if (!FillEdge(Sample, Axis, X, Y, Z, Result.Edges[EdgeIndex]))
			{
				return false;
			}
			++EdgeIndex;
		}
		return true;
	}
};

bool VoxelLand::M5::SampleInfluence::TryToCentimetres(
	const FVoxelLandM5LatticePoint& Point, int64 VoxelSizeCm, FVoxelLandM5PointCm& Out)
{
	FVoxelLandM5PointCm Result;
	if (VoxelSizeCm <= 0 || !CheckedMath::TryMultiply(Point.X, VoxelSizeCm, Result.X)
		|| !CheckedMath::TryMultiply(Point.Y, VoxelSizeCm, Result.Y)
		|| !CheckedMath::TryMultiply(Point.Z, VoxelSizeCm, Result.Z))
	{
		return false;
	}
	Out = Result;
	return true;
}

bool VoxelLand::M5::SampleInfluence::TryBuildEdgeSamples(
	const FVoxelLandM5OwnedLatticeEdge& Edge, FVoxelLandM5EdgeSamples& Out)
{
	if (Edge.Axis >= FVoxelLandM5SampleInfluenceInternals::AxisCount
		|| !FVoxelLandM5SampleInfluenceInternals::HasCorrectOwner(Edge))
	{
		return false;
	}
	FVoxelLandM5EdgeSamples Result;
	int32 Index = 0;
	for (int32 OffsetIndex = 0; OffsetIndex < 27; ++OffsetIndex)
	{
		const int32 X = OffsetIndex % 3 - 1;
		const int32 Y = (OffsetIndex / 3) % 3 - 1;
		const int32 Z = OffsetIndex / 9 - 1;
		const int32 Delta[3] = {X, Y, Z};
		if (Delta[Edge.Axis] < 0)
		{
			continue;
		}
		if (!FVoxelLandM5SampleInfluenceInternals::Offset(Edge.Lower, X, Y, Z, Result.Samples[Index]))
		{
			return false;
		}
		++Index;
	}
	check(Index == FVoxelLandM5EdgeSamples::Count);
	Out = Result;
	return true;
}

bool VoxelLand::M5::SampleInfluence::TryBuild(const FVoxelLandM5LatticePoint& Sample, FVoxelLandM5SampleInfluence& Out)
{
	FVoxelLandM5SampleInfluence Result;
	int32 CellIndex = 0;
	for (int32 Index = 0; Index < FVoxelLandM5SampleInfluence::CellCount; ++Index)
	{
		const int32 X = Index % 2 - 1;
		const int32 Y = (Index / 2) % 2 - 1;
		const int32 Z = Index / 4 - 1;
		if (!FVoxelLandM5SampleInfluenceInternals::Offset(Sample, X, Y, Z, Result.Cells[CellIndex]))
		{
			return false;
		}
		++CellIndex;
	}
	int32 EdgeIndex = 0;
	for (int32 Axis = 0; Axis < FVoxelLandM5SampleInfluenceInternals::AxisCount; ++Axis)
	{
		if (!FVoxelLandM5SampleInfluenceInternals::FillAxisEdges(Sample, Axis, Result, EdgeIndex))
		{
			return false;
		}
	}
	check(CellIndex == FVoxelLandM5SampleInfluence::CellCount);
	check(EdgeIndex == FVoxelLandM5SampleInfluence::EdgeCount);
	Out = Result;
	return true;
}
