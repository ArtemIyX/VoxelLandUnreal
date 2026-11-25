#include "Generation/VoxelLandM7Clipmap.h"

#include "VoxelLandM7ClipmapInternals.h"

bool FVoxelLandM7Clipmap::Build(const FVoxelLandM7ClipmapSettings& Settings,
	const FVoxelLandM7ClipmapLayout* Previous, FVoxelLandM7ClipmapLayout& OutLayout)
{
	using I = FVoxelLandM7ClipmapInternals;
	if (!I::ValidateSettings(Settings))
	{
		return false;
	}
	FVoxelLandM7ClipmapLayout Result;
	Result.Settings = Settings;
	if (!I::MakeRoots(Settings, Result.Roots))
	{
		return false;
	}

	TSet<FVoxelLandChunkKey> Leaves;
	Leaves.Reserve(Settings.MaxLeaves);
	Result.SplitParents.Reserve(Settings.MaxLeaves / I::ExtraLeavesPerSplit);
	for (const FVoxelLandChunkKey& Root : Result.Roots)
	{
		Leaves.Add(Root);
	}

	int32 WorkRemaining = I::MaxPlannerWork;
	if (Previous && I::AreSettingsCompatible(Settings, Previous->Settings)
		&& !I::RetainPreviousSplits(*Previous, Result, Leaves, WorkRemaining))
	{
		return false;
	}

	TArray<I::FPriority> Pending;
	Pending.Reserve(Settings.MaxLeaves);
	for (const FVoxelLandChunkKey& Leaf : Leaves)
	{
		if (Leaf.LOD > 0)
		{
			Pending.Add(I::MakePriority(Leaf, Settings));
		}
	}
	const int32 ElectiveLimit = FMath::Max(Result.Roots.Num(), Settings.MaxLeaves - Settings.FallbackReserveLeaves);
	TArray<FVoxelLandChunkKey> NewLeaves;
	while (!Pending.IsEmpty() && ElectiveLimit - Leaves.Num() >= I::ExtraLeavesPerSplit)
	{
		if (!I::ConsumeWork(Pending.Num(), WorkRemaining))
		{
			return false;
		}
		int32 Best = 0;
		for (int32 Index = 1; Index < Pending.Num(); ++Index)
		{
			if (I::IsHigherPriority(Pending[Index], Pending[Best]))
			{
				Best = Index;
			}
		}
		const I::FPriority Candidate = Pending[Best];
		Pending.RemoveAtSwap(Best, 1, EAllowShrinking::No);
		if (!Leaves.Contains(Candidate.Value))
		{
			continue;
		}
		if (Candidate.Key > I::SquaredSplitDistanceCm(Candidate.Value, Settings, false))
		{
			continue;
		}
		if (!I::ApplySplitClosure(Candidate.Value, ElectiveLimit, Leaves,
				Result.SplitParents, NewLeaves, WorkRemaining))
		{
			if (WorkRemaining < 0)
			{
				return false;
			}
			continue;
		}
		for (const FVoxelLandChunkKey& Child : NewLeaves)
		{
			if (Child.LOD > 0)
			{
				Pending.Add(I::MakePriority(Child, Settings));
			}
		}
	}

	if (!I::WriteOrderedLeaves(Leaves, Result, WorkRemaining))
	{
		return false;
	}
	OutLayout = MoveTemp(Result);
	return true;
}

bool FVoxelLandM7Clipmap::Refine(const FVoxelLandM7ClipmapSettings& Settings, const FVoxelLandM7ClipmapLayout& Current,
	TConstArrayView<FVoxelLandChunkKey> RequiredLeaves, FVoxelLandM7ClipmapLayout& OutLayout)
{
	using I = FVoxelLandM7ClipmapInternals;
	if (!I::ValidateSettings(Settings) || !I::AreSettingsCompatible(Settings, Current.Settings)
		|| RequiredLeaves.Num() > Settings.MaxLeaves)
	{
		return false;
	}
	FVoxelLandM7ClipmapLayout Result;
	Result.Settings = Settings;
	if (!I::MakeRoots(Settings, Result.Roots))
	{
		return false;
	}

	int32 WorkRemaining = I::MaxPlannerWork;
	TSet<FVoxelLandChunkKey> Leaves;
	Leaves.Reserve(Settings.MaxLeaves);
	if (!I::CanRestoreLayout(Current, Result.Roots, Leaves, WorkRemaining))
	{
		return false;
	}

	TArray<FVoxelLandChunkKey> Ordered;
	Ordered.Append(RequiredLeaves.GetData(), RequiredLeaves.Num());
	Ordered.Sort(I::KeyLess);
	for (const FVoxelLandChunkKey& Key : Ordered)
	{
		if (Key.LOD == 0 || !Leaves.Contains(Key))
		{
			return false;
		}
	}

	Result.SplitParents = Current.SplitParents;
	TArray<FVoxelLandChunkKey> NewLeaves;
	for (const FVoxelLandChunkKey& Key : Ordered)
	{
		// A previous request may have split this leaf as a balance prerequisite.
		if (!Result.SplitParents.Contains(Key)
			&& !I::ApplySplitClosure(Key, Settings.MaxLeaves, Leaves, Result.SplitParents, NewLeaves, WorkRemaining))
		{
			return false;
		}
	}

	if (!I::WriteOrderedLeaves(Leaves, Result, WorkRemaining))
	{
		return false;
	}
	OutLayout = MoveTemp(Result);
	return true;
}
