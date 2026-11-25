#include "World/Caves/Persistence/VoxelLandCaveMeteredTerrainIdentity.h"

#include "Data/Terrain/Domains/VoxelLandTerrainDomainMode.h"
#include "Data/Terrain/Worlds/VoxelLandTerrainWorldSnapshot.h"
#include "Generation/Field/VoxelLandGenerator.h"
#include "Generation/Terrain/Domains/VoxelLandTerrainDomain.h"
#include "Generation/Caves/Infrastructure/VoxelLandCaveBudget.h"
#include "Generation/Caves/Infrastructure/VoxelLandCaveBudgetLease.h"
#include "Generation/Caves/Infrastructure/VoxelLandCaveCheckedMath.h"
#include "Graph/Serialization/VoxelLandCaveProgramLoader.h"
#include "../../Terrain/Persistence/VoxelLandIdentitySerialization.h"
#include "World/Caves/Planning/VoxelLandCaveMeteredPrepass.h"
#include "World/Caves/Infrastructure/VoxelLandCavePrepassBytes.h"

class FVoxelLandM5MeteredM4IdentityInternals final
{
public:
	static constexpr int32 IdentityVersion = 1;
	static constexpr int64 MaxIdentityBytes = 8ll * 1024 * 1024;
	static constexpr int64 FixedEncodingAllowance = 16384;
	static constexpr int64 OverrideBytes = 21;
	static constexpr int64 MaxOverrides = 128;

	static FVoxelLandM5Diagnostic Error(EVoxelLandM5ErrorCode Code, const TCHAR* Message)
	{
		FVoxelLandM5Diagnostic Result;
		Result.Code = Code;
		Result.Stage = TEXT("M5.MeteredM4Identity");
		Result.Message = Message;
		return Result;
	}

	static bool ValidScalars(const FVoxelLandM4WorldSnapshot& S)
	{
		return S.Domain.Mode == EVoxelLandM4DomainMode::Finite && S.FormatVersion == 1 && S.VoxelSizeCm >= 1
			&& S.VoxelSizeCm <= 2000000 && !VoxelLandM4Domain::Validate(S.Domain) && S.Smooth.Version == 1
			&& FMath::IsFinite(S.Smooth.Strength) && S.Smooth.Strength >= 0.f && S.Smooth.Strength <= 1.f
			&& S.OverhangAmplitudeQ8 >= 0 && S.OverhangAmplitudeQ8 <= 128000 && S.OverhangBandQ8 >= 1
			&& S.OverhangBandQ8 <= 1280000 && S.OverhangAmplitudeQ8 <= S.OverhangBandQ8
			&& FMath::IsFinite(S.OverhangFrequencyPerCm) && S.OverhangFrequencyPerCm > 0.f
			&& S.OverhangFrequencyPerCm <= 1.f && S.OverhangSalt >= 0 && S.OverhangNodeId.IsValid()
			&& S.HeightDetail.AlgorithmVersion == 1 && S.HeightDetail.AmplitudeQ8 >= 0
			&& S.HeightDetail.AmplitudeQ8 <= 1280000 && FMath::IsFinite(S.HeightDetail.FrequencyPerCm)
			&& S.HeightDetail.FrequencyPerCm > 0.f && S.HeightDetail.FrequencyPerCm <= 1.f
			&& S.HeightDetail.NodeId.IsValid();
	}
};

FVoxelLandM5MeteredM4Identity::FVoxelLandM5MeteredM4Identity(
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe>&& InLease)
	: Lease(MoveTemp(InLease))
{
}

