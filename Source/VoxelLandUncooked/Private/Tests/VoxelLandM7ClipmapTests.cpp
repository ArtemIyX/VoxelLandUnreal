#if WITH_DEV_AUTOMATION_TESTS

#include "Data/Structs/VoxelLandM7ClipmapLayout.h"
#include "Generation/VoxelLandM7Clipmap.h"
#include "Math/Box.h"
#include "Misc/AutomationTest.h"

class FVoxelLandM7ClipmapFixture final
{
public:
	static bool CheckCoverageAndOrder(FAutomationTestBase& Test);
	static bool CheckHysteresis(FAutomationTestBase& Test);
	static bool CheckFallback(FAutomationTestBase& Test);
	static bool CheckHorizontalFace(FAutomationTestBase& Test);

private:
	using FKey = FVoxelLandChunkKey;
	using FLayout = FVoxelLandM7ClipmapLayout;
	using FSettings = FVoxelLandM7ClipmapSettings;

	static FInt64Vector Minimum(const FKey& Key)
	{
		const int64 Stride = int64(1) << Key.LOD;
		return FInt64Vector(int64(Key.Coordinate.X) * Stride, int64(Key.Coordinate.Y) * Stride,
			int64(Key.Coordinate.Z) * Stride);
	}
	static FInt64Vector End(const FKey& Key)
	{
		const int64 Stride = int64(1) << Key.LOD;
		return Minimum(Key) + FInt64Vector(Stride, Stride, Stride);
	}
	static bool Contains(const FKey& A, const FKey& B);
	static bool OverlapsOrTouches(const FKey& A, const FKey& B, int32 ExpectedTouchAxes);
	static bool CheckFaceKeys(const FLayout& Layout, const FVoxelLandM7ClipmapFace& Face);
	static bool CheckPartition(const FLayout& Layout);
	static bool CheckPriority(const FLayout& Layout);
	static bool SameLayout(const FLayout& A, const FLayout& B);
};

bool FVoxelLandM7ClipmapFixture::Contains(const FKey& A, const FKey& B)
{
	const FInt64Vector MinA = Minimum(A);
	const FInt64Vector MinB = Minimum(B);
	const FInt64Vector EndA = End(A);
	const FInt64Vector EndB = End(B);
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		if (MinA[Axis] > MinB[Axis] || EndA[Axis] < EndB[Axis])
		{
			return false;
		}
	}
	return true;
}

bool FVoxelLandM7ClipmapFixture::OverlapsOrTouches(const FKey& A, const FKey& B, int32 ExpectedTouchAxes)
{
	const FInt64Vector MinA = Minimum(A);
	const FInt64Vector MinB = Minimum(B);
	const FInt64Vector EndA = End(A);
	const FInt64Vector EndB = End(B);
	int32 TouchAxes = 0;
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		const int64 Lo = FMath::Max(MinA[Axis], MinB[Axis]);
		const int64 Hi = FMath::Min(EndA[Axis], EndB[Axis]);
		if (Hi < Lo)
		{
			return false;
		}
		TouchAxes += int32(Hi == Lo);
	}
	return TouchAxes == ExpectedTouchAxes;
}

bool FVoxelLandM7ClipmapFixture::CheckFaceKeys(const FLayout& Layout, const FVoxelLandM7ClipmapFace& Face)
{
	if (Face.Coarse.LOD == 0 || Face.Axis > 2 || (Face.Sign != -1 && Face.Sign != 1)
		|| !Layout.Leaves.Contains(Face.Coarse))
	{
		return false;
	}
	for (int32 Index = 0; Index < 4; ++Index)
	{
		const FKey& Fine = Face.Fine[Index];
		if (Fine.LOD + 1 != Face.Coarse.LOD || !Layout.Leaves.Contains(Fine)
			|| !OverlapsOrTouches(Face.Coarse, Fine, 1))
		{
			return false;
		}
		if (Face.Sign > 0 ? Minimum(Fine)[Face.Axis] != End(Face.Coarse)[Face.Axis]
			: End(Fine)[Face.Axis] != Minimum(Face.Coarse)[Face.Axis])
		{
			return false;
		}
		int32 Tangent = 0;
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			if (Axis != Face.Axis)
			{
				const int32 Expected = Face.Coarse.Coordinate[Axis] * 2 + ((Index >> Tangent) & 1);
				if (Fine.Coordinate[Axis] != Expected)
				{
					return false;
				}
				++Tangent;
			}
		}
	}
	return true;
}

