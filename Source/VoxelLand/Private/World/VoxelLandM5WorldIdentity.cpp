#include "World/VoxelLandM5WorldIdentity.h"

#include "Data/Enums/VoxelLandM4DomainMode.h"
#include "Data/Structs/VoxelLandCanonicalWriter.h"
#include "Data/Structs/VoxelLandM4IdentityRecord.h"
#include "Data/Structs/VoxelLandM5IdentityRecord.h"
#include "Data/Structs/VoxelLandM5WorldSnapshot.h"
#include "Generation/VoxelLandM5Budget.h"
#include "Generation/VoxelLandM5BudgetLease.h"
#include "Generation/VoxelLandM5CheckedMath.h"
#include "Hash/xxhash.h"
#include "World/VoxelLandM5MeteredIdentity.h"
#include "World/VoxelLandM5MeteredM4Identity.h"
#include "World/VoxelLandM4WorldIdentity.h"
#include "World/VoxelLandM5CanonicalSettings.h"

class FVoxelLandM5WorldIdentityInternals final
{
public:
	static constexpr int64 M5IdentityCoordinateLimit = 1048576;
	static constexpr int32 M5IdentityMaxBytes = 8 * 1024 * 1024 + 4096;
	static int64 M5IdentityHash(const TArray<uint8>& Bytes);
	static bool M5ValidAxis(int64 Min, int64 Max);
	static bool M5ValidSettings(const FVoxelLandM5WorldSnapshot& S, FString& Error);
	static bool Serialize(const FVoxelLandM5WorldSnapshot& S, const FVoxelLandM4IdentityRecord& Base,
		FVoxelLandM5IdentityRecord& Out, FString& Error);
	static FVoxelLandM5Diagnostic MeteredError(EVoxelLandM5ErrorCode Code, const TCHAR* Message);
};

int64 FVoxelLandM5WorldIdentityInternals::M5IdentityHash(const TArray<uint8>& Bytes)
{
	const uint64 Hash = FXxHash64::HashBuffer(Bytes.GetData(), Bytes.Num()).Hash;
	int64 Bits = 0;
	FMemory::Memcpy(&Bits, &Hash, sizeof(Bits));
	return Bits;
}

bool FVoxelLandM5WorldIdentityInternals::M5ValidAxis(int64 Min, int64 Max)
{
	return Min >= -M5IdentityCoordinateLimit && Max <= M5IdentityCoordinateLimit + 1 && Min < Max;
}

