#include "Generation/VoxelLandM5SupportSelector.h"

#include "Data/Structs/VoxelLandM5CaveSettings.h"
#include "Generation/VoxelLandM5Budget.h"
#include "Generation/VoxelLandM5BudgetLease.h"
#include "Generation/VoxelLandM5CandidateResult.h"
#include "Generation/VoxelLandM5CheckedMath.h"
#include "Generation/VoxelLandM5ScratchChunks.h"
#include "Generation/VoxelLandM5ScratchMesh.h"

class FVoxelLandM5SupportSelectorInternals final
{
public:
	static FVoxelLandM5Diagnostic Error(EVoxelLandM5ErrorCode Code, const TCHAR* Message)
	{
		FVoxelLandM5Diagnostic Result;
		Result.Code = Code;
		Result.Stage = TEXT("M5.SupportSelector");
		Result.Message = Message;
		return Result;
	}
	// Exact XY winding after lossless common power-of-two reduction. Failure
	// means the determinant cannot be represented within the bounded domain.
	static bool Winding(const FVoxelLandM5FixedTriangleQ18& T, int64& Out)
	{
		using namespace VoxelLand::M5::CheckedMath;
		const int64 Values[] = {T.B.X - T.A.X, T.B.Y - T.A.Y, T.C.X - T.A.X, T.C.Y - T.A.Y};
		for (int32 Shift = 38; Shift >= 0; --Shift)
		{
			const int64 Scale = int64(1) << Shift;
			bool bFits = true;
			for (int64 Value : Values)
			{
				bFits &= Value % Scale == 0 && Value / Scale >= -1048576 && Value / Scale <= 1048576;
			}
			if (!bFits)
			{
				continue;
			}
			int64 Left = 0, Right = 0;
			return TryMultiply(Values[0] / Scale, Values[3] / Scale, Left)
				&& TryMultiply(Values[1] / Scale, Values[2] / Scale, Right) && TrySubtract(Left, Right, Out);
		}
		return false;
	}
};

