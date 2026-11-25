#if WITH_DEV_AUTOMATION_TESTS
#include "Data/Caves/Field/VoxelLandCaveEdgeSamples.h"
#include "Data/Caves/Geometry/VoxelLandCavePointCm.h"
#include "Data/Caves/Field/VoxelLandCaveSampleInfluenceRecord.h"
#include "Generation/Caves/Field/VoxelLandCaveSampleInfluence.h"
#include "Misc/AutomationTest.h"

class FVoxelLandM5SampleInfluenceOracle final
{
public:
	static FString PointKey(int64 X, int64 Y, int64 Z) { return FString::Printf(TEXT("%lld,%lld,%lld"), X, Y, Z); }

	static FString EdgeKey(int32 Axis, int64 X, int64 Y, int64 Z)
	{
		return FString::Printf(TEXT("%d:%lld,%lld,%lld"), Axis, X, Y, Z);
	}

	static bool SameSet(const TSet<FString>& A, const TSet<FString>& B)
	{
		if (A.Num() != B.Num())
		{
			return false;
		}
		for (const FString& Key : A)
		{
			if (!B.Contains(Key))
			{
				return false;
			}
		}
		return true;
	}

	static int64 FloorChunk(int64 Value)
	{
		const int64 Quotient = Value / 32;
		return Quotient - (Value % 32 < 0 ? 1 : 0);
	}

