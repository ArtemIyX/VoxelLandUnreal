#include "VoxelLandClipmapInternals.h"

bool FVoxelLandM7ClipmapInternals::CanRestoreLayout(const FVoxelLandM7ClipmapLayout& Layout,
	const TArray<FKey>& ExpectedRoots, TSet<FKey>& Leaves, int32& WorkRemaining)
{
	const int32 MaxLeaves = Layout.Settings.MaxLeaves;
	if (Layout.Roots != ExpectedRoots || Layout.Leaves.Num() > MaxLeaves
		|| Layout.SplitParents.Num() > (MaxLeaves - ExpectedRoots.Num()) / ExtraLeavesPerSplit
		|| Layout.Leaves.Num() != ExpectedRoots.Num() + Layout.SplitParents.Num() * ExtraLeavesPerSplit)
	{
		return false;
	}
	TArray<FKey> Pending = ExpectedRoots;
	Pending.Reserve(MaxLeaves);
	int32 VisitedSplits = 0;
	while (!Pending.IsEmpty())
	{
		if (!ConsumeWork(1, WorkRemaining))
		{
			return false;
		}
		const FKey Key = Pending.Pop(EAllowShrinking::No);
		if (!Layout.SplitParents.Contains(Key))
		{
			Leaves.Add(Key);
			continue;
		}
		if (Key.LOD == 0 || ++VisitedSplits > Layout.SplitParents.Num())
		{
			return false;
		}
		for (int32 Index = 0; Index < ChildrenPerSplit; ++Index)
		{
			Pending.Add(Child(Key, Index));
		}
	}
	if (VisitedSplits != Layout.SplitParents.Num() || Leaves.Num() != Layout.Leaves.Num())
	{
		return false;
	}

	TSet<FKey> Seen;
	Seen.Reserve(Layout.Leaves.Num());
	constexpr int8 Signs[] = {-1, 1};
	for (const FKey& Key : Layout.Leaves)
	{
		if (!Leaves.Contains(Key) || Seen.Contains(Key)
			|| !ConsumeWork(1 + 2 * AxisCount * MaxPlannerLod, WorkRemaining))
		{
			return false;
		}
		Seen.Add(Key);
		for (int32 Axis = 0; Axis < AxisCount; ++Axis)
		{
			for (const int8 Sign : Signs)
			{
				const FInt64Vector Probe = FaceProbe(Key, Axis, Sign);
				for (uint8 Lod = Key.LOD + 2; Lod <= Layout.Settings.MaxLod; ++Lod)
				{
					FKey Neighbor;
					if (TryKeyContaining(Probe, Lod, Neighbor) && Leaves.Contains(Neighbor))
					{
						return false;
					}
				}
			}
		}
	}
	return true;
}

bool FVoxelLandM7ClipmapInternals::RetainPreviousSplits(const FVoxelLandM7ClipmapLayout& Previous,
	FVoxelLandM7ClipmapLayout& Layout, TSet<FKey>& Leaves, int32& WorkRemaining)
{
	TArray<FKey> PreviousRoots;
	TSet<FKey> PreviousLeaves;
	const FVoxelLandM7ClipmapSettings& Settings = Layout.Settings;
	if (!ValidateSettings(Previous.Settings) || !MakeRoots(Previous.Settings, PreviousRoots)
		|| !CanRestoreLayout(Previous, PreviousRoots, PreviousLeaves, WorkRemaining)
		|| !ConsumeWork(32 * Previous.SplitParents.Num(), WorkRemaining))
	{
		return false;
	}

	TArray<FKey> Ordered;
	Ordered.Reserve(Previous.SplitParents.Num());
	for (const FKey& Key : Previous.SplitParents)
	{
		Ordered.Add(Key);
	}
	Ordered.Sort(KeyLess);
	TArray<FKey> NewLeaves;
	// Ancestors precede descendants. Closure and retained splits can use the
	// fallback reserve, while elective work waits for the protected topology.
	for (const FKey& Key : Ordered)
	{
		if (!Leaves.Contains(Key) || SquaredDistanceCm(Key, Settings) > SquaredSplitDistanceCm(Key, Settings, true))
		{
			continue;
		}
		if (!ApplySplitClosure(Key, Settings.MaxLeaves, Leaves, Layout.SplitParents, NewLeaves, WorkRemaining))
		{
			return false;
		}
	}
	return true;
}

