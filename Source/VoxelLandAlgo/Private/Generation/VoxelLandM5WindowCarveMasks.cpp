#include "Generation/VoxelLandM5WindowCarveMasks.h"

#include "Data/Structs/VoxelLandM5CaveSettings.h"
#include "Generation/VoxelLandM5BaselineWindow.h"
#include "Data/Structs/VoxelLandM5WorldSnapshot.h"
#include "Generation/VoxelLandM5BoundBaseline.h"
#include "Generation/VoxelLandM5InfluenceSet.h"
#include "Generation/VoxelLandM5PocketSurfaceColumns.h"
#include "Generation/VoxelLandM5ValidatedDefinition.h"
#include "Generation/VoxelLandM5Budget.h"
#include "Generation/VoxelLandM5BudgetLease.h"
#include "Generation/VoxelLandM5CandidateResult.h"
#include "Generation/VoxelLandM5CarveMasks.h"
#include "Generation/VoxelLandM5CheckedMath.h"
#include "Generation/VoxelLandM5EntranceMask.h"
#include "Generation/VoxelLandM5ProtectionGrid.h"
#include "Generation/VoxelLandM5ProtectionSamples.h"

class FVoxelLandM5OwnedWindowCarveMasks final
{
public:
	static FVoxelLandM5Diagnostic Build(const FVoxelLandM5CaveSettings& Settings, int64 WorldSeed,
		const FVoxelLandM5BaselineWindow& Window, TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> Candidate,
		const FVoxelLandM5ProtectionGrid* Protection, FIntVector Start, FIntVector Count,
		TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled,
		TSharedPtr<const FVoxelLandM5CarveMasks, ESPMode::ThreadSafe>& OutMasks, const FVoxelLandM5PocketSurfaceColumns* Columns,
		TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> SourceBudget);
};
class FVoxelLandM5WindowCarveMasksInternals final
{
public:
	static FVoxelLandM5Diagnostic Error(EVoxelLandM5ErrorCode Code, const TCHAR* Message)
	{
		FVoxelLandM5Diagnostic Result;
		Result.Code = Code;
		Result.Stage = TEXT("M5.WindowCarveMasks");
		Result.Message = Message;
		return Result;
	}
};

FVoxelLandM5Diagnostic VoxelLand::M5::WindowCarveMasks::Build(const FVoxelLandM5CaveSettings& Settings,
	int64 WorldSeed, const FVoxelLandM5BaselineWindow& Window,
	TSharedRef<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> Candidate,
	const FVoxelLandM5ProtectionGrid* Protection, FIntVector Start, FIntVector Count,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled,
	TSharedPtr<const FVoxelLandM5CarveMasks, ESPMode::ThreadSafe>& OutMasks)
{
	return Build(Settings, WorldSeed, Window,
		TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe>(Candidate), Protection,
		Start, Count, Budget, IsCancelled, OutMasks);
}

FVoxelLandM5Diagnostic VoxelLand::M5::WindowCarveMasks::Build(const FVoxelLandM5CaveSettings& Settings,
	int64 WorldSeed, const FVoxelLandM5BaselineWindow& Window,
	TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> Candidate,
	const FVoxelLandM5ProtectionGrid* Protection, FIntVector Start, FIntVector Count,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled,
	TSharedPtr<const FVoxelLandM5CarveMasks, ESPMode::ThreadSafe>& OutMasks)
{
	return FVoxelLandM5OwnedWindowCarveMasks::Build(Settings, WorldSeed, Window, Candidate, Protection, Start, Count, Budget, IsCancelled, OutMasks, nullptr, Budget);
}