FVoxelLandM5Diagnostic FVoxelLandM5MeteredM4Identity::Capture(const FVoxelLandM4WorldSnapshot& Snapshot,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled,
	TSharedPtr<const FVoxelLandM5MeteredM4Identity, ESPMode::ThreadSafe>& OutIdentity)
{
	using E = EVoxelLandM5ErrorCode;
	using namespace VoxelLand::M5::CheckedMath;
	if (IsCancelled())
	{
		return FVoxelLandM5MeteredM4IdentityInternals::Error(E::Cancelled, TEXT("M4 identity cancelled"));
	}
	if (IsInGameThread() || !FVoxelLandM5MeteredM4IdentityInternals::ValidScalars(Snapshot))
	{
		return FVoxelLandM5MeteredM4IdentityInternals::Error(
			E::InvalidInput, TEXT("Invalid finite M4 identity inputs"));
	}
	TSharedPtr<const FVoxelLandM5PrepassBytes, ESPMode::ThreadSafe> Prepass;
	FVoxelLandM5Diagnostic Result = FVoxelLandM5MeteredPrepass::Build(Snapshot, 1, Budget, IsCancelled, Prepass);
	if (Result.Code != E::None)
	{
		return Result;
	}
	if (!Prepass.IsValid())
	{
		return FVoxelLandM5MeteredM4IdentityInternals::Error(E::Unproven, TEXT("M4 prepass bytes missing"));
	}
	const FVoxelLandM4ControlSnapshot* Channels[] = {&Snapshot.Continental, &Snapshot.Mountain, &Snapshot.Peaks};
	TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Programs[3];
	int64 OverrideCount = 0;
	for (int32 Index = 0; Index < 3; ++Index)
	{
		if (IsCancelled())
		{
			return FVoxelLandM5MeteredM4IdentityInternals::Error(E::Cancelled, TEXT("M4 identity cancelled"));
		}
		if (Channels[Index]->Overrides.Num() > FVoxelLandM5MeteredM4IdentityInternals::MaxOverrides
			|| !TryAdd(OverrideCount, Channels[Index]->Overrides.Num(), OverrideCount))
		{
			return FVoxelLandM5MeteredM4IdentityInternals::Error(
				E::BudgetExceeded, TEXT("M4 override count exceeds limits"));
		}
		Result = FVoxelLandM5ProgramLoader::Load(
			Channels[Index]->Source, Channels[Index]->Payload, Budget, IsCancelled, Programs[Index]);
		if (Result.Code != E::None)
		{
			return Result;
		}
		if (!Programs[Index].IsValid())
		{
			return FVoxelLandM5MeteredM4IdentityInternals::Error(E::Unproven, TEXT("M4 program missing"));
		}
	}
	int64 OverrideOutput = 0;
	int64 Upper = 0;
	int64 Work = 0;
	int64 Bytes = 0;
	if (!TryMultiplyNonNegative(OverrideCount, FVoxelLandM5MeteredM4IdentityInternals::OverrideBytes, OverrideOutput)
		|| !TryAdd(Prepass->GetBytes().Num(), OverrideOutput, Upper)
		|| !TryAdd(Upper, FVoxelLandM5MeteredM4IdentityInternals::FixedEncodingAllowance, Upper)
		|| Upper > FVoxelLandM5MeteredM4IdentityInternals::MaxIdentityBytes || !TryMultiplyNonNegative(Upper, 8, Work)
		|| !TryAdd(Work, OverrideCount * OverrideCount * 8, Work) || !TryMultiplyNonNegative(Upper, 4, Bytes))
	{
		return FVoxelLandM5MeteredM4IdentityInternals::Error(
			E::BudgetExceeded, TEXT("M4 identity admission exceeds limits"));
	}
	if (!Budget->TryChargeWork(Work))
	{
		return FVoxelLandM5MeteredM4IdentityInternals::Error(
			E::BudgetExceeded, TEXT("M4 identity work budget exhausted"));
	}
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Lease;
	if (!Budget->TryReserveBytes(Bytes, Lease))
	{
		return FVoxelLandM5MeteredM4IdentityInternals::Error(
			E::BudgetExceeded, TEXT("M4 identity memory budget exhausted"));
	}
	if (IsCancelled())
	{
		return FVoxelLandM5MeteredM4IdentityInternals::Error(E::Cancelled, TEXT("M4 identity cancelled"));
	}
	FVoxelLandM4IdentityRecord Identity;
	FString Reason;
	if (!VoxelLandM4WorldIdentityInternal::SerializeFromPrepass(Snapshot,
			FVoxelLandM5MeteredM4IdentityInternals::IdentityVersion, *Programs[0], *Programs[1], *Programs[2],
			Prepass->GetBytes(), Identity, Reason))
	{
		return FVoxelLandM5MeteredM4IdentityInternals::Error(E::InvalidInput, *Reason);
	}
	if (IsCancelled())
	{
		return FVoxelLandM5MeteredM4IdentityInternals::Error(E::Cancelled, TEXT("M4 identity cancelled"));
	}
	if (Identity.CanonicalBytes.Num() > Upper)
	{
		return FVoxelLandM5MeteredM4IdentityInternals::Error(E::Unproven, TEXT("M4 identity exceeded admitted size"));
	}
	TSharedPtr<FVoxelLandM5MeteredM4Identity, ESPMode::ThreadSafe> Output =
		MakeShareable(new FVoxelLandM5MeteredM4Identity(MoveTemp(Lease)));
	Output->Identity = MoveTemp(Identity);
	OutIdentity = MoveTemp(Output);
	return {};
}