bool FVoxelLandM5WorldIdentityInternals::M5ValidSettings(const FVoxelLandM5WorldSnapshot& S, FString& Error)
{
	const auto& C = S.Caves;
	const auto& M = S.Manifest;
	const auto& B = C.Bounds;
	const auto& P = C.Capsule;
	const auto& T = C.Topology;
	const auto& N = C.Pockets;
	const auto& L = C.Limits;
	if (S.FormatVersion != 1 || M.Descriptor != 1 || M.Seed != 1 || M.Topology != 1 || M.Geometry != 1
		|| M.RegionalOwnership != 1 || M.PocketNoise != 1 || M.SurfaceValidation != 1 || M.Composition != 1)
	{
		Error = TEXT("Unsupported M5 snapshot or algorithm version");
		return false;
	}
	if ((!C.bEnabled && !N.bEnabled) || !C.StageId.IsValid()
		|| (C.PlanningMode != EVoxelLandM5PlanningMode::Finite && C.PlanningMode != EVoxelLandM5PlanningMode::Regional)
		|| !M5ValidAxis(B.MinInclusive.X, B.MaxExclusive.X) || !M5ValidAxis(B.MinInclusive.Y, B.MaxExclusive.Y)
		|| !M5ValidAxis(B.MinInclusive.Z, B.MaxExclusive.Z) || C.RegionSizeCm < 1
		|| C.RegionSizeCm > 2 * M5IdentityCoordinateLimit || C.RegionOriginXCm < -M5IdentityCoordinateLimit
		|| C.RegionOriginXCm > M5IdentityCoordinateLimit || C.RegionOriginYCm < -M5IdentityCoordinateLimit
		|| C.RegionOriginYCm > M5IdentityCoordinateLimit || C.RoofThicknessCm < 0
		|| C.RoofThicknessCm > M5IdentityCoordinateLimit || P.RadiusCm < 1 || P.RadiusCm > M5IdentityCoordinateLimit
		|| P.StandingHeightCm < int64(P.RadiusCm) * 2 || P.StandingHeightCm > 2 * M5IdentityCoordinateLimit
		|| P.MaxSlopeDegrees < 0 || P.MaxSlopeDegrees > 90 || P.MaxStepCm < 0 || P.MaxStepCm > M5IdentityCoordinateLimit
		|| P.ClearanceMarginCm < 0 || P.ClearanceMarginCm > M5IdentityCoordinateLimit || T.MinChambers < 1
		|| T.MaxChambers < T.MinChambers || T.MaxChambers > 256 || T.EntranceCount < 0 || T.EntranceCount > 256
		|| T.MinLoops < 0 || T.MaxLoops < T.MinLoops || T.MaxLoops > 32640 || T.MinTunnelRadiusCm < 1
		|| T.MaxTunnelRadiusCm < T.MinTunnelRadiusCm || T.MaxTunnelRadiusCm > M5IdentityCoordinateLimit
		|| T.MinChamberRadiusCm < T.MaxTunnelRadiusCm || T.MaxChamberRadiusCm < T.MinChamberRadiusCm
		|| T.MaxChamberRadiusCm > M5IdentityCoordinateLimit || N.LatticeSpacingCm < 1
		|| N.LatticeSpacingCm > M5IdentityCoordinateLimit || N.ThresholdQ16 < 0 || N.ThresholdQ16 > 65535
		|| N.AmplitudeCm < 0 || N.AmplitudeCm > M5IdentityCoordinateLimit || N.MinDepthCm < 0
		|| N.MaxDepthCm < N.MinDepthCm || N.MaxDepthCm > 2 * M5IdentityCoordinateLimit
		|| L.MaxCandidates < T.MaxChambers || L.MaxCandidates > 256 || L.MaxEdges < 1 || L.MaxEdges > 32640
		|| L.MaxSegments < 1 || L.MaxSegments > 8192 || L.MaxJunctions < 1 || L.MaxJunctions > 8192 || L.MaxRoutes < 1
		|| L.MaxRoutes > 16384 || L.MaxRouteLegs < 1 || L.MaxRouteLegs > 65536 || L.MaxRouteFeatureRefs < 1
		|| L.MaxRouteFeatureRefs > 131072 || L.MaxAttempts < 1 || L.MaxAttempts > 64 || L.MaxBytes < 1
		|| L.MaxBytes > 268435456 || L.MaxValidationWork < 1 || L.MaxValidationWork > 1000000000)
	{
		Error = TEXT("Invalid or out-of-range M5 identity inputs");
		return false;
	}
	// No supplied mask/provider representation or water classification execution is
	// retained yet. Never encode those as though their data were captured.
	if (C.Protection.bRequireMask || C.Water.Policy != EVoxelLandM5WaterPolicy::Ignore)
	{
		Error = TEXT("M5 identity does not yet support supplied protection masks or water policies");
		return false;
	}
	if (C.bEnabled
		&& ((C.PlanningMode == EVoxelLandM5PlanningMode::Finite && S.Base.Domain.Mode != EVoxelLandM4DomainMode::Finite)
			|| (C.PlanningMode == EVoxelLandM5PlanningMode::Regional
				&& S.Base.Domain.Mode != EVoxelLandM4DomainMode::Unlimited)))
	{
		Error = TEXT("M5 planning mode does not match the M4 domain mode");
		return false;
	}
	// Planner bounds are made of whole regional cells. Both operands have
	// already been range checked, so the differences are representable.
	if (C.bEnabled && C.PlanningMode == EVoxelLandM5PlanningMode::Regional
		&& (S.Base.VoxelSizeCm <= 0 || C.RegionSizeCm % int64(S.Base.VoxelSizeCm) != 0
			|| (B.MinInclusive.X - C.RegionOriginXCm) % C.RegionSizeCm != 0
			|| (B.MaxExclusive.X - C.RegionOriginXCm) % C.RegionSizeCm != 0
			|| (B.MinInclusive.Y - C.RegionOriginYCm) % C.RegionSizeCm != 0
			|| (B.MaxExclusive.Y - C.RegionOriginYCm) % C.RegionSizeCm != 0))
	{
		Error = TEXT("M5 region size must be a voxel multiple and bounds must align to the region grid");
		return false;
	}
	return true;
}

