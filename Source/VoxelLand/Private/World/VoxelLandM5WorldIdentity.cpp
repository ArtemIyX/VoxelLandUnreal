#include "World/VoxelLandM5WorldIdentity.h"

#include "Data/Enums/VoxelLandM4DomainMode.h"
#include "Data/Structs/VoxelLandCanonicalWriter.h"
#include "Data/Structs/VoxelLandM4IdentityRecord.h"
#include "Data/Structs/VoxelLandM5IdentityRecord.h"
#include "Data/Structs/VoxelLandM5WorldSnapshot.h"
#include "Hash/xxhash.h"
#include "World/VoxelLandM4WorldIdentity.h"

namespace
{
constexpr int64 M5IdentityCoordinateLimit = 1048576;
constexpr int32 M5IdentityMaxBytes = 8 * 1024 * 1024 + 4096;

void M5WriteU64(FCanonicalWriter& W, uint64 Value)
{
	W.U32(uint32(Value));
	W.U32(uint32(Value >> 32));
}

void M5WritePoint(FCanonicalWriter& W, const FVoxelLandM5PointCm& P)
{
	M5WriteU64(W, uint64(P.X));
	M5WriteU64(W, uint64(P.Y));
	M5WriteU64(W, uint64(P.Z));
}

int64 M5IdentityHash(const TArray<uint8>& Bytes)
{
	const uint64 Hash = FXxHash64::HashBuffer(Bytes.GetData(), Bytes.Num()).Hash;
	int64 Bits = 0;
	FMemory::Memcpy(&Bits, &Hash, sizeof(Bits));
	return Bits;
}

bool M5ValidAxis(int64 Min, int64 Max)
{
	return Min >= -M5IdentityCoordinateLimit && Max <= M5IdentityCoordinateLimit + 1 && Min < Max;
}

bool M5ValidSettings(const FVoxelLandM5WorldSnapshot& S, FString& Error)
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
		|| N.MinDepthCm < 0 || N.MaxDepthCm < N.MinDepthCm || N.MaxDepthCm > 2 * M5IdentityCoordinateLimit
		|| L.MaxCandidates < T.MaxChambers || L.MaxCandidates > 256 || L.MaxEdges < 1 || L.MaxEdges > 32640
		|| L.MaxSegments < 1 || L.MaxSegments > 8192 || L.MaxAttempts < 1 || L.MaxAttempts > 64 || L.MaxBytes < 1
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
}

bool VoxelLandM5WorldIdentity::Capture(
	const FVoxelLandM5WorldSnapshot& S, FVoxelLandM5IdentityRecord& Out, FString& Error)
{
	Error.Reset();
	if (!M5ValidSettings(S, Error))
	{
		return false;
	}
	FVoxelLandM4IdentityRecord Base;
	if (!VoxelLandM4WorldIdentity::Capture(S.Base, Base, Error))
	{
		return false;
	}
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
	W.U8(C.bEnabled ? 1 : 0);
	W.U32(C.StageId.A);
	W.U32(C.StageId.B);
	W.U32(C.StageId.C);
	W.U32(C.StageId.D);
	M5WriteU64(W, uint64(C.Salt));
	W.U8(uint8(C.PlanningMode));
	M5WritePoint(W, C.Bounds.MinInclusive);
	M5WritePoint(W, C.Bounds.MaxExclusive);
	M5WriteU64(W, uint64(C.RegionSizeCm));
	M5WriteU64(W, uint64(C.RegionOriginXCm));
	M5WriteU64(W, uint64(C.RegionOriginYCm));
	W.U32(uint32(C.RoofThicknessCm));
	const auto& P = C.Capsule;
	W.U32(uint32(P.StandingHeightCm));
	W.U32(uint32(P.RadiusCm));
	W.U32(uint32(P.MaxSlopeDegrees));
	W.U32(uint32(P.MaxStepCm));
	W.U32(uint32(P.ClearanceMarginCm));
	const auto& T = C.Topology;
	W.U32(uint32(T.MinChambers));
	W.U32(uint32(T.MaxChambers));
	W.U32(uint32(T.EntranceCount));
	W.U32(uint32(T.MinLoops));
	W.U32(uint32(T.MaxLoops));
	W.U32(uint32(T.MinTunnelRadiusCm));
	W.U32(uint32(T.MaxTunnelRadiusCm));
	W.U32(uint32(T.MinChamberRadiusCm));
	W.U32(uint32(T.MaxChamberRadiusCm));
	const auto& N = C.Pockets;
	W.U8(N.bEnabled ? 1 : 0);
	W.U32(uint32(N.LatticeSpacingCm));
	W.U32(uint32(N.ThresholdQ16));
	W.U32(uint32(N.MinDepthCm));
	W.U32(uint32(N.MaxDepthCm));
	W.U8(N.bAllowAttachment ? 1 : 0);
	W.U8(N.bAllowEnclosedVoids ? 1 : 0);
	M5WriteU64(W, uint64(N.Salt));
	W.U8(C.Protection.bProtectAll ? 1 : 0);
	W.U8(C.Protection.bRequireMask ? 1 : 0);
	W.U8(uint8(C.Water.Policy));
	M5WriteU64(W, uint64(C.Water.LevelCm));
	const auto& L = C.Limits;
	W.U32(uint32(L.MaxCandidates));
	W.U32(uint32(L.MaxEdges));
	W.U32(uint32(L.MaxSegments));
	W.U32(uint32(L.MaxAttempts));
	M5WriteU64(W, uint64(L.MaxBytes));
	M5WriteU64(W, uint64(L.MaxValidationWork));
	if (W.Bytes.Num() > M5IdentityMaxBytes)
	{
		Error = TEXT("M5 identity exceeds its supported size");
		return false;
	}
	FVoxelLandM5IdentityRecord Result;
	Result.FormatVersion = 1;
	Result.CanonicalBytes = MoveTemp(W.Bytes);
	Result.FingerprintBits = M5IdentityHash(Result.CanonicalBytes);
	Out = MoveTemp(Result);
	return true;
}

bool VoxelLandM5WorldIdentity::Matches(
	const FVoxelLandM5IdentityRecord& Record, const FVoxelLandM5WorldSnapshot& S, FString& Error)
{
	Error.Reset();
	if (Record.FormatVersion != 1 || Record.CanonicalBytes.Num() < 8 || Record.CanonicalBytes.Num() > M5IdentityMaxBytes
		|| Record.FingerprintBits != M5IdentityHash(Record.CanonicalBytes))
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
