#include "Generation/VoxelLandM5PlanPreparation.h"

#include "Generation/VoxelLandM5Budget.h"
#include "Generation/VoxelLandM5BudgetLease.h"
#include "Generation/VoxelLandM5CandidatePlanner.h"
#include "Generation/VoxelLandM5CandidateResult.h"
#include "Generation/VoxelLandM5CheckedMath.h"
#include "Generation/VoxelLandM5EntrancePlanner.h"
#include "Generation/VoxelLandM5SpatialIndex.h"
#include "Generation/VoxelLandM5Topology.h"

class FVoxelLandM5PlanPreparationInternals final
{
public:
	static constexpr int64 M5PreparationCoordinateLimit = 1048576;
	static constexpr int32 M5PreparationIdentityLimit = 4 * 1024 * 1024;

	static FVoxelLandM5Diagnostic M5PreparationFailure(EVoxelLandM5ErrorCode Code, const TCHAR* Message)
	{
		FVoxelLandM5Diagnostic Error;
		Error.Code = Code;
		Error.Stage = TEXT("M5.PlanPreparation");
		Error.Message = Message;
		return Error;
	}
};

FVoxelLandM5Diagnostic VoxelLand::M5::PlanPreparation::Build(const FVoxelLandM5CaveSettings& Settings, int64 WorldSeed,
	const FVoxelLandM5RegionCoord& Region, TConstArrayView<uint8> ExpectedIdentity,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget,
	TFunctionRef<FVoxelLandM5Diagnostic(TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe>,
		const FVoxelLandM5CaveSettings&, int64, const FVoxelLandM5RegionCoord&, TConstArrayView<uint8>,
		TFunctionRef<bool()>)>
		VerifyDescriptor,
	TFunctionRef<FVoxelLandM5Diagnostic(TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe>, int64, int64, int32,
		int64&, int64&, int64&, int32&, bool&, bool&)>
		QuerySurface,
	TFunctionRef<FVoxelLandM5Diagnostic(TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe>,
		const FVoxelLandM5CaveSettings&, const FVoxelLandM5CavePlan&, TConstArrayView<uint8>, TFunctionRef<bool()>)>
		CertifyFinalField,
	TFunctionRef<bool()> IsCancelled, TSharedPtr<const FVoxelLandM5CavePlan, ESPMode::ThreadSafe>& OutPlan)
{
	if (IsCancelled())
	{
		return FVoxelLandM5PlanPreparationInternals::M5PreparationFailure(
			EVoxelLandM5ErrorCode::Cancelled, TEXT("Plan preparation cancelled"));
	}
	int64 ByteLimit = 0, WorkLimit = 0;
	Budget->GetLimits(ByteLimit, WorkLimit);
	if (!Settings.bEnabled || ExpectedIdentity.IsEmpty()
		|| ExpectedIdentity.Num() > FVoxelLandM5PlanPreparationInternals::M5PreparationIdentityLimit
		|| Settings.Limits.MaxBytes <= 0 || Settings.Limits.MaxValidationWork <= 0
		|| ByteLimit > Settings.Limits.MaxBytes || WorkLimit > Settings.Limits.MaxValidationWork)
	{
		return FVoxelLandM5PlanPreparationInternals::M5PreparationFailure(
			EVoxelLandM5ErrorCode::InvalidInput, TEXT("Invalid descriptor or shared budget"));
	}
	// Identity bytes are copied only after admission; the reservation outlives
	// the copy and all consumers. The verifier owns its own metered scratch.
	using namespace VoxelLand::M5::CheckedMath;
	int64 IdentityBytes = 0;
	if (!TryMultiplyNonNegative(ExpectedIdentity.Num(), 2, IdentityBytes) || !TryAdd(IdentityBytes, 64, IdentityBytes))
	{
		return FVoxelLandM5PlanPreparationInternals::M5PreparationFailure(
			EVoxelLandM5ErrorCode::ArithmeticOverflow, TEXT("Descriptor allocation overflow"));
	}
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> IdentityLease;
	if (!Budget->TryReserveBytes(IdentityBytes, IdentityLease) || !Budget->TryChargeWork(ExpectedIdentity.Num()))
	{
		return FVoxelLandM5PlanPreparationInternals::M5PreparationFailure(
			EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("Descriptor admission failed"));
	}
	TArray<uint8> Identity;
	Identity.Append(ExpectedIdentity.GetData(), ExpectedIdentity.Num());
	FVoxelLandM5Diagnostic Error = VerifyDescriptor(Budget, Settings, WorldSeed, Region, Identity, IsCancelled);
	if (Error.Code != EVoxelLandM5ErrorCode::None)
	{
		return Error;
	}
	if (IsCancelled())
	{
		return FVoxelLandM5PlanPreparationInternals::M5PreparationFailure(
			EVoxelLandM5ErrorCode::Cancelled, TEXT("Plan preparation cancelled"));
	}
	TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> Candidate;
	Error = CandidatePlanner::Build(Settings, WorldSeed, Region, Budget, IsCancelled, Candidate);
	if (Error.Code != EVoxelLandM5ErrorCode::None)
	{
		return Error;
	}
	if (!Candidate.IsValid())
	{
		return FVoxelLandM5PlanPreparationInternals::M5PreparationFailure(
			EVoxelLandM5ErrorCode::Unproven, TEXT("Candidate result is unavailable"));
	}
	TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> Complete;
	Error = EntrancePlanner::Build(
		Settings, WorldSeed, Candidate.ToSharedRef(), Budget, QuerySurface, IsCancelled, Complete);
	if (Error.Code != EVoxelLandM5ErrorCode::None)
	{
		return Error;
	}
	Candidate.Reset();
	if (!Complete.IsValid())
	{
		return FVoxelLandM5PlanPreparationInternals::M5PreparationFailure(
			EVoxelLandM5ErrorCode::Unproven, TEXT("Entrance result is unavailable"));
	}
	Error = Topology::Validate(Settings, WorldSeed, Region, Complete.ToSharedRef(), Budget, IsCancelled);
	if (Error.Code != EVoxelLandM5ErrorCode::None)
	{
		return Error;
	}
	const FVoxelLandM5CavePlan& Plan = Complete->GetPlan();
	const int64 EntryCount = int64(Plan.Chambers.Num()) + Plan.Segments.Num();
	if (EntryCount < 1 || EntryCount > FVoxelLandM5SpatialIndex::MaximumEntries)
	{
		return FVoxelLandM5PlanPreparationInternals::M5PreparationFailure(
			EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("Influence index entry limit exceeded"));
	}
	int64 EntryBytes = 0;
	if (!TryMultiplyNonNegative(EntryCount, int64(sizeof(FVoxelLandM5InfluenceBounds)), EntryBytes)
		|| !TryMultiplyNonNegative(EntryBytes, 2, EntryBytes))
	{
		return FVoxelLandM5PlanPreparationInternals::M5PreparationFailure(
			EVoxelLandM5ErrorCode::ArithmeticOverflow, TEXT("Influence entry allocation overflow"));
	}
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> EntryLease;
	if (!Budget->TryReserveBytes(EntryBytes, EntryLease) || !Budget->TryChargeWork(EntryCount))
	{
		return FVoxelLandM5PlanPreparationInternals::M5PreparationFailure(
			EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("Influence entry admission failed"));
	}
	// Hard subtraction may affect samples beyond the physical primitive. Until
	// a smaller bound is proven, each entry covers the complete numeric domain.
	// Ordinal handles [0,chambers) name chamber array indices; remaining handles
	// name segment array indices. Handles are plan-scoped, not primitive IDs.
	TArray<FVoxelLandM5InfluenceBounds> Entries;
	Entries.Reserve(int32(EntryCount));
	const FBox3d FullDomain(FVector3d(-FVoxelLandM5PlanPreparationInternals::M5PreparationCoordinateLimit),
		FVector3d(FVoxelLandM5PlanPreparationInternals::M5PreparationCoordinateLimit));
	for (int64 I = 0; I < EntryCount; ++I)
	{
		if (IsCancelled())
		{
			return FVoxelLandM5PlanPreparationInternals::M5PreparationFailure(
				EVoxelLandM5ErrorCode::Cancelled, TEXT("Influence index preparation cancelled"));
		}
		FVoxelLandM5InfluenceBounds Entry;
		Entry.PrimitiveId = uint64(I);
		Entry.Bounds = FullDomain;
		Entries.Add(Entry);
	}
	TSharedPtr<const FVoxelLandM5SpatialIndex, ESPMode::ThreadSafe> Index;
	const FVoxelLandError IndexError =
		FVoxelLandM5SpatialIndex::Build(Entries, int32(EntryCount), Budget, IsCancelled, Index);
	if (IndexError.Code != EVoxelLandErrorCode::None)
	{
		return FVoxelLandM5PlanPreparationInternals::M5PreparationFailure(
			IndexError.Code == EVoxelLandErrorCode::Cancelled ? EVoxelLandM5ErrorCode::Cancelled
															  : EVoxelLandM5ErrorCode::BudgetExceeded,
			TEXT("Influence index build failed"));
	}
	if (!Index.IsValid() || IsCancelled())
	{
		return FVoxelLandM5PlanPreparationInternals::M5PreparationFailure(
			IsCancelled() ? EVoxelLandM5ErrorCode::Cancelled : EVoxelLandM5ErrorCode::Unproven,
			TEXT("Influence index could not be finalized"));
	}
	// The real certifier, immutable plan+index publication object and their
	// retained leases are not yet integrated. No callback can certify by fiat.
	return FVoxelLandM5PlanPreparationInternals::M5PreparationFailure(EVoxelLandM5ErrorCode::Unproven,
		TEXT("Final field, complete capsule routes and regional agreement are not yet certified"));
}
