#include "Generation/VoxelLandM5PlanEncoding.h"

#include "Data/Structs/VoxelLandCanonicalWriter.h"
#include "Data/Structs/VoxelLandM5CavePlan.h"
#include "Generation/VoxelLandM5Budget.h"
#include "Generation/VoxelLandM5BudgetLease.h"
#include "Generation/VoxelLandM5CheckedMath.h"

namespace VoxelLand::M5::PlanEncoding
{
namespace
{
constexpr int64 MaxEncodedBytes = 64ll * 1024 * 1024;
constexpr int64 MaxRecords = 1000000;
FVoxelLandM5Diagnostic Error(EVoxelLandM5ErrorCode Code, const TCHAR* Message)
{
	FVoxelLandM5Diagnostic D;
	D.Code = Code;
	D.Stage = TEXT("M5.PlanEncoding");
	D.Message = Message;
	return D;
}
void U64(FCanonicalWriter& W, uint64 V) { W.U32(uint32(V)); W.U32(uint32(V >> 32)); }
void I64(FCanonicalWriter& W, int64 V) { U64(W, uint64(V)); }
void Point(FCanonicalWriter& W, const FVoxelLandM5PointCm& P) { I64(W, P.X); I64(W, P.Y); I64(W, P.Z); }
void Region(FCanonicalWriter& W, const FVoxelLandM5RegionCoord& R) { I64(W, R.X); I64(W, R.Y); }
void Bounds(FCanonicalWriter& W, const FVoxelLandM5Bounds& B) { Point(W, B.MinInclusive); Point(W, B.MaxExclusive); }
void Endpoint(FCanonicalWriter& W, const FVoxelLandM5Endpoint& E)
{
	W.U8(E.bValid ? 1 : 0); W.U8(uint8(E.Kind)); W.U8(uint8(E.AnchorRole)); Region(W, E.Region); U64(W, E.Id);
}
void Feature(FCanonicalWriter& W, const FVoxelLandM5RouteFeatureRef& F)
{
	W.U8(F.bValid ? 1 : 0); W.U8(uint8(F.Kind)); Region(W, F.Region); U64(W, F.Id);
}
void PortalKey(FCanonicalWriter& W, const FVoxelLandM5PortalKey& K)
{
	I64(W, K.WorldSeed); W.U32(K.StageId.A); W.U32(K.StageId.B); W.U32(K.StageId.C); W.U32(K.StageId.D);
	I64(W, K.Salt); Region(W, K.RegionA); Region(W, K.RegionB); W.U8(uint8(K.Axis));
}
}

FVoxelLandM5Diagnostic Encode(const FVoxelLandM5CavePlan& P,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled,
	TArray<uint8>& OutBytes, TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe>& OutLease)
{
	using namespace CheckedMath;
	using E = EVoxelLandM5ErrorCode;
	if (IsCancelled()) return Error(E::Cancelled, TEXT("Plan encoding cancelled"));
	const int64 Top = int64(P.Chambers.Num()) + P.Segments.Num() + P.Connections.Num() + P.Entrances.Num()
		+ P.Junctions.Num() + P.Portals.Num() + P.Routes.Num();
	if (Top > MaxRecords) return Error(E::BudgetExceeded, TEXT("Plan record count exceeds encoding limit"));
	if (!Budget->TryChargeWork(int64(P.Routes.Num()) + 1))
		return Error(E::BudgetExceeded, TEXT("Plan inventory budget exhausted"));
	int64 Legs = 0, Features = 0;
	for (const auto& Route : P.Routes)
	{
		if (IsCancelled()) return Error(E::Cancelled, TEXT("Plan encoding cancelled"));
		if (!TryAdd(Legs, Route.Legs.Num(), Legs) || !TryAdd(Features, Route.CoveredFeatures.Num(), Features)
			|| Legs > MaxRecords || Features > MaxRecords)
			return Error(E::BudgetExceeded, TEXT("Plan nested record count exceeds encoding limit"));
	}
	int64 Upper = 128, Part = 0;
	auto Add = [&Upper, &Part](int64 Count, int64 Width) {
		return TryMultiplyNonNegative(Count, Width, Part) && TryAdd(Upper, Part, Upper);
	};
	if (!Add(P.Chambers.Num(), 128) || !Add(P.Segments.Num(), 160) || !Add(P.Connections.Num(), 128)
		|| !Add(P.Entrances.Num(), 160) || !Add(P.Junctions.Num(), 96) || !Add(P.Portals.Num(), 192)
		|| !Add(P.Routes.Num(), 128) || !Add(Legs, 192) || !Add(Features, 64) || Upper > MaxEncodedBytes)
		return Error(E::BudgetExceeded, TEXT("Plan encoding exceeds size limit"));
	int64 Work = 0, Bytes = 0;
	if (!TryMultiplyNonNegative(Upper, 2, Work) || !TryAdd(Upper, 4096, Bytes))
		return Error(E::ArithmeticOverflow, TEXT("Plan encoding admission overflow"));
	if (!Budget->TryChargeWork(Work)) return Error(E::BudgetExceeded, TEXT("Plan encoding work exhausted"));
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Lease;
	if (!Budget->TryReserveBytes(Bytes, Lease)) return Error(E::BudgetExceeded, TEXT("Plan encoding memory exhausted"));
	FCanonicalWriter W;
	W.Bytes.Reserve(int32(Upper));
	W.U8('V'); W.U8('L'); W.U8('P'); W.U8('5'); W.U32(1); W.U32(uint32(P.GeometryVersion));
	Bounds(W, P.Bounds); Region(W, P.Region);
	auto Cancel = [&]() { return IsCancelled(); };
	W.U32(uint32(P.Chambers.Num()));
	for (const auto& C : P.Chambers)
	{
		if (Cancel()) return Error(E::Cancelled, TEXT("Plan encoding cancelled"));
		W.U8(C.bHasId); U64(W, C.Id); Point(W, C.CenterCm); W.U32(uint32(C.RadiusCm));
		W.U32(uint32(C.Profile.Version)); I64(W, C.Profile.FloorCm); I64(W, C.Profile.CeilingCm);
		W.U8(C.bHasFeetAnchor); Point(W, C.FeetAnchorCm);
	}
	W.U32(uint32(P.Connections.Num()));
	for (const auto& C : P.Connections)
	{
		if (Cancel()) return Error(E::Cancelled, TEXT("Plan encoding cancelled"));
		W.U8(C.bHasId); U64(W, C.Id); Endpoint(W, C.Start); Endpoint(W, C.End); W.U8(C.bLoop);
	}
	W.U32(uint32(P.Segments.Num()));
	for (const auto& S : P.Segments)
	{
		if (Cancel()) return Error(E::Cancelled, TEXT("Plan encoding cancelled"));
		W.U8(S.bHasId); U64(W, S.Id); W.U8(S.bHasConnectionId); U64(W, S.ConnectionId);
		Point(W, S.StartCm); Point(W, S.EndCm); W.U32(uint32(S.RadiusCm)); W.U32(uint32(S.Profile.Version));
		I64(W, S.Profile.StartFloorCm); I64(W, S.Profile.EndFloorCm);
		I64(W, S.Profile.StartCeilingCm); I64(W, S.Profile.EndCeilingCm);
	}
	W.U32(uint32(P.Entrances.Num()));
	for (const auto& E0 : P.Entrances)
	{
		if (Cancel()) return Error(E::Cancelled, TEXT("Plan encoding cancelled"));
		W.U8(E0.bHasId); U64(W, E0.Id); W.U8(E0.bHasConnectionId); U64(W, E0.ConnectionId);
		Bounds(W, E0.OpeningBoundsCm); W.U8(E0.bHasInsideFeetAnchor); Point(W, E0.InsideFeetAnchorCm);
		W.U8(E0.bHasOutsideFeetAnchor); Point(W, E0.OutsideFeetAnchorCm); W.U8(E0.bFlooded);
	}
	W.U32(uint32(P.Junctions.Num()));
	for (const auto& J : P.Junctions)
	{
		if (Cancel()) return Error(E::Cancelled, TEXT("Plan encoding cancelled"));
		W.U8(J.bHasId); U64(W, J.Id); W.U8(J.bHasCenter); Point(W, J.CenterCm);
		W.U8(J.bHasFeetAnchor); Point(W, J.FeetAnchorCm);
	}
	W.U32(uint32(P.Portals.Num()));
	for (const auto& P0 : P.Portals)
	{
		if (Cancel()) return Error(E::Cancelled, TEXT("Plan encoding cancelled"));
		W.U8(P0.bHasId); U64(W, P0.Id); PortalKey(W, P0.Key); Point(W, P0.CenterCm); W.U32(uint32(P0.RadiusCm));
		W.U8(P0.bHasRegionAFeetAnchor); Point(W, P0.RegionAFeetAnchorCm);
		W.U8(P0.bHasRegionBFeetAnchor); Point(W, P0.RegionBFeetAnchorCm);
	}
	W.U32(uint32(P.Routes.Num()));
	for (const auto& R : P.Routes)
	{
		if (Cancel()) return Error(E::Cancelled, TEXT("Plan encoding cancelled"));
		W.U8(R.bHasId); U64(W, R.Id); Endpoint(W, R.Start); Endpoint(W, R.End); W.U32(uint32(R.Legs.Num()));
		for (const auto& L : R.Legs)
		{
			if (Cancel()) return Error(E::Cancelled, TEXT("Plan encoding cancelled"));
			Point(W, L.StartFeetCm); Point(W, L.EndFeetCm); W.U8(uint8(L.Transition)); Feature(W, L.Feature);
			W.U8(L.bHasSupportPlane); Point(W, L.SupportPlane.A); Point(W, L.SupportPlane.B); Point(W, L.SupportPlane.C);
		}
		W.U32(uint32(R.CoveredFeatures.Num()));
		for (const auto& F : R.CoveredFeatures)
		{
			if (Cancel()) return Error(E::Cancelled, TEXT("Plan encoding cancelled"));
			Feature(W, F);
		}
	}
	if (W.Bytes.Num() > Upper) return Error(E::Unproven, TEXT("Plan encoding exceeded admitted size"));
	if (Cancel()) return Error(E::Cancelled, TEXT("Plan encoding cancelled"));
	OutBytes = MoveTemp(W.Bytes);
	OutLease = MoveTemp(Lease);
	return {};
}
}
