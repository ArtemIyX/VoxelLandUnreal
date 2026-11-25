#include "Generation/VoxelLandM5FinalField.h"

#include "Data/Structs/VoxelLandM5CavePlan.h"
#include "Data/Structs/VoxelLandM5FinalFieldInputs.h"
#include "Generation/VoxelLandM5Budget.h"
#include "Generation/VoxelLandM5BudgetLease.h"
#include "Generation/VoxelLandM5CheckedMath.h"
#include "Generation/VoxelLandM5ChunkCarve.h"
#include "Generation/VoxelLandM5PocketMapping.h"
#include "Generation/VoxelLandM5PocketNoise.h"

class FVoxelLandM5FinalFieldInternals final
{
public:
	static constexpr int64 CoordinateLimit = 1048576;
	static constexpr int64 ArrayAllowance = 4096;
	// Packet/digest arrays and bounded SHA stack are reused one sample at a time.
	static constexpr int64 SamplerScratchAllowance = 8192;
	static constexpr int64 PocketWorkPerSample = 4096;

	static FVoxelLandM5Diagnostic Error(EVoxelLandM5ErrorCode Code, const TCHAR* Message)
	{
		FVoxelLandM5Diagnostic Result;
		Result.Code = Code;
		Result.Stage = TEXT("M5.FinalField");
		Result.Message = Message;
		return Result;
	}

	static bool IsBoxValid(const FVoxelLandM5FinalFieldInputs& Inputs, bool bEnforceCaveDomain)
	{
		using namespace VoxelLand::M5::CheckedMath;
		const int64 Origins[] = {Inputs.OriginCm.X, Inputs.OriginCm.Y, Inputs.OriginCm.Z};
		const int32 Sizes[] = {Inputs.Size.X, Inputs.Size.Y, Inputs.Size.Z};
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			int64 Offset = 0;
			int64 End = 0;
			if (!TryMultiplyNonNegative(int64(Sizes[Axis]) - 1, Inputs.SpacingCm, Offset)
				|| !TryAdd(Origins[Axis], Offset, End)
				|| (bEnforceCaveDomain && (Origins[Axis] < -CoordinateLimit || End > CoordinateLimit)))
			{
				return false;
			}
		}
		return true;
	}
};

FVoxelLandM5FinalField::FVoxelLandM5FinalField(TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe>&& InLease)
	: Lease(MoveTemp(InLease))
{
}

