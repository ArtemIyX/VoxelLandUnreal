#include "Generation/Clipmap/Planning/VoxelLandHeightClipmap.h"

#include "Data/Field/Chunks/VoxelLandChunkData.h"
#include "VoxelLandClipmapInternals.h"

bool FVoxelLandHeightClipmap::MakeRoots(FVoxelLandM7ClipmapLayout& Layout)
{
	const FVoxelLandM7ClipmapSettings& Settings = Layout.Settings;
	const int64 Stride = int64(1) << Settings.MaxLod;
	const double Width = double(Settings.BaseVoxelSizeCm) * FVoxelLandChunkData::Cells * Stride;
	FInt64Vector First = FInt64Vector::ZeroValue;
	FInt64Vector End = FInt64Vector::ZeroValue;
	int32 Count = 1;
	for (int32 Axis = 0; Axis < 2; ++Axis)
	{
		const double Lo = FMath::FloorToDouble((Settings.ViewCm[Axis] - Settings.HorizontalRadiusCm) / Width);
		const double Hi = FMath::CeilToDouble((Settings.ViewCm[Axis] + Settings.HorizontalRadiusCm) / Width);
		if (!FMath::IsFinite(Lo) || !FMath::IsFinite(Hi) || Lo < double(int64(MIN_int32) / Stride)
			|| Hi > double((int64(MAX_int32) + 1) / Stride) || Hi <= Lo)
		{
			return false;
		}
		First[Axis] = int64(Lo);
		End[Axis] = int64(Hi);
		const int64 Extent = End[Axis] - First[Axis];
		if (Extent > Settings.MaxLeaves / Count)
		{
			return false;
		}
		Count *= int32(Extent);
	}

	Layout.Roots.Reserve(Count);
	for (int64 Y = First.Y; Y < End.Y; ++Y)
	{
		for (int64 X = First.X; X < End.X; ++X)
		{
			Layout.Roots.Add({FIntVector(int32(X), int32(Y), 0), Settings.MaxLod});
		}
	}
	return true;
}

FVoxelLandHeightClipmap::FKey FVoxelLandHeightClipmap::ProbeCoarserNeighbor(const FKey& Key, int32 Edge)
{
	constexpr int32 XOffset[] = {-1, 1, 0, 0};
	constexpr int32 YOffset[] = {0, 0, -1, 1};
	// Use int64 for a face probe just outside the int32 tile at the lattice limit.
	const int64 X = int64(Key.Coordinate.X) + XOffset[Edge];
	const int64 Y = int64(Key.Coordinate.Y) + YOffset[Edge];
	const int32 ParentX = int32(X / 2 - (X % 2 < 0 ? 1 : 0));
	const int32 ParentY = int32(Y / 2 - (Y % 2 < 0 ? 1 : 0));
	return {FIntVector(ParentX, ParentY, 0), uint8(Key.LOD + 1)};
}

FVoxelLandHeightClipmap::FPriority FVoxelLandHeightClipmap::MakePriority(
	const FKey& Key, const FVoxelLandM7ClipmapSettings& Settings)
{
	const double Width = double(Settings.BaseVoxelSizeCm) * FVoxelLandChunkData::Cells * (int64(1) << Key.LOD);
	const double X = double(Key.Coordinate.X) * Width;
	const double Y = double(Key.Coordinate.Y) * Width;
	const double DX = FMath::Max3(X - Settings.ViewCm.X, Settings.ViewCm.X - (X + Width), 0.0);
	const double DY = FMath::Max3(Y - Settings.ViewCm.Y, Settings.ViewCm.Y - (Y + Width), 0.0);
	return {FMath::Square(DX) + FMath::Square(DY), Key};
}