bool FVoxelLandM7ClipmapFixture::CheckPartition(const FLayout& Layout)
{
	if (Layout.Leaves.IsEmpty() || Layout.Leaves.Num() > Layout.Settings.MaxLeaves)
	{
		return false;
	}
	int64 LeafVolume = 0;
	int32 MixedPairs = 0;
	for (int32 Index = 0; Index < Layout.Leaves.Num(); ++Index)
	{
		const FKey& Leaf = Layout.Leaves[Index];
		LeafVolume += int64(1) << (3 * Leaf.LOD);
		int32 RootMatches = 0;
		for (const FKey& Root : Layout.Roots)
		{
			RootMatches += int32(Contains(Root, Leaf));
		}
		if (RootMatches != 1)
		{
			return false;
		}
		for (int32 Other = 0; Other < Index; ++Other)
		{
			const FKey& Neighbor = Layout.Leaves[Other];
			if (OverlapsOrTouches(Leaf, Neighbor, 0))
			{
				return false;
			}
			if (OverlapsOrTouches(Leaf, Neighbor, 1))
			{
				const int32 Difference = FMath::Abs(int32(Leaf.LOD) - int32(Neighbor.LOD));
				if (Difference > 1)
				{
					return false;
				}
				MixedPairs += Difference;
			}
		}
	}
	if (LeafVolume != int64(Layout.Roots.Num()) * (int64(1) << (3 * Layout.Settings.MaxLod))
		|| MixedPairs != Layout.MixedFaces.Num() * 4)
	{
		return false;
	}

	for (int32 Index = 0; Index < Layout.MixedFaces.Num(); ++Index)
	{
		const FVoxelLandM7ClipmapFace& Face = Layout.MixedFaces[Index];
		if (!CheckFaceKeys(Layout, Face))
		{
			return false;
		}
		for (int32 Other = 0; Other < Index; ++Other)
		{
			const FVoxelLandM7ClipmapFace& Peer = Layout.MixedFaces[Other];
			if (Face.Coarse == Peer.Coarse && Face.Axis == Peer.Axis && Face.Sign == Peer.Sign)
			{
				return false;
			}
		}
	}
	return true;
}

bool FVoxelLandM7ClipmapFixture::CheckPriority(const FLayout& Layout)
{
	constexpr int32 CellsPerChunk = 32;
	const double BaseWidthCm = double(Layout.Settings.BaseVoxelSizeCm) * CellsPerChunk;
	double LastDistance = -1.0;
	for (int32 Index = 0; Index < Layout.Leaves.Num(); ++Index)
	{
		const FKey& Leaf = Layout.Leaves[Index];
		const FInt64Vector Lo = Minimum(Leaf);
		const FInt64Vector Hi = End(Leaf);
		const FBox Bounds(FVector(double(Lo.X), double(Lo.Y), double(Lo.Z)) * BaseWidthCm,
			FVector(double(Hi.X), double(Hi.Y), double(Hi.Z)) * BaseWidthCm);
		const double Squared = Bounds.ComputeSquaredDistanceToPoint(Layout.Settings.ViewCm);
		if (Squared < LastDistance)
		{
			return false;
		}
		if (Squared == LastDistance)
		{
			const FKey& Last = Layout.Leaves[Index - 1];
			const FIntVector A = Last.Coordinate;
			const FIntVector B = Leaf.Coordinate;
			const bool bCanonicalTie = Last.LOD > Leaf.LOD || (Last.LOD == Leaf.LOD
				&& (A.X < B.X || (A.X == B.X && (A.Y < B.Y || (A.Y == B.Y && A.Z < B.Z)))));
			if (!bCanonicalTie)
			{
				return false;
			}
		}
		LastDistance = Squared;
	}
	return true;
}

bool FVoxelLandM7ClipmapFixture::SameLayout(const FLayout& A, const FLayout& B)
{
	if (A.Settings.ViewCm != B.Settings.ViewCm || A.Settings.MaxLeaves != B.Settings.MaxLeaves
		|| A.Roots != B.Roots || A.Leaves != B.Leaves
		|| A.SplitParents.Num() != B.SplitParents.Num() || A.MixedFaces.Num() != B.MixedFaces.Num())
	{
		return false;
	}
	for (const FKey& Parent : A.SplitParents)
	{
		if (!B.SplitParents.Contains(Parent))
		{
			return false;
		}
	}
	for (int32 Index = 0; Index < A.MixedFaces.Num(); ++Index)
	{
		const FVoxelLandM7ClipmapFace& Face = A.MixedFaces[Index];
		const FVoxelLandM7ClipmapFace& Peer = B.MixedFaces[Index];
		if (!(Face.Coarse == Peer.Coarse) || Face.Axis != Peer.Axis || Face.Sign != Peer.Sign)
		{
			return false;
		}
		for (int32 Child = 0; Child < 4; ++Child)
		{
			if (!(Face.Fine[Child] == Peer.Fine[Child]))
			{
				return false;
			}
		}
	}
	return true;
}

