#include "Generation/Caves/Field/VoxelLandCaveChunkCarve.h"

#include "Data/Caves/Planning/VoxelLandCavePlan.h"
#include "Generation/Caves/Infrastructure/VoxelLandCaveCheckedMath.h"
#include "Generation/Caves/Planning/VoxelLandCaveComposition.h"
#include "../Planning/VoxelLandCaveCompositionPrivate.h"
#include "VoxelLandCaveChunkCarvePrivate.h"
#include "Generation/Caves/Planning/VoxelLandCaveCandidateResult.h"
#include "../Planning/VoxelLandCavePrimitiveWork.h"

namespace VoxelLand::M5::ChunkCarve
{
class FVoxelLandM5ChunkCarveInternals final
{
public:
	static bool TryCarveInternal(TConstArrayView<int16> Baseline, const FIntVector& Size,
		const FVoxelLandM5PointCm& OriginCm, int64 SpacingCm, int32 VoxelSizeCm, bool bNetworkEnabled,
		bool bPocketEnabled, TConstArrayView<TSharedPtr<const FVoxelLandM5CavePlan, ESPMode::ThreadSafe>> Plans,
		TConstArrayView<uint8> NetworkAllowed, TConstArrayView<uint8> PocketAllowed, TConstArrayView<uint8> Protected,
		bool bExplicitPresence, TConstArrayView<uint8> PocketPresent, TConstArrayView<int64> PocketDistanceQ8,
		int64 MaxScratchBytes, int64 MaxWork, bool bOwned, TFunctionRef<bool()> IsCancelled, TArray<int16>& OutSamples);
};

bool FVoxelLandM5ChunkCarveInternals::TryCarveInternal(TConstArrayView<int16> Baseline, const FIntVector& Size,
	const FVoxelLandM5PointCm& OriginCm, int64 SpacingCm, int32 VoxelSizeCm, bool bNetworkEnabled, bool bPocketEnabled,
	TConstArrayView<TSharedPtr<const FVoxelLandM5CavePlan, ESPMode::ThreadSafe>> Plans,
	TConstArrayView<uint8> NetworkAllowed, TConstArrayView<uint8> PocketAllowed, TConstArrayView<uint8> Protected,
	bool bExplicitPresence, TConstArrayView<uint8> PocketPresent, TConstArrayView<int64> PocketDistanceQ8,
	int64 MaxScratchBytes, int64 MaxWork, bool bOwned, TFunctionRef<bool()> IsCancelled, TArray<int16>& OutSamples)
{
	using namespace CheckedMath;
	if (IsCancelled() || Size.X <= 0 || Size.Y <= 0 || Size.Z <= 0 || MaxScratchBytes < 0 || MaxWork < 0)
	{
		return false;
	}
	int64 XY = 0;
	int64 Count = 0;
	int64 ScratchBytes = 0;
	if (!TryMultiplyNonNegative(Size.X, Size.Y, XY) || !TryMultiplyNonNegative(XY, Size.Z, Count) || Count > MAX_int32
		|| Baseline.Num() != Count || !TryMultiplyNonNegative(Count, sizeof(int16), ScratchBytes)
		|| ScratchBytes > MaxScratchBytes)
	{
		return false;
	}
	const bool bEnabled = bNetworkEnabled || bPocketEnabled;
	int64 WorkPerSample = 1;
	int64 InventoryWork = 0;
	if (bEnabled)
	{
		if (SpacingCm <= 0 || VoxelSizeCm <= 0 || Protected.Num() != Count
			|| (bNetworkEnabled && (Plans.IsEmpty() || NetworkAllowed.Num() != Count))
			|| (bPocketEnabled
				&& (PocketDistanceQ8.Num() != Count || PocketAllowed.Num() != Count
					|| (bExplicitPresence && PocketPresent.Num() != Count))))
		{
			return false;
		}
		// Check the entire sample box before allocating, not just carved samples.
		const int64 Origins[] = {OriginCm.X, OriginCm.Y, OriginCm.Z};
		const int32 Extents[] = {Size.X, Size.Y, Size.Z};
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			int64 Offset = 0;
			int64 Last = 0;
			if (Origins[Axis] < -1048576 || Origins[Axis] > 1048576
				|| !TryMultiplyNonNegative(Extents[Axis] - 1, SpacingCm, Offset) || !TryAdd(Origins[Axis], Offset, Last)
				|| Last > 1048576)
			{
				return false;
			}
		}
		if (bNetworkEnabled)
		{
			for (const auto& Plan : Plans)
			{
				if (IsCancelled() || !Plan.IsValid()
					|| (Plan->GeometryVersion != 1 && !(bOwned && Plan->GeometryVersion == 2)))
				{
					return false;
				}
				if (!TryAdd(InventoryWork, 1 + int64(Plan->Chambers.Num()) + Plan->Segments.Num(), InventoryWork)
					|| InventoryWork > MaxWork)
				{
					return false;
				}
				for (const auto& Chamber : Plan->Chambers)
				{
					if (IsCancelled() || Chamber.Profile.Version != Plan->GeometryVersion)
					{
						return false;
					}
				}
				for (const auto& Segment : Plan->Segments)
				{
					if (IsCancelled() || Segment.Profile.Version != Plan->GeometryVersion)
					{
						return false;
					}
				}
				int64 PlanWork = 0;
				if (!PrimitiveWork::TryForPlan(*Plan, PlanWork) || !TryAdd(WorkPerSample, PlanWork, WorkPerSample))
				{
					return false;
				}
			}
		}
		if (bPocketEnabled && !TryAdd(WorkPerSample, 1, WorkPerSample))
		{
			return false;
		}
	}
	int64 Work = 0;
	if (!TryMultiplyNonNegative(Count, WorkPerSample, Work) || !TryAdd(Work, InventoryWork, Work) || Work > MaxWork)
	{
		return false;
	}
	TArray<int16> Scratch;
	Scratch.Reserve(static_cast<int32>(Count));
	if (!bEnabled)
	{
		for (int32 Index = 0; Index < Baseline.Num(); ++Index)
		{
			if (Index % 64 == 0 && IsCancelled())
			{
				return false;
			}
			Scratch.Add(Baseline[Index]);
		}
	}
	else
	{
		const FVoxelLandM5CavePlan EmptyPlan;
		int32 Index = 0;
		for (int32 Z = 0; Z < Size.Z; ++Z)
		{
			for (int32 Y = 0; Y < Size.Y; ++Y)
			{
				for (int32 X = 0; X < Size.X; ++X, ++Index)
				{
					if ((Index % 64 == 0 && IsCancelled()) || Protected[Index] > 1
						|| (bNetworkEnabled && NetworkAllowed[Index] > 1)
						|| (bPocketEnabled
							&& (PocketAllowed[Index] > 1 || (bExplicitPresence && PocketPresent[Index] > 1))))
					{
						return false;
					}
					// Preflight proved all products and additions representable.
					FVoxelLandM5PointCm P;
					P.X = OriginCm.X + static_cast<int64>(X) * SpacingCm;
					P.Y = OriginCm.Y + static_cast<int64>(Y) * SpacingCm;
					P.Z = OriginCm.Z + static_cast<int64>(Z) * SpacingCm;
					int16 Value = Baseline[Index];
					if (bNetworkEnabled)
					{
						for (const auto& Plan : Plans)
						{
							const bool bOk = bOwned ? Composition::Private::TryComposeOwned(Value, *Plan, P,
														  VoxelSizeCm, true, NetworkAllowed[Index] != 0,
														  Protected[Index] != 0, false, 0, IsCancelled, Value)
													: Composition::TryCompose(Value, *Plan, P, VoxelSizeCm, true,
														  NetworkAllowed[Index] != 0, Protected[Index] != 0, false, 0,
														  IsCancelled, Value);
							if (!bOk)
							{
								return false;
							}
						}
					}
					if (bPocketEnabled
						&& !Composition::TryCompose(Value, EmptyPlan, P, VoxelSizeCm, true, PocketAllowed[Index] != 0,
							Protected[Index] != 0, !bExplicitPresence || PocketPresent[Index] != 0,
							PocketDistanceQ8[Index], IsCancelled, Value))
					{
						return false;
					}
					Scratch.Add(Value);
				}
			}
		}
	}
	if (IsCancelled())
	{
		return false;
	}
	OutSamples = MoveTemp(Scratch);
	return true;
}