bool VoxelLandM5WorldIdentity::Capture(
	const FVoxelLandM5WorldSnapshot& S, FVoxelLandM5IdentityRecord& Out, FString& Error)
{
	Error.Reset();
	if (!FVoxelLandM5WorldIdentityInternals::M5ValidSettings(S, Error))
	{
		return false;
	}
	FVoxelLandM4IdentityRecord Base;
	if (!VoxelLandM4WorldIdentity::Capture(S.Base, Base, Error))
	{
		return false;
	}
	return FVoxelLandM5WorldIdentityInternals::Serialize(S, Base, Out, Error);
}

bool FVoxelLandM5WorldIdentityInternals::Serialize(const FVoxelLandM5WorldSnapshot& S,
	const FVoxelLandM4IdentityRecord& Base, FVoxelLandM5IdentityRecord& Out, FString& Error)
{
	const auto& C = S.Caves;
	const auto& M = S.Manifest;
	FCanonicalWriter W;
	W.U8('V');
	W.U8('L');
	W.U8('M');
	W.U8('5');
	W.U32(uint32(M.Descriptor));
	W.U32(uint32(M.Seed));
	W.U32(uint32(M.Topology));
	W.U32(uint32(M.Geometry));
	W.U32(uint32(M.RegionalOwnership));
	W.U32(uint32(M.PocketNoise));
	W.U32(uint32(M.SurfaceValidation));
	W.U32(uint32(M.Composition));
	W.U32(uint32(Base.FormatVersion));
	W.U32(uint32(Base.CanonicalBytes.Num()));
	W.Bytes.Append(Base.CanonicalBytes);
	VoxelLandM5CanonicalSettings::Write(W, C);
	if (W.Bytes.Num() > FVoxelLandM5WorldIdentityInternals::M5IdentityMaxBytes)
	{
		Error = TEXT("M5 identity exceeds its supported size");
		return false;
	}
	FVoxelLandM5IdentityRecord Result;
	Result.FormatVersion = 1;
	Result.CanonicalBytes = MoveTemp(W.Bytes);
	Result.FingerprintBits = FVoxelLandM5WorldIdentityInternals::M5IdentityHash(Result.CanonicalBytes);
	Out = MoveTemp(Result);
	return true;
}

FVoxelLandM5Diagnostic FVoxelLandM5WorldIdentityInternals::MeteredError(
	EVoxelLandM5ErrorCode Code, const TCHAR* Message)
{
	FVoxelLandM5Diagnostic Result;
	Result.Code = Code;
	Result.Stage = TEXT("M5.MeteredIdentity");
	Result.Message = Message;
	return Result;
}

FVoxelLandM5MeteredIdentity::FVoxelLandM5MeteredIdentity(
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe>&& InLease)
	: Lease(MoveTemp(InLease))
{
}