bool FVoxelLandM7ClipmapInternals::ApplySplitClosure(const FKey& Leaf, int32 MaxLeaves, TSet<FKey>& Leaves,
	TSet<FKey>& SplitParents, TArray<FKey>& NewLeaves, int32& WorkRemaining)
{
	const int32 AvailableSplits = (MaxLeaves - Leaves.Num()) / ExtraLeavesPerSplit;
	if (Leaf.LOD == 0 || AvailableSplits <= 0 || !Leaves.Contains(Leaf))
	{
		return false;
	}
	TArray<FKey, TInlineAllocator<16>> Closure;
	TSet<FKey> Seen;
	Closure.Add(Leaf);
	Seen.Add(Leaf);
	constexpr int8 Signs[] = {-1, 1};
	for (int32 Index = 0; Index < Closure.Num(); ++Index)
	{
		if (!ConsumeWork(1 + 2 * AxisCount, WorkRemaining))
		{
			return false;
		}
		const FKey Parent = Closure[Index];
		if (Parent.LOD == MaxPlannerLod)
		{
			continue;
		}
		// A face probe finds the unique larger neighbor whenever one exists:
		// its aligned face spans the entire smaller face.
		for (int32 Axis = 0; Axis < AxisCount; ++Axis)
		{
			for (const int8 Sign : Signs)
			{
				FKey Coarser;
				if (!TryKeyContaining(FaceProbe(Parent, Axis, Sign), Parent.LOD + 1, Coarser)
					|| !Leaves.Contains(Coarser) || Seen.Contains(Coarser))
				{
					continue;
				}
				if (Closure.Num() == AvailableSplits)
				{
					return false;
				}
				Closure.Add(Coarser);
				Seen.Add(Coarser);
			}
		}
	}

	if (!ConsumeWork((ChildrenPerSplit + 2) * Closure.Num(), WorkRemaining))
	{
		return false;
	}
	NewLeaves.Reset(ChildrenPerSplit * Closure.Num());
	for (const FKey& Parent : Closure)
	{
		Leaves.Remove(Parent);
		SplitParents.Add(Parent);
		for (int32 Index = 0; Index < ChildrenPerSplit; ++Index)
		{
			const FKey Key = Child(Parent, Index);
			Leaves.Add(Key);
			NewLeaves.Add(Key);
		}
	}
	return true;
}

bool FVoxelLandM7ClipmapInternals::TryAddMixedFace(
	const FKey& Coarse, int32 Axis, int8 Sign, const TSet<FKey>& Leaves, TArray<FVoxelLandM7ClipmapFace>& MixedFaces)
{
	FVoxelLandM7ClipmapFace Candidate;
	Candidate.Coarse = Coarse;
	Candidate.Axis = uint8(Axis);
	Candidate.Sign = Sign;

	const FInt64Vector Outside = FaceProbe(Coarse, Axis, Sign);
	const uint8 FineLod = Coarse.LOD - 1;
	const int64 FineStride = BaseChunkStride(FineLod);
	const int32 FirstTangent = Axis == 0 ? 1 : 0;
	const int32 SecondTangent = Axis == 2 ? 1 : 2;
	int32 Found = 0;
	constexpr int32 FinePerFace = 4;
	for (int32 Index = 0; Index < FinePerFace; ++Index)
	{
		FInt64Vector Sample = Outside;
		Sample[FirstTangent] += FineStride * int64(Index & 1);
		Sample[SecondTangent] += FineStride * int64(Index >> 1);
		FKey Fine;
		if (TryKeyContaining(Sample, FineLod, Fine) && Leaves.Contains(Fine))
		{
			Candidate.Fine[Index] = Fine;
			++Found;
		}
	}
	if (Found == FinePerFace)
	{
		MixedFaces.Add(Candidate);
	}
	return Found == 0 || Found == FinePerFace;
}

bool FVoxelLandM7ClipmapInternals::AppendMixedFaces(const TArray<FKey>& OrderedKeys, const TSet<FKey>& Leaves,
	TArray<FVoxelLandM7ClipmapFace>& MixedFaces, int32& WorkRemaining)
{
	constexpr int8 Signs[] = {-1, 1};
	constexpr int32 FaceWorkPerKey = 2 * AxisCount * 4;
	if (!ConsumeWork(OrderedKeys.Num() * FaceWorkPerKey, WorkRemaining))
	{
		return false;
	}
	for (const FKey& Key : OrderedKeys)
	{
		if (Key.LOD == 0)
		{
			continue;
		}
		for (int32 Axis = 0; Axis < AxisCount; ++Axis)
		{
			for (const int8 Sign : Signs)
			{
				if (!TryAddMixedFace(Key, Axis, Sign, Leaves, MixedFaces))
				{
					return false;
				}
			}
		}
	}
	return true;
}

bool FVoxelLandM7ClipmapInternals::WriteOrderedLeaves(
	const TSet<FKey>& Leaves, FVoxelLandM7ClipmapLayout& Layout, int32& WorkRemaining)
{
	constexpr int32 WorkPerLeaf = 64;
	if (!ConsumeWork(Leaves.Num() * WorkPerLeaf, WorkRemaining))
	{
		return false;
	}

	TArray<FKey> Ordered;
	TArray<FPriority> Priorities;
	Ordered.Reserve(Leaves.Num());
	Priorities.Reserve(Leaves.Num());
	for (const FKey& Key : Leaves)
	{
		Ordered.Add(Key);
		Priorities.Add(MakePriority(Key, Layout.Settings));
	}
	Ordered.Sort(KeyLess);
	if (!AppendMixedFaces(Ordered, Leaves, Layout.MixedFaces, WorkRemaining))
	{
		return false;
	}
	Priorities.Sort(IsHigherPriority);
	Layout.Leaves.Reserve(Priorities.Num());
	for (const FPriority& Priority : Priorities)
	{
		Layout.Leaves.Add(Priority.Value);
	}
	return true;
}