bool TryCarve(TConstArrayView<int16> Baseline, const FIntVector& Size, const FVoxelLandM5PointCm& OriginCm,
	int64 SpacingCm, int32 VoxelSizeCm, bool bNetworkEnabled, bool bPocketEnabled,
	TConstArrayView<TSharedPtr<const FVoxelLandM5CavePlan, ESPMode::ThreadSafe>> Plans,
	TConstArrayView<uint8> NetworkAllowed, TConstArrayView<uint8> PocketAllowed, TConstArrayView<uint8> Protected,
	TConstArrayView<int64> PocketDistanceQ8, int64 MaxScratchBytes, int64 MaxWork, TFunctionRef<bool()> IsCancelled,
	TArray<int16>& OutSamples)
{
	return FVoxelLandM5ChunkCarveInternals::TryCarveInternal(Baseline, Size, OriginCm, SpacingCm, VoxelSizeCm,
		bNetworkEnabled, bPocketEnabled, Plans, NetworkAllowed, PocketAllowed, Protected, false, {}, PocketDistanceQ8,
		MaxScratchBytes, MaxWork, false, IsCancelled, OutSamples);
}

bool TryCarve(TConstArrayView<int16> Baseline, const FIntVector& Size, const FVoxelLandM5PointCm& OriginCm,
	int64 SpacingCm, int32 VoxelSizeCm, bool bNetworkEnabled, bool bPocketEnabled,
	TConstArrayView<TSharedPtr<const FVoxelLandM5CavePlan, ESPMode::ThreadSafe>> Plans,
	TConstArrayView<uint8> NetworkAllowed, TConstArrayView<uint8> PocketAllowed, TConstArrayView<uint8> Protected,
	TConstArrayView<uint8> PocketPresent, TConstArrayView<int64> PocketDistanceQ8, int64 MaxScratchBytes, int64 MaxWork,
	TFunctionRef<bool()> IsCancelled, TArray<int16>& OutSamples)
{
	return FVoxelLandM5ChunkCarveInternals::TryCarveInternal(Baseline, Size, OriginCm, SpacingCm, VoxelSizeCm,
		bNetworkEnabled, bPocketEnabled, Plans, NetworkAllowed, PocketAllowed, Protected, true, PocketPresent,
		PocketDistanceQ8, MaxScratchBytes, MaxWork, false, IsCancelled, OutSamples);
}