FVoxelLandM5Diagnostic FVoxelLandM5SupportSelector::Build(const FVoxelLandM5CaveSettings& Settings, int64 WorldSeed,
	TSharedRef<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> CompositionSource,
	TSharedRef<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> Routed,
	TSharedRef<const FVoxelLandM5ScratchMesh, ESPMode::ThreadSafe> Mesh, int64 ContactSkinCm,
	int32 MaxTotalFaces, TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget,
	TFunctionRef<bool()> IsCancelled, TSharedPtr<const FVoxelLandM5SupportSelector, ESPMode::ThreadSafe>& Out)
{
	using I = FVoxelLandM5SupportSelectorInternals;
	using E = EVoxelLandM5ErrorCode;
	using namespace VoxelLand::M5::CheckedMath;
	if (IsCancelled())
	{
		return I::Error(E::Cancelled, TEXT("Support selection cancelled"));
	}
	const FVoxelLandM5RegionCoord& Region = CompositionSource->GetPlan().Region;
	const TConstArrayView<FVoxelLandM5FixedTriangleQ18> Triangles = Mesh->GetTriangles();
	const TConstArrayView<FVoxelLandM5ChunkFaceSelection> Faces = Mesh->GetFaces();
	const TArray<FVoxelLandM5Route>& Routes = Routed->GetPlan().Routes;
	if (!Settings.bEnabled || Settings.PlanningMode != EVoxelLandM5PlanningMode::Finite
		|| Settings.Capsule.RadiusCm < 1 || Settings.Capsule.RadiusCm > 1048576
		|| ContactSkinCm < 0 || ContactSkinCm > 1048576 || MaxTotalFaces < 1 || MaxTotalFaces > 131072
		|| Routes.IsEmpty() || Routes.Num() > Settings.Limits.MaxRoutes || Routes.Num() > 16384
		|| Triangles.Num() != Faces.Num() || Triangles.Num() > 196608
		|| !CompositionSource->UsesBudget(*Budget) || !Routed->UsesBudget(*Budget) || !Mesh->IsOwnedBy(*Budget)
		|| !Mesh->GetSource().IsComposedFrom(CompositionSource.Get())
		|| !Routed->IsDirectlyDerivedFrom(CompositionSource.Get())
		|| !CompositionSource->MatchesSource(Settings, WorldSeed, Region) || !Routed->MatchesSource(Settings, WorldSeed, Region))
	{
		return I::Error(E::InvalidInput, TEXT("Invalid support selector sources or limits"));
	}
	if (!Budget->TryChargeWork(int64(Routes.Num()) + 1))
	{
		return I::Error(E::BudgetExceeded, TEXT("Support selector inventory exhausted"));
	}
	int64 LegCount = 0;
	for (int32 Index = 0; Index < Routes.Num(); ++Index)
	{
		if (IsCancelled())
		{
			return I::Error(E::Cancelled, TEXT("Support selection cancelled"));
		}
		if (!Routes[Index].bHasId || Routes[Index].Legs.IsEmpty()
			|| (Index > 0 && Routes[Index - 1].Id >= Routes[Index].Id)
			|| !TryAdd(LegCount, Routes[Index].Legs.Num(), LegCount)
			|| LegCount > Settings.Limits.MaxRouteLegs || LegCount > 65536)
		{
			return I::Error(E::InvalidInput, TEXT("Invalid ordered route inventory"));
		}
	}
	int64 Work = 0, Bytes = 0, ArrayBytes = 0;
	if (!TryMultiplyNonNegative(LegCount, Triangles.Num(), Work) || !TryMultiplyNonNegative(Work, 128, Work)
		|| !TryAdd(Work, LegCount + 64, Work)
		|| !TryMultiplyNonNegative(LegCount, 4 * int64(sizeof(FVoxelLandM5MultiRouteSupportSelection)), Bytes)
		|| !TryMultiplyNonNegative(MaxTotalFaces, 4 * int64(sizeof(FVoxelLandM5ChunkFaceSelection)), ArrayBytes)
		|| !TryAdd(Bytes, ArrayBytes, Bytes) || !TryAdd(Bytes, 4096, Bytes))
	{
		return I::Error(E::ArithmeticOverflow, TEXT("Support selector admission overflow"));
	}
	if (!Budget->TryChargeWork(Work))
	{
		return I::Error(E::BudgetExceeded, TEXT("Support selector work exhausted"));
	}
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Lease;
	if (!Budget->TryReserveBytes(Bytes, Lease))
	{
		return I::Error(E::BudgetExceeded, TEXT("Support selector memory exhausted"));
	}
	if (IsCancelled())
	{
		return I::Error(E::Cancelled, TEXT("Support selection cancelled"));
	}
	TSharedPtr<FVoxelLandM5SupportSelector, ESPMode::ThreadSafe> Built =
		MakeShareable(new FVoxelLandM5SupportSelector(MoveTemp(Lease), Routed, Mesh));
	Built->Selections.Reserve(int32(LegCount));
	constexpr int64 Q = int64(1) << 18;
	constexpr int64 LimitQ = int64(1048576) << 18;
	const int64 Radius = Settings.Capsule.RadiusCm, Extent = Radius + ContactSkinCm;
	int32 TotalFaces = 0;
	for (const FVoxelLandM5Route& Route : Routes)
	{
		for (int32 LegIndex = 0; LegIndex < Route.Legs.Num(); ++LegIndex)
		{
			if (IsCancelled())
			{
				return I::Error(E::Cancelled, TEXT("Support selection cancelled"));
			}
			const FVoxelLandM5RouteLeg& Leg = Route.Legs[LegIndex];
			const FVoxelLandM5PointCm Points[] = {Leg.StartFeetCm, Leg.EndFeetCm};
			for (const FVoxelLandM5PointCm& P : Points)
			{
				if (P.X < -1048576 || P.X > 1048576 || P.Y < -1048576 || P.Y > 1048576
					|| P.Z < -1048576 || P.Z > 1048576)
				{
					return I::Error(E::OutOfDomain, TEXT("Route feet outside selector domain"));
				}
			}
			const int64 MinX = (FMath::Min(Points[0].X, Points[1].X) - Extent) * Q;
			const int64 MaxX = (FMath::Max(Points[0].X, Points[1].X) + Extent) * Q;
			const int64 MinY = (FMath::Min(Points[0].Y, Points[1].Y) - Extent) * Q;
			const int64 MaxY = (FMath::Max(Points[0].Y, Points[1].Y) + Extent) * Q;
			const int64 MinZ = (FMath::Min(Points[0].Z, Points[1].Z) - ContactSkinCm) * Q;
			const int64 MaxZ = (FMath::Max(Points[0].Z, Points[1].Z) + 2 * Radius + ContactSkinCm) * Q;
			FVoxelLandM5MultiRouteSupportSelection Selection;
			Selection.RouteId = Route.Id;
			Selection.LegIndex = LegIndex;
			for (int32 Index = 0; Index < Triangles.Num(); ++Index)
			{
				if ((Index & 63) == 0 && IsCancelled())
				{
					return I::Error(E::Cancelled, TEXT("Support selection cancelled"));
				}
				const FVoxelLandM5FixedTriangleQ18& T = Triangles[Index];
				const FVoxelLandM5FixedPointQ18 P[] = {T.A, T.B, T.C};
				for (const FVoxelLandM5FixedPointQ18& V : P)
				{
					if (V.X < -LimitQ || V.X > LimitQ || V.Y < -LimitQ || V.Y > LimitQ
						|| V.Z < -LimitQ || V.Z > LimitQ)
					{
						return I::Error(E::Unproven, TEXT("Mesh face exceeds fixed domain"));
					}
				}
				if (FMath::Max3(P[0].X, P[1].X, P[2].X) < MinX || FMath::Min3(P[0].X, P[1].X, P[2].X) > MaxX
					|| FMath::Max3(P[0].Y, P[1].Y, P[2].Y) < MinY || FMath::Min3(P[0].Y, P[1].Y, P[2].Y) > MaxY
					|| FMath::Max3(P[0].Z, P[1].Z, P[2].Z) < MinZ || FMath::Min3(P[0].Z, P[1].Z, P[2].Z) > MaxZ)
				{
					continue;
				}
				if (T.RendererErrorQ18.X != 0 || T.RendererErrorQ18.Y != 0 || T.RendererErrorQ18.Z != 0
					|| Faces[Index].FaceIndex != T.SourceTriangle || Faces[Index].FaceIndex < 0)
				{
					return I::Error(E::Unproven, TEXT("Ambiguous support face identity or uncertainty"));
				}
				int64 Winding = 0;
				if (!I::Winding(T, Winding))
				{
					return I::Error(E::Unproven, TEXT("Support face winding exceeds exact arithmetic"));
				}
				if (Winding <= 0)
				{
					continue;
				}
				if (Selection.SupportFaces.Num() >= 256 || TotalFaces >= MaxTotalFaces)
				{
					return I::Error(E::BudgetExceeded, TEXT("Support face selection limit exhausted"));
				}
				Selection.SupportFaces.Add(Faces[Index]);
				++TotalFaces;
			}
			if (Selection.SupportFaces.IsEmpty())
			{
				return I::Error(E::Unproven, TEXT("No support faces intersect route footprint"));
			}
			Built->Selections.Add(MoveTemp(Selection));
		}
	}
	if (IsCancelled())
	{
		return I::Error(E::Cancelled, TEXT("Support selection cancelled"));
	}
	Out = MoveTemp(Built);
	return {};
}