	static void AddCellEdges(const FVoxelLandM5LatticePoint& Cell, TSet<FString>& Out)
	{
		const int64 Base[3] = {Cell.X, Cell.Y, Cell.Z};
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			const int32 U = (Axis + 1) % 3;
			const int32 V = (Axis + 2) % 3;
			for (int32 I = 0; I < 2; ++I)
			{
				for (int32 J = 0; J < 2; ++J)
				{
					int64 Edge[3] = {Base[0], Base[1], Base[2]};
					Edge[U] += I;
					Edge[V] += J;
					Out.Add(EdgeKey(Axis, Edge[0], Edge[1], Edge[2]));
				}
			}
		}
	}

	static void AddCellCorners(const int64 Cell[3], TSet<FString>& Out)
	{
		for (int32 Z = 0; Z < 2; ++Z)
		{
			for (int32 Y = 0; Y < 2; ++Y)
			{
				for (int32 X = 0; X < 2; ++X)
				{
					Out.Add(PointKey(Cell[0] + X, Cell[1] + Y, Cell[2] + Z));
				}
			}
		}
	}

	static TSet<FString> EdgeDependencies(const FVoxelLandM5OwnedLatticeEdge& Edge)
	{
		TSet<FString> Result;
		const int64 Base[3] = {Edge.Lower.X, Edge.Lower.Y, Edge.Lower.Z};
		const int32 U = (Edge.Axis + 1) % 3;
		const int32 V = (Edge.Axis + 2) % 3;
		for (int32 I = -1; I <= 0; ++I)
		{
			for (int32 J = -1; J <= 0; ++J)
			{
				int64 Cell[3] = {Base[0], Base[1], Base[2]};
				Cell[U] += I;
				Cell[V] += J;
				AddCellCorners(Cell, Result);
			}
		}
		return Result;
	}

	static bool CheckEdge(FAutomationTestBase& Test, const FVoxelLandM5OwnedLatticeEdge& Edge,
		const FVoxelLandM5LatticePoint& Sample, TSet<FString>& ActualEdges)
	{
		const FString Key = EdgeKey(Edge.Axis, Edge.Lower.X, Edge.Lower.Y, Edge.Lower.Z);
		if (ActualEdges.Contains(Key))
		{
			Test.AddError(TEXT("Duplicate edge"));
			return false;
		}
		ActualEdges.Add(Key);
		const FIntVector ExpectedOwner(static_cast<int32>(FloorChunk(Edge.Lower.X)),
			static_cast<int32>(FloorChunk(Edge.Lower.Y)), static_cast<int32>(FloorChunk(Edge.Lower.Z)));
		if (Edge.Owner.LOD != 0 || Edge.Owner.Coordinate != ExpectedOwner)
		{
			Test.AddError(TEXT("Incorrect edge owner"));
			return false;
		}
		FVoxelLandM5EdgeSamples Dependencies;
		if (!VoxelLand::M5::SampleInfluence::TryBuildEdgeSamples(Edge, Dependencies))
		{
			Test.AddError(TEXT("Edge dependencies failed"));
			return false;
		}
		const TSet<FString> Expected = EdgeDependencies(Edge);
		TSet<FString> Actual;
		for (const FVoxelLandM5LatticePoint& Point : Dependencies.Samples)
		{
			Actual.Add(PointKey(Point.X, Point.Y, Point.Z));
		}
		if (Expected.Num() != 18 || Actual.Num() != 18 || !SameSet(Actual, Expected)
			|| !Expected.Contains(PointKey(Sample.X, Sample.Y, Sample.Z)))
		{
			Test.AddError(TEXT("Incorrect edge dependency samples"));
			return false;
		}
		return true;
	}

	static bool EdgeBefore(const FVoxelLandM5OwnedLatticeEdge& A, const FVoxelLandM5OwnedLatticeEdge& B)
	{
		if (A.Axis != B.Axis)
		{
			return A.Axis < B.Axis;
		}
		if (A.Lower.Z != B.Lower.Z)
		{
			return A.Lower.Z < B.Lower.Z;
		}
		if (A.Lower.Y != B.Lower.Y)
		{
			return A.Lower.Y < B.Lower.Y;
		}
		return A.Lower.X < B.Lower.X;
	}

	static bool CheckSample(FAutomationTestBase& Test, const FVoxelLandM5LatticePoint& Sample)
	{
		FVoxelLandM5SampleInfluence Result;
		if (!VoxelLand::M5::SampleInfluence::TryBuild(Sample, Result))
		{
			Test.AddError(TEXT("Sample influence failed"));
			return false;
		}
		TSet<FString> ExpectedCells;
		TSet<FString> ExpectedEdges;
		for (int32 Z = 0; Z < 2; ++Z)
		{
			for (int32 Y = 0; Y < 2; ++Y)
			{
				for (int32 X = 0; X < 2; ++X)
				{
					FVoxelLandM5LatticePoint Cell;
					Cell.X = Sample.X - X;
					Cell.Y = Sample.Y - Y;
					Cell.Z = Sample.Z - Z;
					ExpectedCells.Add(PointKey(Cell.X, Cell.Y, Cell.Z));
					AddCellEdges(Cell, ExpectedEdges);
				}
			}
		}
		TSet<FString> ActualCells;
		for (const FVoxelLandM5LatticePoint& Cell : Result.Cells)
		{
			ActualCells.Add(PointKey(Cell.X, Cell.Y, Cell.Z));
		}
		for (int32 Index = 1; Index < FVoxelLandM5SampleInfluence::EdgeCount; ++Index)
		{
			if (!EdgeBefore(Result.Edges[Index - 1], Result.Edges[Index]))
			{
				Test.AddError(TEXT("Edge order is not stable axis/Z/Y/X"));
				return false;
			}
		}
		TSet<FString> ActualEdges;
		for (const FVoxelLandM5OwnedLatticeEdge& Edge : Result.Edges)
		{
			if (!CheckEdge(Test, Edge, Sample, ActualEdges))
			{
				return false;
			}
		}
		if (ExpectedCells.Num() != 8 || ActualCells.Num() != 8 || ExpectedEdges.Num() != 54 || ActualEdges.Num() != 54
			|| !SameSet(ExpectedCells, ActualCells) || !SameSet(ExpectedEdges, ActualEdges))
		{
			Test.AddError(TEXT("Inverse neighborhood differs from cell-edge oracle"));
			return false;
		}
		return true;
	}
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM5SampleInfluenceTest, "VoxelLand.M5.Numeric.SampleInfluence",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FVoxelLandM5SampleInfluenceTest::RunTest(const FString& Parameters)
{
	const int64 Seams[] = {0, 31, 32, -1, -32, -33};
	for (const int64 X : Seams)
	{
		for (const int64 Y : Seams)
		{
			for (const int64 Z : Seams)
			{
				FVoxelLandM5LatticePoint Sample;
				Sample.X = X;
				Sample.Y = Y;
				Sample.Z = Z;
				if (!FVoxelLandM5SampleInfluenceOracle::CheckSample(*this, Sample))
				{
					return false;
				}
			}
		}
	}
	FVoxelLandM5SampleInfluence Prior;
	Prior.Cells[0].X = 12345;
	FVoxelLandM5LatticePoint Invalid;
	Invalid.X = MAX_int64;
	TestFalse(TEXT("Overflow rejected"), VoxelLand::M5::SampleInfluence::TryBuild(Invalid, Prior));
	TestEqual(TEXT("Failure preserves output"), Prior.Cells[0].X, int64(12345));
	Invalid.X = (int64(MAX_int32) + 2) * 32;
	TestFalse(TEXT("Unrepresentable owner rejected"), VoxelLand::M5::SampleInfluence::TryBuild(Invalid, Prior));
	TestEqual(TEXT("Owner failure preserves output"), Prior.Cells[0].X, int64(12345));

	FVoxelLandM5PointCm Centimetres;
	Centimetres.X = 456;
	Invalid.X = MAX_int64;
	TestFalse(TEXT("Centimetre overflow rejected"),
		VoxelLand::M5::SampleInfluence::TryToCentimetres(Invalid, 2, Centimetres));
	TestEqual(TEXT("Centimetre failure preserves output"), Centimetres.X, int64(456));
	Invalid.X = -33;
	TestTrue(TEXT("Negative lattice conversion"),
		VoxelLand::M5::SampleInfluence::TryToCentimetres(Invalid, 100, Centimetres));
	TestEqual(TEXT("Negative centimetres"), Centimetres.X, int64(-3300));

	FVoxelLandM5OwnedLatticeEdge BadEdge;
	BadEdge.Axis = 3;
	FVoxelLandM5EdgeSamples PriorSamples;
	PriorSamples.Samples[0].X = 54321;
	TestFalse(
		TEXT("Invalid axis rejected"), VoxelLand::M5::SampleInfluence::TryBuildEdgeSamples(BadEdge, PriorSamples));
	TestEqual(TEXT("Dependency failure preserves output"), PriorSamples.Samples[0].X, int64(54321));
	BadEdge.Axis = 0;
	BadEdge.Owner.Coordinate.X = 1;
	TestFalse(TEXT("Wrong owner rejected"), VoxelLand::M5::SampleInfluence::TryBuildEdgeSamples(BadEdge, PriorSamples));
	TestEqual(TEXT("Owner mismatch preserves output"), PriorSamples.Samples[0].X, int64(54321));
	return true;
}
#endif