FVoxelLandM5Diagnostic FVoxelLandM5OwnedWindowCarveMasks::Build(const FVoxelLandM5CaveSettings& Settings,
	int64 WorldSeed, const FVoxelLandM5BaselineWindow& Window,
	TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> Candidate,
	const FVoxelLandM5ProtectionGrid* Protection, FIntVector Start, FIntVector Count,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled,
	TSharedPtr<const FVoxelLandM5CarveMasks, ESPMode::ThreadSafe>& OutMasks, const FVoxelLandM5PocketSurfaceColumns* Columns,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> SourceBudget)
{
	using namespace VoxelLand::M5::CheckedMath;
	using E = EVoxelLandM5ErrorCode;
	if (IsCancelled())
	{
		return FVoxelLandM5WindowCarveMasksInternals::Error(E::Cancelled, TEXT("Window mask assembly cancelled"));
	}
	int64 MaxBytes = 0, MaxWork = 0;
	Budget->GetLimits(MaxBytes, MaxWork);
	if (Settings.PlanningMode != EVoxelLandM5PlanningMode::Finite || !Settings.StageId.IsValid()
		|| Settings.bEnabled != Candidate.IsValid() || (!Settings.bEnabled && !Settings.Pockets.bEnabled)
		|| (Settings.Pockets.bEnabled && (Settings.Pockets.AmplitudeCm < 0 || Settings.Pockets.AmplitudeCm > 1048576))
		|| Settings.Limits.MaxBytes < 1 || Settings.Limits.MaxValidationWork < 1
		|| MaxBytes > Settings.Limits.MaxBytes || MaxWork > Settings.Limits.MaxValidationWork
		|| !Window.IsOwnedBy(*Budget)
		|| (Candidate && !Candidate->UsesBudget(*SourceBudget)) || (Protection && !Protection->IsOwnedBy(*SourceBudget)))
	{
		return FVoxelLandM5WindowCarveMasksInternals::Error(E::InvalidInput, TEXT("Invalid finite mask inputs or budget ownership"));
	}
	if (Settings.Water.Policy != EVoxelLandM5WaterPolicy::DryWorld)
	{
		return FVoxelLandM5WindowCarveMasksInternals::Error(E::Unproven, TEXT("Dry world input is required"));
	}
	if (Settings.Protection.bRequireMask && !Protection)
	{
		return FVoxelLandM5WindowCarveMasksInternals::Error(E::Unproven, TEXT("Required protection grid is missing"));
	}
	const FIntVector Size = Window.GetSize();
	const int64 Spacing = Window.GetVoxelSizeCm();
	if (Size.X <= 0 || Size.Y <= 0 || Size.Z <= 0 || Spacing <= 0 || Start.X < 0 || Start.Y < 0
		|| Start.Z < 0 || Count.X <= 0 || Count.Y <= 0 || Count.Z <= 0
		|| int64(Start.X) + Count.X > Size.X || int64(Start.Y) + Count.Y > Size.Y
		|| int64(Start.Z) + Count.Z > Size.Z || (Protection && Protection->GetSpacingCm() != Spacing))
	{
		return FVoxelLandM5WindowCarveMasksInternals::Error(E::InvalidInput, TEXT("Invalid window, selection or protection spacing"));
	}
	int64 XY = 0, Samples = 0, OutputXY = 0, Outputs = 0;
	int64 ScratchBytes = 0, Work = 0;
	if (!TryMultiplyNonNegative(Size.X, Size.Y, XY) || !TryMultiplyNonNegative(XY, Size.Z, Samples)
		|| !TryMultiplyNonNegative(Count.X, Count.Y, OutputXY) || !TryMultiplyNonNegative(OutputXY, Count.Z, Outputs)
		|| Samples > MAX_int32 || Outputs > MAX_int32 || Window.GetDensity().Num() != Samples
		|| !TryMultiplyNonNegative(Samples, 2, ScratchBytes) || !TryAdd(ScratchBytes, Outputs, ScratchBytes)
		|| !TryAdd(ScratchBytes, 4096, ScratchBytes) || !TryAdd(Samples, Outputs, Work)
		|| !TryAdd(Work, Samples, Work) || !TryAdd(Work, Outputs, Work) || !TryAdd(Work, 1, Work))
	{
		return FVoxelLandM5WindowCarveMasksInternals::Error(E::ArithmeticOverflow, TEXT("Window mask admission overflow"));
	}
	if (!Budget->TryChargeWork(Work))
	{
		return FVoxelLandM5WindowCarveMasksInternals::Error(E::BudgetExceeded, TEXT("Window mask assembly work exhausted"));
	}
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> ScratchLease;
	if (!Budget->TryReserveBytes(ScratchBytes, ScratchLease))
	{
		return FVoxelLandM5WindowCarveMasksInternals::Error(E::BudgetExceeded, TEXT("Window mask assembly memory exhausted"));
	}
	TSharedPtr<const FVoxelLandM5ProtectionSamples, ESPMode::ThreadSafe> Projected;
	if (Protection)
	{
		const FVoxelLandM5Diagnostic Result = Protection->ProjectForJob(Size, Window.GetOriginCm(), SourceBudget, Budget, IsCancelled, Projected);
		if (Result.Code != E::None)
		{
			return Result;
		}
	}
	TArray<uint8> FullProtection, FullEntrance, SelectedProtection;
	FullProtection.SetNumUninitialized(int32(Samples));
	FullEntrance.SetNumUninitialized(int32(Samples));
	SelectedProtection.SetNumUninitialized(int32(Outputs));
	const TConstArrayView<uint8> Source = Projected ? Projected->GetSamples() : TConstArrayView<uint8>();
	for (int32 Index = 0; Index < Samples; ++Index)
	{
		if ((Index & 1023) == 0 && IsCancelled())
		{
			return FVoxelLandM5WindowCarveMasksInternals::Error(E::Cancelled, TEXT("Window mask assembly cancelled"));
		}
		FullProtection[Index] = Settings.Protection.bProtectAll || (Projected && Source[Index] != 0) ? 1 : 0;
		FullEntrance[Index] = 0;
	}
	int32 OutputIndex = 0;
	for (int64 Z = 0; Z < Count.Z; ++Z)
	{
		for (int64 Y = 0; Y < Count.Y; ++Y)
		{
			for (int64 X = 0; X < Count.X; ++X, ++OutputIndex)
			{
				if ((OutputIndex & 1023) == 0 && IsCancelled())
				{
					return FVoxelLandM5WindowCarveMasksInternals::Error(E::Cancelled, TEXT("Window mask assembly cancelled"));
				}
				const int64 Index = Start.X + X + int64(Size.X) * (Start.Y + Y) + XY * (Start.Z + Z);
				SelectedProtection[OutputIndex] = FullProtection[int32(Index)];
			}
		}
	}
	if (Candidate)
	{
		TArray<TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe>> Candidates;
		Candidates.Add(Candidate);
		TSharedPtr<const FVoxelLandM5EntranceMask, ESPMode::ThreadSafe> Entrance;
		const FVoxelLandM5Diagnostic EntranceResult = FVoxelLandM5EntranceMask::Build(Size, Window.GetOriginCm(), Spacing,
			Start, Count, SelectedProtection, Settings, WorldSeed, Candidates, Budget, IsCancelled, Entrance);
		if (EntranceResult.Code != E::None)
		{
			return EntranceResult;
		}
		if (!Entrance || Entrance->GetAuthorized().Num() != Outputs)
		{
			return FVoxelLandM5WindowCarveMasksInternals::Error(E::Unproven, TEXT("Entrance mask output missing"));
		}
		OutputIndex = 0;
		for (int64 Z = 0; Z < Count.Z; ++Z)
		{
			for (int64 Y = 0; Y < Count.Y; ++Y)
			{
				for (int64 X = 0; X < Count.X; ++X, ++OutputIndex)
				{
					if ((OutputIndex & 1023) == 0 && IsCancelled())
					{
						return FVoxelLandM5WindowCarveMasksInternals::Error(E::Cancelled, TEXT("Window mask assembly cancelled"));
					}
					const int64 Index = Start.X + X + int64(Size.X) * (Start.Y + Y) + XY * (Start.Z + Z);
					FullEntrance[int32(Index)] = Entrance->GetAuthorized()[OutputIndex];
				}
			}
		}
	}
	return FVoxelLandM5CarveMasks::BuildImpl(Size, Window.GetOriginCm(), Spacing, Start, Count, Window.GetDensity(),
		FullProtection, FullEntrance, Settings.RoofThicknessCm,
		Settings.Pockets.bEnabled && Settings.Pockets.AmplitudeCm != 0,
		Settings.Pockets.MinDepthCm, Settings.Pockets.MaxDepthCm, true, Budget, IsCancelled, OutMasks, Columns);
}

