#include "Generation/VoxelLandM5WindowFinalField.h"

#include "Data/Structs/VoxelLandM5CaveSettings.h"
#include "Data/Structs/VoxelLandM5FinalFieldInputs.h"
#include "Data/Structs/VoxelLandM5StageManifest.h"
#include "Generation/VoxelLandM5BaselineWindow.h"
#include "Generation/VoxelLandM5Budget.h"
#include "Generation/VoxelLandM5BudgetLease.h"
#include "Generation/VoxelLandM5CandidateResult.h"
#include "Generation/VoxelLandM5CarveMasks.h"
#include "Generation/VoxelLandM5CheckedMath.h"
#include "Generation/VoxelLandM5FinalField.h"
#include "Generation/VoxelLandM5WindowCarveMasks.h"

class FVoxelLandM5WindowFinalFieldInternals final
{
public:
	static FVoxelLandM5Diagnostic Error(EVoxelLandM5ErrorCode Code, const TCHAR* Message)
	{
		FVoxelLandM5Diagnostic Result;
		Result.Code = Code;
		Result.Stage = TEXT("M5.WindowFinalField");
		Result.Message = Message;
		return Result;
	}

	// The shared wrapper is local to the synchronous call; Candidate retains
	// ownership of the embedded plan and its lease until after Build returns.
	struct FBorrowedPlanDeleter
	{
		void operator()(const FVoxelLandM5CavePlan*) const {}
	};
};

FVoxelLandM5Diagnostic VoxelLand::M5::WindowFinalField::Build(const FVoxelLandM5CaveSettings& Settings,
	const FVoxelLandM5StageManifest& Manifest, int64 WorldSeed, const FVoxelLandM5BaselineWindow& Window,
	TSharedRef<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> Candidate,
	const FVoxelLandM5ProtectionGrid* Protection, FIntVector Start, FIntVector Count,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled,
	TSharedPtr<const FVoxelLandM5FinalField, ESPMode::ThreadSafe>& OutField)
{
	return Build(Settings, Manifest, WorldSeed, Window,
		TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe>(Candidate), Protection,
		Start, Count, Budget, IsCancelled, OutField);
}

