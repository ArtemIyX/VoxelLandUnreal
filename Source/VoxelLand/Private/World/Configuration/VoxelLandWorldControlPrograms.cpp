#include "World/Configuration/VoxelLandWorldControlPrograms.h"

#include "Data/World/Configuration/VoxelLandWorldSourceSnapshot.h"
#include "Generation/Caves/Infrastructure/VoxelLandCaveBudget.h"
#include "Generation/Caves/Infrastructure/VoxelLandCaveBudgetLease.h"
#include "Graph/Serialization/VoxelLandCaveProgramLoader.h"
#include "Graph/Capabilities/VoxelLandWorldCapabilities.h"
#include "World/Terrain/Curves/VoxelLandCurveValidation.h"
#include "World/Terrain/Controls/VoxelLandOverrideValidation.h"

class FVoxelLandWorldControlProgramsInternals final
{
public:
	static FVoxelLandM5Diagnostic Failure(EVoxelLandM5ErrorCode Code, const FString& Message)
	{
		FVoxelLandM5Diagnostic Result;
		Result.Code = Code;
		Result.Stage = TEXT("World.ControlPrograms");
		Result.Message = Message;
		return Result;
	}

	static EVoxelLandM5ErrorCode CapabilityError(EVoxelLandWorldCapabilityError Code)
	{
		switch (Code)
		{
			case EVoxelLandWorldCapabilityError::BudgetExceeded:
				return EVoxelLandM5ErrorCode::BudgetExceeded;
			case EVoxelLandWorldCapabilityError::Cancelled:
				return EVoxelLandM5ErrorCode::Cancelled;
			case EVoxelLandWorldCapabilityError::UnknownCapability:
				return EVoxelLandM5ErrorCode::ProviderUnavailable;
			default:
				return EVoxelLandM5ErrorCode::InvalidInput;
		}
	}
};

FVoxelLandWorldControlPrograms::FVoxelLandWorldControlPrograms(
	TSharedRef<const FVoxelLandWorldSourceSnapshot, ESPMode::ThreadSafe> InSource,
	TSharedRef<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> InSourceLease,
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe>&& InLease,
	TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe>&& InContinental,
	TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe>&& InMountain,
	TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe>&& InPeaks)
	: SourceLease(MoveTemp(InSourceLease))
	, Lease(MoveTemp(InLease))
	, Source(MoveTemp(InSource))
	, Continental(MoveTemp(InContinental))
	, Mountain(MoveTemp(InMountain))
	, Peaks(MoveTemp(InPeaks))
{
}

FVoxelLandM5Diagnostic FVoxelLandWorldControlPrograms::Load(
	TSharedRef<const FVoxelLandWorldSourceSnapshot, ESPMode::ThreadSafe> Source,
	TSharedRef<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> SourceLease,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled,
	TSharedPtr<const FVoxelLandWorldControlPrograms, ESPMode::ThreadSafe>& OutPrograms)
{
	if (IsCancelled())
	{
		return FVoxelLandWorldControlProgramsInternals::Failure(
			EVoxelLandM5ErrorCode::Cancelled, TEXT("Control preparation cancelled."));
	}
	if (IsInGameThread())
	{
		return FVoxelLandWorldControlProgramsInternals::Failure(
			EVoxelLandM5ErrorCode::InvalidInput, TEXT("Control preparation requires a worker thread."));
	}
	if (!Budget->OwnsLease(SourceLease.Get()))
	{
		return FVoxelLandWorldControlProgramsInternals::Failure(
			EVoxelLandM5ErrorCode::InvalidInput, TEXT("Source lease belongs to another budget."));
	}
	if (SourceLease->GetReservedBytes() < int64(sizeof(FVoxelLandWorldSourceSnapshot)))
	{
		return FVoxelLandWorldControlProgramsInternals::Failure(
			EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("Source lease cannot cover the fixed snapshot record."));
	}
	constexpr int64 ResultBytes = 4096;
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Lease;
	if (!Budget->TryReserveBytes(ResultBytes, Lease))
	{
		return FVoxelLandWorldControlProgramsInternals::Failure(
			EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("Control result admission failed."));
	}
	const FVoxelLandWorldCommonSnapshot& Common = Source->GetCommon();
	const FVoxelLandM4ControlSnapshot* Controls[] = {&Common.Continental, &Common.Mountain, &Common.Peaks};
	const TCHAR* Names[] = {TEXT("Continental"), TEXT("Mountain"), TEXT("Peaks")};
	TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Programs[3];
	const TArray<FVoxelLandWorldOperatorCapability> NoProviders;
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Controls); ++Index)
	{
		const FVoxelLandM4ControlSnapshot& Control = *Controls[Index];
		const FVoxelLandWorldCapabilityDiagnostic Capability = FVoxelLandWorldCapabilities::Validate(
			Control.Source, Source->GetWorldKind(), NoProviders, Budget, IsCancelled);
		if (Capability.Code != EVoxelLandWorldCapabilityError::None)
		{
			FString Detail = Capability.Message;
			if (!Capability.FunctionPath.IsEmpty())
			{
				Detail += FString::Printf(TEXT(" [function=%s]"), *Capability.FunctionPath);
			}
			if (Capability.NodeId.IsValid())
			{
				Detail += FString::Printf(TEXT(" [node=%s]"), *Capability.NodeId.ToString());
			}
			return FVoxelLandWorldControlProgramsInternals::Failure(
				FVoxelLandWorldControlProgramsInternals::CapabilityError(Capability.Code),
				FString::Printf(TEXT("%s control: %s"), Names[Index], *Detail));
		}
		FVoxelLandM5Diagnostic Result = VoxelLandM4OverrideValidation::ValidateMetered(Control, Budget, IsCancelled);
		if (Result.Code == EVoxelLandM5ErrorCode::None)
		{
			Result = VoxelLandM4CurveValidation::ValidateMetered(Control, Budget, IsCancelled);
		}
		if (Result.Code == EVoxelLandM5ErrorCode::None)
		{
			Result =
				FVoxelLandM5ProgramLoader::Load(Control.Source, Control.Payload, Budget, IsCancelled, Programs[Index]);
		}
		if (Result.Code != EVoxelLandM5ErrorCode::None)
		{
			Result.Message = FString::Printf(TEXT("%s control: %s"), Names[Index], *Result.Message);
			return Result;
		}
		if (!Programs[Index])
		{
			return FVoxelLandWorldControlProgramsInternals::Failure(
				EVoxelLandM5ErrorCode::Unproven, FString::Printf(TEXT("%s program is missing."), Names[Index]));
		}
	}
	if (IsCancelled())
	{
		return FVoxelLandWorldControlProgramsInternals::Failure(
			EVoxelLandM5ErrorCode::Cancelled, TEXT("Control preparation cancelled."));
	}
	TSharedPtr<const FVoxelLandWorldControlPrograms, ESPMode::ThreadSafe> Result =
		MakeShareable(new FVoxelLandWorldControlPrograms(
			Source, SourceLease, MoveTemp(Lease), MoveTemp(Programs[0]), MoveTemp(Programs[1]), MoveTemp(Programs[2])));
	OutPrograms = MoveTemp(Result);
	return {};
}