FVoxelLandM5Diagnostic VoxelLand::M5::WindowCarveMasks::BuildOwned(const FVoxelLandM5BoundBaseline& Baseline,
	const FVoxelLandM5InfluenceSet& Influence, const FVoxelLandM5PocketSurfaceColumns& Columns,
	const FVoxelLandM5BaselineWindow& Window, TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> Candidate,
	FIntVector Start, FIntVector Count, TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget,
	TFunctionRef<bool()> IsCancelled, TSharedPtr<const FVoxelLandM5CarveMasks, ESPMode::ThreadSafe>& OutMasks)
{
	if (&Baseline.GetDefinition() != &Influence.GetDefinition() || !Baseline.UsesBudget(*Budget)
		|| !Influence.UsesBudget(*Budget) || !Columns.Matches(Baseline, Influence, *Budget)
		|| &Window.GetGenerator() != &Baseline.GetGenerator() || !Window.IsOwnedBy(*Budget))
		return FVoxelLandM5WindowCarveMasksInternals::Error(EVoxelLandM5ErrorCode::InvalidInput, TEXT("Owned mask source mismatch"));
	const auto& Snapshot = Baseline.GetDefinition().GetSnapshot();
	return FVoxelLandM5OwnedWindowCarveMasks::Build(Snapshot.Caves, Snapshot.Base.Seed, Window, Candidate,
		Baseline.GetDefinition().GetProtection(), Start, Count, Budget, IsCancelled, OutMasks, &Columns, Budget);
}