bool Private::TryCarveOwned(TConstArrayView<int16> Baseline, const FIntVector& Size,
	const FVoxelLandM5PointCm& OriginCm, int64 SpacingCm, int32 VoxelSizeCm, bool bNetworkEnabled, bool bPocketEnabled,
	TConstArrayView<TSharedPtr<const FVoxelLandM5CavePlan, ESPMode::ThreadSafe>> Plans,
	TConstArrayView<uint8> NetworkAllowed, TConstArrayView<uint8> PocketAllowed, TConstArrayView<uint8> Protected,
	TConstArrayView<uint8> PocketPresent, TConstArrayView<int64> PocketDistanceQ8, int64 MaxScratchBytes, int64 MaxWork,
	const FVoxelLandM5CandidateResult& Candidate, const FVoxelLandM5Budget& SourceBudget,
	TFunctionRef<bool()> IsCancelled, TArray<int16>& OutSamples)
{
	if (!bNetworkEnabled || !Candidate.HasOwnedCanonicalPlan(SourceBudget) || Plans.Num() != 1
		|| Plans[0].Get() != &Candidate.GetPlan() || Candidate.GetPlan().GeometryVersion != 2)
	{
		return false;
	}
	return FVoxelLandM5ChunkCarveInternals::TryCarveInternal(Baseline, Size, OriginCm, SpacingCm, VoxelSizeCm,
		bNetworkEnabled, bPocketEnabled, Plans, NetworkAllowed, PocketAllowed, Protected, true, PocketPresent,
		PocketDistanceQ8, MaxScratchBytes, MaxWork, true, IsCancelled, OutSamples);
}
}
