#if WITH_DEV_AUTOMATION_TESTS

#include "Data/Clipmap/Planning/VoxelLandClipmapLayout.h"
#include "Data/Field/Chunks/VoxelLandChunkData.h"
#include "Generation/Clipmap/Planning/VoxelLandHeightClipmap.h"
#include "Misc/AutomationTest.h"

class FVoxelLandHeightClipmapFixture
{
public:
	using FKey = FVoxelLandChunkKey;
	using FCells = TMap<FIntPoint, FKey>;
	using FEdgeMasks = TMap<FKey, uint8>;

	static bool CheckCoverage(FAutomationTestBase& Test, const FVoxelLandM7ClipmapLayout& Layout,
		const FEdgeMasks& EdgeMasks, FCells& OutCells)
	{
		constexpr uint32 AllLevels = (1u << 6) - 1;
		constexpr uint8 AllEdges = FVoxelLandHeightClipmap::NegativeX | FVoxelLandHeightClipmap::PositiveX
								 | FVoxelLandHeightClipmap::NegativeY | FVoxelLandHeightClipmap::PositiveY;
		const FVoxelLandM7ClipmapSettings& Settings = Layout.Settings;
		const double Width = double(Settings.BaseVoxelSizeCm) * FVoxelLandChunkData::Cells;
		const FIntPoint First(FMath::FloorToInt((Settings.ViewCm.X - Settings.HorizontalRadiusCm) / Width),
			FMath::FloorToInt((Settings.ViewCm.Y - Settings.HorizontalRadiusCm) / Width));
		const FIntPoint End(FMath::CeilToInt((Settings.ViewCm.X + Settings.HorizontalRadiusCm) / Width),
			FMath::CeilToInt((Settings.ViewCm.Y + Settings.HorizontalRadiusCm) / Width));
		uint32 SeenLevels = 0;
		bool bNoOverlaps = true;
		for (const FKey& Key : Layout.Leaves)
		{
			const int32 Size = 1 << Key.LOD;
			SeenLevels |= 1u << Key.LOD;
			for (int32 Y = 0; Y < Size; ++Y)
			{
				for (int32 X = 0; X < Size; ++X)
				{
					const FIntPoint Cell(Key.Coordinate.X * Size + X, Key.Coordinate.Y * Size + Y);
					bNoOverlaps &= !OutCells.Contains(Cell);
					OutCells.Add(Cell, Key);
				}
			}
		}
		if (!Test.TestTrue(TEXT("Planar leaves do not overlap"), bNoOverlaps)
			|| !Test.TestTrue(TEXT("Tile budget respected"), Layout.Leaves.Num() <= Settings.MaxLeaves)
			|| !Test.TestEqual(TEXT("All six LODs coexist"), SeenLevels, AllLevels))
		{
			return false;
		}
		for (int32 Y = First.Y; Y < End.Y; ++Y)
		{
			for (int32 X = First.X; X < End.X; ++X)
			{
				if (!OutCells.Contains(FIntPoint(X, Y)))
				{
					Test.AddError(TEXT("A cell inside the requested 3 km view has no tile"));
					return false;
				}
			}
		}

		FEdgeMasks ExpectedMasks;
		const FIntPoint Neighbors[] = {FIntPoint(1, 0), FIntPoint(0, 1)};
		constexpr uint8 ForwardMask[] = {FVoxelLandHeightClipmap::PositiveX, FVoxelLandHeightClipmap::PositiveY};
		constexpr uint8 ReverseMask[] = {FVoxelLandHeightClipmap::NegativeX, FVoxelLandHeightClipmap::NegativeY};
		for (const TPair<FIntPoint, FKey>& Entry : OutCells)
		{
			for (int32 Axis = 0; Axis < UE_ARRAY_COUNT(Neighbors); ++Axis)
			{
				const FKey* Neighbor = OutCells.Find(Entry.Key + Neighbors[Axis]);
				if (!Neighbor || Neighbor->LOD == Entry.Value.LOD)
				{
					continue;
				}
				if (FMath::Abs(int32(Neighbor->LOD) - Entry.Value.LOD) > 1)
				{
					Test.AddError(TEXT("Adjacent planar leaves differ by more than one LOD"));
					return false;
				}
				if (Entry.Value.LOD > Neighbor->LOD)
				{
					ExpectedMasks.FindOrAdd(Entry.Value) |= ForwardMask[Axis];
				}
				else
				{
					ExpectedMasks.FindOrAdd(*Neighbor) |= ReverseMask[Axis];
				}
			}
		}

		if (!Test.TestFalse(TEXT("The layout contains real mixed edges"), ExpectedMasks.IsEmpty())
			|| !Test.TestEqual(TEXT("Every coarse mixed edge has a mask"), EdgeMasks.Num(), ExpectedMasks.Num()))
		{
			return false;
		}
		uint8 EdgeDirections = 0;
		for (const TPair<FKey, uint8>& Entry : ExpectedMasks)
		{
			EdgeDirections |= Entry.Value;
			if (EdgeMasks.FindRef(Entry.Key) != Entry.Value)
			{
				Test.AddError(TEXT("The coarse edge mask does not match its finer neighbors"));
				return false;
			}
		}
		return Test.TestEqual(TEXT("Mixed edges span all four directions"), EdgeDirections, AllEdges);
	}
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandHeightClipmapTest, "VoxelLand.Clipmap.Height.Planner",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FVoxelLandHeightClipmapTest::RunTest(const FString& Parameters)
{
	using FHeightClipmapFixture = FVoxelLandHeightClipmapFixture;
	FVoxelLandM7ClipmapSettings Settings;
	Settings.ViewCm = FVector(-3100.0, -2100.0, 2600.0);
	FVoxelLandM7ClipmapLayout Initial;
	FHeightClipmapFixture::FEdgeMasks EdgeMasks;
	FHeightClipmapFixture::FCells InitialCells;
	if (!TestTrue(TEXT("Build the six-level 3 km view"),
			FVoxelLandHeightClipmap::Build(Settings, nullptr, Initial, EdgeMasks))
		|| !FHeightClipmapFixture::CheckCoverage(*this, Initial, EdgeMasks, InitialCells))
	{
		return false;
	}

	const FIntPoint TrackedCell(-1, -1);
	if (!TestEqual(TEXT("The spawn patch starts at LOD 0"), InitialCells.FindChecked(TrackedCell).LOD, uint8(0)))
	{
		return false;
	}
	FVoxelLandM7ClipmapLayout Higher;
	Settings.ViewCm.Z = 70000.0;
	if (!TestTrue(TEXT("Altitude preserves the terrain layout"),
			FVoxelLandHeightClipmap::Build(Settings, nullptr, Higher, EdgeMasks))
		|| !TestTrue(TEXT("XY layout is independent of altitude"), Initial.Leaves == Higher.Leaves))
	{
		return false;
	}

	FVoxelLandM7ClipmapLayout Previous = MoveTemp(Initial);
	const FVector Route[] = {
		FVector(23000.0, -2100.0, 2600.0), FVector(-118000.0, -175000.0, 2600.0), FVector(-3100.0, -2100.0, 2600.0)};
	for (const FVector& View : Route)
	{
		FVoxelLandM7ClipmapLayout Next;
		Settings.ViewCm = View;
		FHeightClipmapFixture::FCells Cells;
		if (!TestTrue(
				TEXT("Build a shifted 3 km view"), FVoxelLandHeightClipmap::Build(Settings, &Previous, Next, EdgeMasks))
			|| !FHeightClipmapFixture::CheckCoverage(*this, Next, EdgeMasks, Cells))
		{
			return false;
		}
		const FIntPoint CameraCell(FMath::FloorToInt(View.X / 3200.0), FMath::FloorToInt(View.Y / 3200.0));
		TestEqual(TEXT("The camera always has LOD 0"), Cells.FindChecked(CameraCell).LOD, uint8(0));
		const bool bBackAtSpawn = CameraCell == TrackedCell;
		TestEqual(TEXT("The same patch coarsens in flight and refines on return"),
			Cells.FindChecked(TrackedCell).LOD == 0, bBackAtSpawn);
		Previous = MoveTemp(Next);
	}
	return true;
}

#endif