FVoxelLandM5Diagnostic FVoxelLandM5FinalField::Build(const FVoxelLandM5FinalFieldInputs& Inputs,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled,
	TSharedPtr<const FVoxelLandM5FinalField, ESPMode::ThreadSafe>& OutField)
{
	using namespace VoxelLand::M5::CheckedMath;
	using E = EVoxelLandM5ErrorCode;
	if (IsCancelled())
	{
		return FVoxelLandM5FinalFieldInternals::Error(E::Cancelled, TEXT("Final field cancelled"));
	}
	const bool bPocket = Inputs.Pockets.bEnabled && Inputs.Pockets.AmplitudeCm != 0;
	const bool bCompose = Inputs.bNetworkEnabled || bPocket;
	if ((Inputs.Pockets.bEnabled && Inputs.PocketNoiseVersion != 1)
		|| ((Inputs.bNetworkEnabled || Inputs.Pockets.bEnabled) && Inputs.CompositionVersion != 2)
		|| (Inputs.bNetworkEnabled && Inputs.GeometryVersion != 1))
	{
		return FVoxelLandM5FinalFieldInternals::Error(
			E::UnsupportedVersion, TEXT("Unsupported final field algorithm version"));
	}
	if (Inputs.Size.X <= 0 || Inputs.Size.Y <= 0 || Inputs.Size.Z <= 0 || Inputs.SpacingCm <= 0
		|| Inputs.VoxelSizeCm <= 0
		|| (Inputs.Pockets.bEnabled
			&& (!Inputs.StageId.IsValid() || Inputs.Pockets.AmplitudeCm < 0
				|| Inputs.Pockets.AmplitudeCm > FVoxelLandM5FinalFieldInternals::CoordinateLimit))
		|| (bPocket
			&& (Inputs.Pockets.LatticeSpacingCm <= 0
				|| Inputs.Pockets.LatticeSpacingCm > FVoxelLandM5FinalFieldInternals::CoordinateLimit
				|| Inputs.Pockets.ThresholdQ16 < 0 || Inputs.Pockets.ThresholdQ16 > 65535)))
	{
		return FVoxelLandM5FinalFieldInternals::Error(E::InvalidInput, TEXT("Invalid final field settings"));
	}
	if (!FVoxelLandM5FinalFieldInternals::IsBoxValid(Inputs, bCompose))
	{
		return FVoxelLandM5FinalFieldInternals::Error(E::OutOfDomain, TEXT("Final field sample box is unsupported"));
	}
	int64 XY = 0;
	int64 Count = 0;
	if (!TryMultiplyNonNegative(Inputs.Size.X, Inputs.Size.Y, XY) || !TryMultiplyNonNegative(XY, Inputs.Size.Z, Count)
		|| Count > MAX_int32)
	{
		return FVoxelLandM5FinalFieldInternals::Error(E::ArithmeticOverflow, TEXT("Final field sample count overflow"));
	}
	if (Inputs.Baseline.Num() != Count || ((Inputs.bNetworkEnabled || bPocket) && Inputs.Protected.Num() != Count)
		|| (Inputs.bNetworkEnabled && (Inputs.Plans.IsEmpty() || Inputs.NetworkAllowed.Num() != Count))
		|| (bPocket && Inputs.PocketAllowed.Num() != Count))
	{
		return FVoxelLandM5FinalFieldInternals::Error(E::InvalidInput, TEXT("Final field input size mismatch"));
	}
	int64 PerSample = 2;
	int64 PlanInventoryWork = Inputs.bNetworkEnabled ? Inputs.Plans.Num() : 0;
	if (Inputs.bNetworkEnabled)
	{
		if (!Budget->TryChargeWork(Inputs.Plans.Num()))
		{
			return FVoxelLandM5FinalFieldInternals::Error(
				E::BudgetExceeded, TEXT("Final field plan inventory budget exhausted"));
		}
		for (const TSharedPtr<const FVoxelLandM5CavePlan, ESPMode::ThreadSafe>& Plan : Inputs.Plans)
		{
			if (IsCancelled())
			{
				return FVoxelLandM5FinalFieldInternals::Error(E::Cancelled, TEXT("Final field cancelled"));
			}
			if (!Plan.IsValid() || Plan->GeometryVersion != Inputs.GeometryVersion)
			{
				return FVoxelLandM5FinalFieldInternals::Error(E::InvalidInput, TEXT("Final field plan is missing"));
			}
			const int64 PrimitiveCount = int64(Plan->Chambers.Num()) + Plan->Segments.Num();
			if (!TryAdd(PlanInventoryWork, PrimitiveCount, PlanInventoryWork))
				return FVoxelLandM5FinalFieldInternals::Error(E::ArithmeticOverflow, TEXT("Profile inventory overflow"));
			if (!Budget->TryChargeWork(PrimitiveCount))
				return FVoxelLandM5FinalFieldInternals::Error(E::BudgetExceeded, TEXT("Profile inventory budget exhausted"));
			for (const auto& Chamber : Plan->Chambers)
			{
				if (IsCancelled()) return FVoxelLandM5FinalFieldInternals::Error(E::Cancelled, TEXT("Final field cancelled"));
				if (Chamber.Profile.Version != Inputs.GeometryVersion)
					return FVoxelLandM5FinalFieldInternals::Error(E::InvalidInput, TEXT("Chamber profile version mismatch"));
			}
			for (const auto& Segment : Plan->Segments)
			{
				if (IsCancelled()) return FVoxelLandM5FinalFieldInternals::Error(E::Cancelled, TEXT("Final field cancelled"));
				if (Segment.Profile.Version != Inputs.GeometryVersion)
					return FVoxelLandM5FinalFieldInternals::Error(E::InvalidInput, TEXT("Segment profile version mismatch"));
			}
			int64 PrimitiveWork = 0;
			if (!TryMultiplyNonNegative(int64(Plan->Chambers.Num()) + Plan->Segments.Num(), 1, PrimitiveWork)
				|| !TryAdd(PerSample, 1, PerSample) || !TryAdd(PerSample, PrimitiveWork, PerSample))
			{
				return FVoxelLandM5FinalFieldInternals::Error(E::ArithmeticOverflow, TEXT("Final field work overflow"));
			}
		}
	}
	int64 Work = 0;
	int64 OutputBytes = 0;
	int64 ScratchBytes = 0;
	if (!TryMultiplyNonNegative(Count, PerSample, Work)
		|| !TryMultiplyNonNegative(Count, 2 * int64(sizeof(int16)), OutputBytes)
		|| !TryAdd(OutputBytes, FVoxelLandM5FinalFieldInternals::ArrayAllowance, OutputBytes)
		|| (bPocket
			&& (!TryMultiplyNonNegative(Count, 2 * int64(sizeof(uint8) + sizeof(int64)), ScratchBytes)
				|| !TryAdd(ScratchBytes, FVoxelLandM5FinalFieldInternals::SamplerScratchAllowance, ScratchBytes))))
	{
		return FVoxelLandM5FinalFieldInternals::Error(E::ArithmeticOverflow, TEXT("Final field admission overflow"));
	}
	// Admit the mask inventory before reading caller arrays; sampling charges follow
	// only after all binary masks have been validated.
	if (!Budget->TryChargeWork(Work) || ((Inputs.bNetworkEnabled || bPocket) && !Budget->TryChargeWork(Count)))
	{
		return FVoxelLandM5FinalFieldInternals::Error(E::BudgetExceeded, TEXT("Final field work budget exhausted"));
	}
	int64 EligiblePockets = 0;
	for (int32 Index = 0; Index < Count && (Inputs.bNetworkEnabled || bPocket); ++Index)
	{
		if (Index % 64 == 0 && IsCancelled())
		{
			return FVoxelLandM5FinalFieldInternals::Error(E::Cancelled, TEXT("Final field cancelled"));
		}
		if (Inputs.Protected[Index] > 1 || (Inputs.bNetworkEnabled && Inputs.NetworkAllowed[Index] > 1)
			|| (bPocket && Inputs.PocketAllowed[Index] > 1))
		{
			return FVoxelLandM5FinalFieldInternals::Error(E::InvalidInput, TEXT("Final field mask is not binary"));
		}
		EligiblePockets += bPocket && Inputs.PocketAllowed[Index] && !Inputs.Protected[Index] ? 1 : 0;
	}
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> OutputLease;
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> ScratchLease;
	if (!Budget->TryReserveBytes(OutputBytes, OutputLease)
		|| (bPocket && !Budget->TryReserveBytes(ScratchBytes, ScratchLease)))
	{
		return FVoxelLandM5FinalFieldInternals::Error(E::BudgetExceeded, TEXT("Final field memory budget exhausted"));
	}
	TUniquePtr<FVoxelLandM5PocketNoiseCache> Cache;
	if (EligiblePockets > 0)
	{
		FVoxelLandM5PointCm Last = Inputs.OriginCm;
		Last.X += int64(Inputs.Size.X - 1) * Inputs.SpacingCm;
		Last.Y += int64(Inputs.Size.Y - 1) * Inputs.SpacingCm;
		Last.Z += int64(Inputs.Size.Z - 1) * Inputs.SpacingCm;
		const auto CacheResult = FVoxelLandM5PocketNoiseCache::Build(Inputs.OriginCm, Last,
			Inputs.Pockets.LatticeSpacingCm, Inputs.WorldSeed, Inputs.StageId, uint64(Inputs.Pockets.Salt),
			Budget, IsCancelled, Cache);
		if (CacheResult.Code != E::None && CacheResult.Code != E::BudgetExceeded) return CacheResult;
		if (!Cache)
		{
			int64 PocketWork = 0;
			if (!TryMultiplyNonNegative(EligiblePockets, FVoxelLandM5FinalFieldInternals::PocketWorkPerSample, PocketWork))
				return FVoxelLandM5FinalFieldInternals::Error(E::ArithmeticOverflow, TEXT("Pocket sampler work overflow"));
			if (!Budget->TryChargeWork(PocketWork))
				return FVoxelLandM5FinalFieldInternals::Error(E::BudgetExceeded, TEXT("Pocket sampler work budget exhausted"));
		}
	}
	TArray<uint8> Present;
	TArray<int64> Distances;
	if (bPocket)
	{
		Present.SetNumZeroed(static_cast<int32>(Count));
		Distances.SetNumZeroed(static_cast<int32>(Count));
		int32 Index = 0;
		for (int32 Z = 0; Z < Inputs.Size.Z; ++Z)
		{
			for (int32 Y = 0; Y < Inputs.Size.Y; ++Y)
			{
				for (int32 X = 0; X < Inputs.Size.X; ++X, ++Index)
				{
					if (IsCancelled())
					{
						return FVoxelLandM5FinalFieldInternals::Error(E::Cancelled, TEXT("Final field cancelled"));
					}
					if (!Inputs.PocketAllowed[Index] || Inputs.Protected[Index])
					{
						continue;
					}
					int32 Noise = 0;
					bool bHas = false;
					int64 Distance = 0;
					const int64 PX = Inputs.OriginCm.X + int64(X) * Inputs.SpacingCm;
					const int64 PY = Inputs.OriginCm.Y + int64(Y) * Inputs.SpacingCm;
					const int64 PZ = Inputs.OriginCm.Z + int64(Z) * Inputs.SpacingCm;
					if (Cache)
					{
						const auto SampleResult = Cache->Sample(PX, PY, PZ, IsCancelled, Noise);
						if (SampleResult.Code != E::None) return SampleResult;
					}
					else if (!VoxelLand::M5::PocketNoise::TrySample(PX, PY, PZ, Inputs.Pockets.LatticeSpacingCm,
						Inputs.WorldSeed, Inputs.StageId, uint64(Inputs.Pockets.Salt), IsCancelled, Noise))
						return FVoxelLandM5FinalFieldInternals::Error(IsCancelled() ? E::Cancelled : E::Unproven, TEXT("Pocket sampling failed"));
					if (!VoxelLand::M5::PocketMapping::TryMap(
							true, Noise, Inputs.Pockets.ThresholdQ16, Inputs.Pockets.AmplitudeCm, bHas, Distance))
					{
						return FVoxelLandM5FinalFieldInternals::Error(
							IsCancelled() ? E::Cancelled : E::Unproven, TEXT("Pocket sampling failed"));
					}
					Present[Index] = bHas ? 1 : 0;
					Distances[Index] = Distance;
				}
			}
		}
	}
	// ChunkCarve independently validates every plan and primitive before sampling.
	// Charge that second inventory scan as well as this builder's scan.
	if (!Budget->TryChargeWork(PlanInventoryWork))
		return FVoxelLandM5FinalFieldInternals::Error(E::BudgetExceeded, TEXT("Carve inventory budget exhausted"));
	int64 CarveWork = 0;
	if (!TryAdd(Work, PlanInventoryWork, CarveWork))
		return FVoxelLandM5FinalFieldInternals::Error(E::ArithmeticOverflow, TEXT("Carve work overflow"));
	bool bCarveCancelled = false;
	const auto CheckCarveCancelled = [&IsCancelled, &bCarveCancelled]() {
		bCarveCancelled = bCarveCancelled || IsCancelled();
		return bCarveCancelled;
	};
	TArray<int16> Samples;
	if (!VoxelLand::M5::ChunkCarve::TryCarve(Inputs.Baseline, Inputs.Size, Inputs.OriginCm, Inputs.SpacingCm,
			Inputs.VoxelSizeCm, Inputs.bNetworkEnabled, bPocket, Inputs.Plans, Inputs.NetworkAllowed,
			Inputs.PocketAllowed, Inputs.Protected, Present, Distances, OutputBytes, CarveWork, CheckCarveCancelled, Samples))
	{
		return FVoxelLandM5FinalFieldInternals::Error(
			bCarveCancelled ? E::Cancelled : E::Unproven, TEXT("Final field composition failed"));
	}
	if (IsCancelled())
	{
		return FVoxelLandM5FinalFieldInternals::Error(E::Cancelled, TEXT("Final field cancelled"));
	}
	TSharedPtr<FVoxelLandM5FinalField, ESPMode::ThreadSafe> Result =
		MakeShareable(new FVoxelLandM5FinalField(MoveTemp(OutputLease)));
	Result->Density = MoveTemp(Samples);
	OutField = MoveTemp(Result);
	return {};
}