FVoxelLandM5Diagnostic VoxelLand::M5::WindowFinalField::Build(const FVoxelLandM5CaveSettings& Settings,
	const FVoxelLandM5StageManifest& Manifest, int64 WorldSeed, const FVoxelLandM5BaselineWindow& Window,
	TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> Candidate,
	const FVoxelLandM5ProtectionGrid* Protection, FIntVector Start, FIntVector Count,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled,
	TSharedPtr<const FVoxelLandM5FinalField, ESPMode::ThreadSafe>& OutField)
{
	using namespace VoxelLand::M5::CheckedMath;
	using E = EVoxelLandM5ErrorCode;
	if (IsCancelled())
	{
		return FVoxelLandM5WindowFinalFieldInternals::Error(E::Cancelled, TEXT("Window field cancelled"));
	}
	if (Settings.bEnabled && (Manifest.Geometry != 1 || (Candidate && Candidate->GetPlan().GeometryVersion != Manifest.Geometry)))
	{
		return FVoxelLandM5WindowFinalFieldInternals::Error(E::UnsupportedVersion, TEXT("Unsupported or mismatched geometry version"));
	}
	if ((Settings.bEnabled || Settings.Pockets.bEnabled) && Manifest.Composition != 2)
	{
		return FVoxelLandM5WindowFinalFieldInternals::Error(E::UnsupportedVersion, TEXT("Unsupported composition version"));
	}
	if (Settings.Pockets.bEnabled && Manifest.PocketNoise != 1)
	{
		return FVoxelLandM5WindowFinalFieldInternals::Error(E::UnsupportedVersion, TEXT("Unsupported pocket noise version"));
	}
	const FIntVector Size = Window.GetSize();
	if (Size.X <= 0 || Size.Y <= 0 || Size.Z <= 0 || Start.X < 0 || Start.Y < 0 || Start.Z < 0
		|| Count.X <= 0 || Count.Y <= 0 || Count.Z <= 0 || int64(Start.X) + Count.X > Size.X
		|| int64(Start.Y) + Count.Y > Size.Y || int64(Start.Z) + Count.Z > Size.Z || Window.GetVoxelSizeCm() <= 0)
	{
		return FVoxelLandM5WindowFinalFieldInternals::Error(E::InvalidInput, TEXT("Invalid window selection"));
	}
	int64 XY = 0, SourceCount = 0, OutputXY = 0, OutputCount = 0, Bytes = 0, Work = 0;
	if (!TryMultiplyNonNegative(Size.X, Size.Y, XY) || !TryMultiplyNonNegative(XY, Size.Z, SourceCount)
		|| !TryMultiplyNonNegative(Count.X, Count.Y, OutputXY)
		|| !TryMultiplyNonNegative(OutputXY, Count.Z, OutputCount) || OutputCount > MAX_int32
		|| SourceCount != Window.GetDensity().Num() || !TryMultiplyNonNegative(OutputCount, 2 * int64(sizeof(int16)), Bytes)
		|| !TryAdd(Bytes, 4096, Bytes) || !TryAdd(OutputCount, 1, Work))
	{
		return FVoxelLandM5WindowFinalFieldInternals::Error(E::ArithmeticOverflow, TEXT("Window field admission overflow"));
	}
	const FVoxelLandM5PointCm BaseOrigin = Window.GetOriginCm();
	FVoxelLandM5PointCm Origin;
	int64 Offset = 0;
	const int64 Spacing = Window.GetVoxelSizeCm();
	if (!TryMultiplyNonNegative(Start.X, Spacing, Offset) || !TryAdd(BaseOrigin.X, Offset, Origin.X)
		|| !TryMultiplyNonNegative(Start.Y, Spacing, Offset) || !TryAdd(BaseOrigin.Y, Offset, Origin.Y)
		|| !TryMultiplyNonNegative(Start.Z, Spacing, Offset) || !TryAdd(BaseOrigin.Z, Offset, Origin.Z))
	{
		return FVoxelLandM5WindowFinalFieldInternals::Error(E::ArithmeticOverflow, TEXT("Window field origin overflow"));
	}
	TSharedPtr<const FVoxelLandM5CarveMasks, ESPMode::ThreadSafe> Masks;
	FVoxelLandM5Diagnostic Result = VoxelLand::M5::WindowCarveMasks::Build(Settings, WorldSeed, Window,
		Candidate, Protection, Start, Count, Budget, IsCancelled, Masks);
	if (Result.Code != E::None)
	{
		return Result;
	}
	if (!Masks)
	{
		return FVoxelLandM5WindowFinalFieldInternals::Error(E::Unproven, TEXT("Window masks missing"));
	}
	if (!Budget->TryChargeWork(Work))
	{
		return FVoxelLandM5WindowFinalFieldInternals::Error(E::BudgetExceeded, TEXT("Window crop work exhausted"));
	}
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> ScratchLease;
	if (!Budget->TryReserveBytes(Bytes, ScratchLease))
	{
		return FVoxelLandM5WindowFinalFieldInternals::Error(E::BudgetExceeded, TEXT("Window crop memory exhausted"));
	}
	TArray<int16> Baseline;
	Baseline.SetNumUninitialized(int32(OutputCount));
	const TConstArrayView<int16> Source = Window.GetDensity();
	int32 OutIndex = 0;
	for (int64 Z = 0; Z < Count.Z; ++Z)
	{
		for (int64 Y = 0; Y < Count.Y; ++Y)
		{
			for (int64 X = 0; X < Count.X; ++X, ++OutIndex)
			{
				if ((OutIndex & 1023) == 0 && IsCancelled())
				{
					return FVoxelLandM5WindowFinalFieldInternals::Error(E::Cancelled, TEXT("Window field cancelled"));
				}
				const int64 Index = Start.X + X + int64(Size.X) * (Start.Y + Y) + XY * (Start.Z + Z);
				Baseline[OutIndex] = Source[int32(Index)];
			}
		}
	}
	TArray<TSharedPtr<const FVoxelLandM5CavePlan, ESPMode::ThreadSafe>> Plans;
	if (Candidate)
	{
		Plans.Add(MakeShareable(&Candidate->GetPlan(), FVoxelLandM5WindowFinalFieldInternals::FBorrowedPlanDeleter{}));
	}
	FVoxelLandM5FinalFieldInputs Inputs;
	Inputs.Size = Count;
	Inputs.OriginCm = Origin;
	Inputs.SpacingCm = Spacing;
	Inputs.VoxelSizeCm = Window.GetVoxelSizeCm();
	Inputs.Baseline = Baseline;
	Inputs.bNetworkEnabled = Settings.bEnabled;
	Inputs.Plans = Plans;
	Inputs.NetworkAllowed = Masks->GetNetworkAllowed();
	Inputs.PocketAllowed = Masks->GetPocketAllowed();
	Inputs.Protected = Masks->GetProtected();
	Inputs.Pockets = Settings.Pockets;
	Inputs.PocketNoiseVersion = Manifest.PocketNoise;
	Inputs.CompositionVersion = Manifest.Composition;
	Inputs.GeometryVersion = Manifest.Geometry;
	Inputs.WorldSeed = WorldSeed;
	Inputs.StageId = Settings.StageId;
	return FVoxelLandM5FinalField::Build(Inputs, Budget, IsCancelled, OutField);
}