FVoxelLandM5Diagnostic FVoxelLandM5MeteredIdentity::Capture(const FVoxelLandM5WorldSnapshot& Snapshot,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled,
	TSharedPtr<const FVoxelLandM5MeteredIdentity, ESPMode::ThreadSafe>& OutIdentity)
{
	using E = EVoxelLandM5ErrorCode;
	using namespace VoxelLand::M5::CheckedMath;
	if (IsCancelled())
	{
		return FVoxelLandM5WorldIdentityInternals::MeteredError(E::Cancelled, TEXT("M5 identity cancelled"));
	}
	if (IsInGameThread())
	{
		return FVoxelLandM5WorldIdentityInternals::MeteredError(
			E::InvalidInput, TEXT("M5 identity requires a worker thread"));
	}
	int64 LimitBytes = 0;
	int64 LimitWork = 0;
	Budget->GetLimits(LimitBytes, LimitWork);
	if (LimitBytes > Snapshot.Caves.Limits.MaxBytes || LimitWork > Snapshot.Caves.Limits.MaxValidationWork)
	{
		return FVoxelLandM5WorldIdentityInternals::MeteredError(
			E::InvalidInput, TEXT("M5 budget exceeds settings limits"));
	}
	if (!Budget->TryChargeWork(1))
	{
		return FVoxelLandM5WorldIdentityInternals::MeteredError(
			E::BudgetExceeded, TEXT("M5 identity validation budget exhausted"));
	}
	FString Reason;
	if (!FVoxelLandM5WorldIdentityInternals::M5ValidSettings(Snapshot, Reason))
	{
		return FVoxelLandM5WorldIdentityInternals::MeteredError(E::InvalidInput, *Reason);
	}
	TSharedPtr<const FVoxelLandM5MeteredM4Identity, ESPMode::ThreadSafe> Base;
	FVoxelLandM5Diagnostic Diagnostic =
		FVoxelLandM5MeteredM4Identity::Capture(Snapshot.Base, Budget, IsCancelled, Base);
	if (Diagnostic.Code != E::None)
	{
		return Diagnostic;
	}
	if (!Base.IsValid())
	{
		return FVoxelLandM5WorldIdentityInternals::MeteredError(E::Unproven, TEXT("M4 identity missing"));
	}
	constexpr int64 EncodingAllowance = 4096;
	int64 Upper = 0;
	int64 Bytes = 0;
	int64 Work = 0;
	if (!TryAdd(Base->GetIdentity().CanonicalBytes.Num(), EncodingAllowance, Upper)
		|| Upper > FVoxelLandM5WorldIdentityInternals::M5IdentityMaxBytes || !TryMultiplyNonNegative(Upper, 4, Bytes)
		|| !TryMultiplyNonNegative(Upper, 8, Work))
	{
		return FVoxelLandM5WorldIdentityInternals::MeteredError(
			E::BudgetExceeded, TEXT("M5 identity exceeds admission limits"));
	}
	if (!Budget->TryChargeWork(Work))
	{
		return FVoxelLandM5WorldIdentityInternals::MeteredError(
			E::BudgetExceeded, TEXT("M5 identity work budget exhausted"));
	}
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Lease;
	if (!Budget->TryReserveBytes(Bytes, Lease))
	{
		return FVoxelLandM5WorldIdentityInternals::MeteredError(
			E::BudgetExceeded, TEXT("M5 identity memory budget exhausted"));
	}
	if (IsCancelled())
	{
		return FVoxelLandM5WorldIdentityInternals::MeteredError(E::Cancelled, TEXT("M5 identity cancelled"));
	}
	FVoxelLandM5IdentityRecord Identity;
	if (!FVoxelLandM5WorldIdentityInternals::Serialize(Snapshot, Base->GetIdentity(), Identity, Reason))
	{
		return FVoxelLandM5WorldIdentityInternals::MeteredError(E::InvalidInput, *Reason);
	}
	if (IsCancelled())
	{
		return FVoxelLandM5WorldIdentityInternals::MeteredError(E::Cancelled, TEXT("M5 identity cancelled"));
	}
	if (Identity.CanonicalBytes.Num() > Upper)
	{
		return FVoxelLandM5WorldIdentityInternals::MeteredError(
			E::Unproven, TEXT("M5 identity exceeded admitted size"));
	}
	TSharedPtr<FVoxelLandM5MeteredIdentity, ESPMode::ThreadSafe> Output =
		MakeShareable(new FVoxelLandM5MeteredIdentity(MoveTemp(Lease)));
	Output->Identity = MoveTemp(Identity);
	OutIdentity = MoveTemp(Output);
	return {};
}

bool VoxelLandM5WorldIdentity::Matches(
	const FVoxelLandM5IdentityRecord& Record, const FVoxelLandM5WorldSnapshot& S, FString& Error)
{
	Error.Reset();
	if (Record.FormatVersion != 1 || Record.CanonicalBytes.Num() < 8
		|| Record.CanonicalBytes.Num() > FVoxelLandM5WorldIdentityInternals::M5IdentityMaxBytes
		|| Record.FingerprintBits != FVoxelLandM5WorldIdentityInternals::M5IdentityHash(Record.CanonicalBytes))
	{
		Error = TEXT("Unsupported or corrupt M5 identity record");
		return false;
	}
	FVoxelLandM5IdentityRecord Current;
	if (!Capture(S, Current, Error))
	{
		return false;
	}
	if (Record.CanonicalBytes != Current.CanonicalBytes)
	{
		Error = TEXT("M5 world identity does not match the current inputs");
		return false;
	}
	return true;
}