bool FVoxelLandM7ClipmapFixture::CheckCoverageAndOrder(FAutomationTestBase& Test)
{
	FSettings Settings;
	Settings.ViewCm = FVector(-1.0);
	FLayout First;
	FLayout Replay;
	if (!Test.TestTrue(TEXT("default 3 km demand builds around a negative view"),
			FVoxelLandM7Clipmap::Build(Settings, nullptr, First))
		|| !Test.TestTrue(TEXT("the same demand rebuilds independently"), FVoxelLandM7Clipmap::Build(Settings, nullptr, Replay)))
	{
		return false;
	}
	const FKey FirstRoot{FIntVector(-3, -3, -1), 5};
	const FKey LastRoot{FIntVector(2, 2, 0), 5};
	const bool bRootsFit = First.Roots.Num() == 72 && First.Roots[0] == FirstRoot && First.Roots.Last() == LastRoot;
	Test.TestTrue(TEXT("signed root bounds floor the low end and ceil the exclusive high end"), bRootsFit);
	Test.TestTrue(TEXT("root volume is covered exactly by nonoverlapping balanced leaves"),
		CheckPartition(First) && !First.MixedFaces.IsEmpty());
	Test.TestTrue(TEXT("leaf requests follow physical distance with deterministic level and coordinate ties"), CheckPriority(First));
	Test.TestTrue(TEXT("independent runs preserve leaf priority, splits and all four-neighbor face keys"),
		SameLayout(First, Replay));

	FSettings Tight = Settings;
	Tight.MaxLeaves = 71;
	Test.TestFalse(TEXT("mandatory coverage rejects a cap smaller than the root grid"),
		FVoxelLandM7Clipmap::Build(Tight, nullptr, Replay));
	Test.TestTrue(TEXT("cap rejection preserves the prior output"), SameLayout(First, Replay));
	return true;
}

bool FVoxelLandM7ClipmapFixture::CheckHysteresis(FAutomationTestBase& Test)
{
	FSettings Settings;
	Settings.MaxLod = 1;
	Settings.ViewCm = FVector(-50.0, 3200.0, 3200.0);
	Settings.HorizontalRadiusCm = 5000.0;
	Settings.VerticalRadiusCm = 200.0;
	Settings.FirstSplitDistanceCm = 100.0;
	Settings.MaxLeaves = 13;
	Settings.FallbackReserveLeaves = 0;
	const FKey Left{FIntVector(-1, 0, 0), 1};
	const FKey Right{FIntVector::ZeroValue, 1};

	FLayout Old;
	FLayout Protected;
	FLayout Fresh;
	if (!Test.TestTrue(TEXT("one elective split fits alongside six mandatory roots"),
			FVoxelLandM7Clipmap::Build(Settings, nullptr, Old)))
	{
		return false;
	}
	Test.TestTrue(TEXT("left root wins before crossing"), Old.SplitParents.Num() == 1 && Old.SplitParents.Contains(Left));

	Settings.ViewCm.X = 105.0;
	if (!Test.TestTrue(TEXT("previous split can be protected inside the exit-only hysteresis band"),
			FVoxelLandM7Clipmap::Build(Settings, &Old, Protected))
		|| !Test.TestTrue(TEXT("fresh plan at the same view succeeds"), FVoxelLandM7Clipmap::Build(Settings, nullptr, Fresh)))
	{
		return false;
	}
	Test.TestTrue(TEXT("protected root claims the last split despite a nearer candidate"),
		Protected.SplitParents.Num() == 1 && Protected.SplitParents.Contains(Left));
	Test.TestTrue(TEXT("without history the nearer root splits"), Fresh.SplitParents.Num() == 1 && Fresh.SplitParents.Contains(Right));

	Settings.ViewCm.X = 111.0;
	Test.TestTrue(TEXT("past exit distance the cap permits a new split"), FVoxelLandM7Clipmap::Build(Settings, &Protected, Fresh));
	Test.TestTrue(TEXT("past exit distance the left root coarsens"), Fresh.SplitParents.Num() == 1 && Fresh.SplitParents.Contains(Right));

	Settings.HorizontalRadiusCm = 6300.0;
	Settings.ViewCm.X = -50.0;
	if (!Test.TestTrue(TEXT("larger radius initially still fits the left split"),
			FVoxelLandM7Clipmap::Build(Settings, nullptr, Old)))
	{
		return false;
	}
	Settings.ViewCm.X = 105.0;
	Protected = Old;
	Test.TestFalse(TEXT("an expanded root grid cannot evict a protected split to meet the cap"),
		FVoxelLandM7Clipmap::Build(Settings, &Old, Protected));
	Test.TestTrue(TEXT("protected-cap failure preserves the complete prior layout"), SameLayout(Old, Protected));
	return true;
}