FVoxelLandM5Diagnostic VoxelLand::M5::WindowCarveMasks::BuildOwnedForJob(const FVoxelLandM5BoundBaseline& Baseline,
	const FVoxelLandM5InfluenceSet& Influence, const FVoxelLandM5PocketSurfaceColumns& Columns,
	const FVoxelLandM5BaselineWindow& Window, FIntVector Start, FIntVector Count,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> SourceBudget,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> JobBudget, TFunctionRef<bool()> IsCancelled,
	TSharedPtr<const FVoxelLandM5CarveMasks, ESPMode::ThreadSafe>& OutMasks)
{
	if (&Baseline.GetDefinition() != &Influence.GetDefinition() || !Baseline.UsesBudget(*SourceBudget)
		|| !Influence.UsesBudget(*SourceBudget) || !Columns.Matches(Baseline, Influence, *JobBudget)
		|| &Window.GetGenerator() != &Baseline.GetGenerator() || !Window.IsOwnedBy(*JobBudget))
		return FVoxelLandM5WindowCarveMasksInternals::Error(EVoxelLandM5ErrorCode::InvalidInput, TEXT("Owned mask source mismatch"));
	const auto& Snapshot = Baseline.GetDefinition().GetSnapshot();
	if (Snapshot.Caves.bEnabled)
		return FVoxelLandM5WindowCarveMasksInternals::Error(EVoxelLandM5ErrorCode::Unproven, TEXT("Network contributor completeness unavailable"));
	return FVoxelLandM5OwnedWindowCarveMasks::Build(Snapshot.Caves, Snapshot.Base.Seed, Window, nullptr,
		Baseline.GetDefinition().GetProtection(), Start, Count, JobBudget, IsCancelled, OutMasks, &Columns, SourceBudget);
}