void FVoxelLandHeightClipmap::Split(
	const FKey& Key, FVoxelLandM7ClipmapLayout& Layout, TSet<FKey>& Leaves, TArray<FPriority>& Pending)
{
	using I = FVoxelLandM7ClipmapInternals;
	const int32 Available = (Layout.Settings.MaxLeaves - Leaves.Num()) / ExtraLeavesPerSplit;
	if (Available <= 0)
	{
		return;
	}

	// Check the whole balancing closure before touching any leaves, so a rejected split keeps its coverage.
	TArray<FKey, TInlineAllocator<16>> Parents;
	Parents.Add(Key);
	for (int32 Index = 0; Index < Parents.Num(); ++Index)
	{
		const FKey Parent = Parents[Index];
		if (Parent.LOD == Layout.Settings.MaxLod)
		{
			continue;
		}
		for (int32 Edge = 0; Edge < Edges; ++Edge)
		{
			const FKey Neighbor = ProbeCoarserNeighbor(Parent, Edge);
			if (!Leaves.Contains(Neighbor) || Parents.Contains(Neighbor))
			{
				continue;
			}
			if (Parents.Num() == Available)
			{
				return;
			}
			Parents.Add(Neighbor);
		}
	}

	for (const FKey& Parent : Parents)
	{
		Leaves.Remove(Parent);
		Layout.SplitParents.Add(Parent);
		for (int32 Index = 0; Index < ChildrenPerSplit; ++Index)
		{
			const FIntVector Offset(Index & 1, Index >> 1, 0);
			const FKey Child{Parent.Coordinate * 2 + Offset, uint8(Parent.LOD - 1)};
			Leaves.Add(Child);
			if (Child.LOD > 0)
			{
				Pending.HeapPush(MakePriority(Child, Layout.Settings), I::IsHigherPriority);
			}
		}
	}
}

void FVoxelLandHeightClipmap::WriteFrontier(
	const TSet<FKey>& Leaves, FVoxelLandM7ClipmapLayout& Layout, TMap<FKey, uint8>& CoarseEdgeMasks)
{
	using I = FVoxelLandM7ClipmapInternals;
	TArray<FPriority> Ordered;
	Ordered.Reserve(Leaves.Num());
	constexpr uint8 OppositeEdge[] = {PositiveX, NegativeX, PositiveY, NegativeY};
	for (const FKey& Key : Leaves)
	{
		Ordered.Add(MakePriority(Key, Layout.Settings));
		if (Key.LOD == Layout.Settings.MaxLod)
		{
			continue;
		}
		for (int32 Edge = 0; Edge < Edges; ++Edge)
		{
			const FKey Coarse = ProbeCoarserNeighbor(Key, Edge);
			if (Leaves.Contains(Coarse))
			{
				CoarseEdgeMasks.FindOrAdd(Coarse) |= OppositeEdge[Edge];
			}
		}
	}
	Ordered.Sort(I::IsHigherPriority);
	Layout.Leaves.Reserve(Ordered.Num());
	for (const FPriority& Entry : Ordered)
	{
		Layout.Leaves.Add(Entry.Value);
	}
}

bool FVoxelLandHeightClipmap::Build(const FVoxelLandM7ClipmapSettings& Settings,
	const FVoxelLandM7ClipmapLayout* Previous, FVoxelLandM7ClipmapLayout& OutLayout,
	TMap<FVoxelLandChunkKey, uint8>& OutCoarseEdgeMasks)
{
	using I = FVoxelLandM7ClipmapInternals;
	if (!I::ValidateSettings(Settings))
	{
		return false;
	}
	FVoxelLandM7ClipmapLayout Result;
	Result.Settings = Settings;
	if (!MakeRoots(Result))
	{
		return false;
	}

	const bool bCanRetain = Previous && I::AreSettingsCompatible(Settings, Previous->Settings);
	TSet<FKey> Leaves;
	TArray<FPriority> Pending;
	Leaves.Reserve(Settings.MaxLeaves);
	Pending.Reserve(Settings.MaxLeaves);
	Result.SplitParents.Reserve((Settings.MaxLeaves - Result.Roots.Num()) / ExtraLeavesPerSplit);
	for (const FKey& Root : Result.Roots)
	{
		Leaves.Add(Root);
		if (Root.LOD > 0)
		{
			Pending.Add(MakePriority(Root, Settings));
		}
	}
	Pending.Heapify(I::IsHigherPriority);

	// The edge masks close forward transitions directly. There are no source fallback splits to reserve.
	while (!Pending.IsEmpty() && Settings.MaxLeaves - Leaves.Num() >= ExtraLeavesPerSplit)
	{
		FPriority Entry;
		Pending.HeapPop(Entry, I::IsHigherPriority, EAllowShrinking::No);
		const FKey& Key = Entry.Value;
		const bool bWasSplit = bCanRetain && Previous->SplitParents.Contains(Key);
		if (Leaves.Contains(Key) && Entry.Key <= I::SquaredSplitDistanceCm(Key, Settings, bWasSplit))
		{
			Split(Key, Result, Leaves, Pending);
		}
	}

	TMap<FKey, uint8> CoarseEdgeMasks;
	WriteFrontier(Leaves, Result, CoarseEdgeMasks);
	OutLayout = MoveTemp(Result);
	OutCoarseEdgeMasks = MoveTemp(CoarseEdgeMasks);
	return true;
}