bool FVoxelLandM7ClipmapFixture::CheckFallback(FAutomationTestBase& Test)
{
	FSettings Settings;
	Settings.MaxLod = 2;
	Settings.ViewCm = FVector(100.0, 6400.0, 6400.0);
	Settings.HorizontalRadiusCm = 5000.0;
	Settings.VerticalRadiusCm = 1000.0;
	Settings.FirstSplitDistanceCm = 100.0;
	Settings.MaxLeaves = 23;
	Settings.FallbackReserveLeaves = 14;
	FLayout Base;
	if (!Test.TestTrue(TEXT("elective cap admits two roots plus one split"),
			FVoxelLandM7Clipmap::Build(Settings, nullptr, Base)))
	{
		return false;
	}
	const FKey CoarseNeighbor{FIntVector(-1, 0, 0), 2};
	const FKey Required{FIntVector::ZeroValue, 1};
	Test.TestTrue(TEXT("elective layout preserves the fallback reserve and exposes a complete face"),
		Base.Leaves.Num() == 9 && Base.Leaves.Contains(CoarseNeighbor) && Base.Leaves.Contains(Required)
		&& Base.MixedFaces.Num() == 1 && Base.MixedFaces[0].Axis == 0 && Base.MixedFaces[0].Sign == 1
		&& CheckPartition(Base));

	FLayout Refined;
	if (!Test.TestTrue(TEXT("fallback spends exactly two reserved splits to preserve face balance"),
			FVoxelLandM7Clipmap::Refine(Settings, Base, TConstArrayView<FKey>(&Required, 1), Refined)))
	{
		return false;
	}
	Test.TestTrue(TEXT("the coarser face neighbor is forced into the same transaction"),
		Refined.Leaves.Num() == 23 && Refined.SplitParents.Contains(CoarseNeighbor) && Refined.SplitParents.Contains(Required)
		&& !Refined.Leaves.Contains(CoarseNeighbor) && !Refined.Leaves.Contains(Required) && CheckPartition(Refined));

	const FKey AnotherLeaf{FIntVector(1, 0, 0), 1};
	Test.TestTrue(TEXT("the next refinement request names an existing coarse leaf"), Refined.Leaves.Contains(AnotherLeaf));
	FLayout Sentinel = Base;
	Test.TestFalse(TEXT("refinement beyond the full cap is rejected without a partial split"),
		FVoxelLandM7Clipmap::Refine(Settings, Refined, TConstArrayView<FKey>(&AnotherLeaf, 1), Sentinel));
	Test.TestTrue(TEXT("fallback failure preserves the caller's prior layout"), SameLayout(Base, Sentinel));
	return true;
}

bool FVoxelLandM7ClipmapFixture::CheckHorizontalFace(FAutomationTestBase& Test)
{
	FSettings Settings;
	Settings.MaxLod = 1;
	Settings.ViewCm = FVector(3200.0, 3200.0, 100.0);
	Settings.HorizontalRadiusCm = 1000.0;
	Settings.VerticalRadiusCm = 5000.0;
	Settings.FirstSplitDistanceCm = 100.0;
	Settings.MaxLeaves = 9;
	Settings.FallbackReserveLeaves = 0;
	FLayout Layout;
	if (!Test.TestTrue(TEXT("two stacked roots can split one side"), FVoxelLandM7Clipmap::Build(Settings, nullptr, Layout)))
	{
		return false;
	}
	const FKey Below{FIntVector(0, 0, -1), 1};
	return Test.TestTrue(TEXT("an unsupported horizontal skirt interface is exposed to fallback planning"),
		Layout.MixedFaces.Num() == 1 && Layout.MixedFaces[0].Axis == 2 && Layout.MixedFaces[0].Sign == 1
		&& Layout.MixedFaces[0].Coarse == Below && CheckPartition(Layout));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM7ClipmapTest, "VoxelLand.M7.Clipmap.Planner",
	EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FVoxelLandM7ClipmapTest::RunTest(const FString& Parameters)
{
	const bool bCoverage = FVoxelLandM7ClipmapFixture::CheckCoverageAndOrder(*this);
	const bool bHysteresis = FVoxelLandM7ClipmapFixture::CheckHysteresis(*this);
	const bool bFallback = FVoxelLandM7ClipmapFixture::CheckFallback(*this);
	const bool bHorizontal = FVoxelLandM7ClipmapFixture::CheckHorizontalFace(*this);
	return bCoverage && bHysteresis && bFallback && bHorizontal;
}

#endif
