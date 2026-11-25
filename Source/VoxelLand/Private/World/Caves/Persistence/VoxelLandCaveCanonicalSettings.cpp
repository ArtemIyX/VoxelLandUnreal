#include "World/Caves/Persistence/VoxelLandCaveCanonicalSettings.h"

#include "Data/Program/Serialization/VoxelLandCanonicalWriter.h"
#include "Data/Caves/Planning/VoxelLandCaveSettings.h"

class FVoxelLandM5CanonicalSettingsInternals final
{
public:
	static void WriteU64(FCanonicalWriter& Writer, uint64 Value)
	{
		Writer.U32(uint32(Value));
		Writer.U32(uint32(Value >> 32));
	}

	static void WritePoint(FCanonicalWriter& Writer, const FVoxelLandM5PointCm& Point)
	{
		WriteU64(Writer, uint64(Point.X));
		WriteU64(Writer, uint64(Point.Y));
		WriteU64(Writer, uint64(Point.Z));
	}
};

void VoxelLandM5CanonicalSettings::Write(FCanonicalWriter& W, const FVoxelLandM5CaveSettings& C)
{
	W.U8(C.bEnabled ? 1 : 0);
	W.U32(C.StageId.A);
	W.U32(C.StageId.B);
	W.U32(C.StageId.C);
	W.U32(C.StageId.D);
	FVoxelLandM5CanonicalSettingsInternals::WriteU64(W, uint64(C.Salt));
	W.U8(uint8(C.PlanningMode));
	FVoxelLandM5CanonicalSettingsInternals::WritePoint(W, C.Bounds.MinInclusive);
	FVoxelLandM5CanonicalSettingsInternals::WritePoint(W, C.Bounds.MaxExclusive);
	FVoxelLandM5CanonicalSettingsInternals::WriteU64(W, uint64(C.RegionSizeCm));
	FVoxelLandM5CanonicalSettingsInternals::WriteU64(W, uint64(C.RegionOriginXCm));
	FVoxelLandM5CanonicalSettingsInternals::WriteU64(W, uint64(C.RegionOriginYCm));
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
	W.U32(uint32(N.AmplitudeCm));
	W.U32(uint32(N.MinDepthCm));
	W.U32(uint32(N.MaxDepthCm));
	W.U8(N.bAllowAttachment ? 1 : 0);
	W.U8(N.bAllowEnclosedVoids ? 1 : 0);
	FVoxelLandM5CanonicalSettingsInternals::WriteU64(W, uint64(N.Salt));
	W.U8(C.Protection.bProtectAll ? 1 : 0);
	W.U8(C.Protection.bRequireMask ? 1 : 0);
	W.U8(uint8(C.Water.Policy));
	FVoxelLandM5CanonicalSettingsInternals::WriteU64(W, uint64(C.Water.LevelCm));
	const auto& L = C.Limits;
	W.U32(uint32(L.MaxCandidates));
	W.U32(uint32(L.MaxEdges));
	W.U32(uint32(L.MaxSegments));
	W.U32(uint32(L.MaxJunctions));
	W.U32(uint32(L.MaxRoutes));
	W.U32(uint32(L.MaxRouteLegs));
	W.U32(uint32(L.MaxRouteFeatureRefs));
	W.U32(uint32(L.MaxAttempts));
	FVoxelLandM5CanonicalSettingsInternals::WriteU64(W, uint64(L.MaxBytes));
	FVoxelLandM5CanonicalSettingsInternals::WriteU64(W, uint64(L.MaxValidationWork));
}
