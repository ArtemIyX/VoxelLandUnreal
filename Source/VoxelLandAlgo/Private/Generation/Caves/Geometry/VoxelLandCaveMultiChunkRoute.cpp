#include "Generation/Caves/Geometry/VoxelLandCaveMultiChunkRoute.h"

#include "Data/Meshing/VoxelLandMeshResult.h"
#include "Data/Field/Chunks/VoxelLandChunkData.h"
#include "Data/Meshing/VoxelLandMeshData.h"
#include "Data/Meshing/VoxelLandMeshingScratch.h"
#include "Generation/Caves/Infrastructure/VoxelLandCaveBudget.h"
#include "Generation/Caves/Infrastructure/VoxelLandCaveBudgetLease.h"
#include "Generation/Caves/Field/VoxelLandCaveChunkSet.h"
#include "Generation/Caves/Field/VoxelLandCaveFixedMesh.h"
#include "Generation/Caves/Geometry/VoxelLandCaveFixedSupport.h"
#include "Generation/Caves/Geometry/VoxelLandCavePiecewiseSupport.h"
#include "Generation/Caves/Field/VoxelLandCaveMeshAdapter.h"
#include "Generation/Caves/Geometry/VoxelLandCaveSweptTriangle.h"
#include "Generation/Caves/Field/VoxelLandCaveScratchChunks.h"
#include "Generation/Caves/Field/VoxelLandCaveScratchMesh.h"
#include "Generation/Meshing/VoxelLandMeshing.h"

class FVoxelLandM5MultiChunkRouteInternals final
{
public:
	static FVoxelLandM5Diagnostic M5MultiRouteError(EVoxelLandM5ErrorCode Code, const TCHAR* Message)
	{
		FVoxelLandM5Diagnostic Result;
		Result.Code = Code;
		Result.Stage = TEXT("M5.MultiChunkRoute");
		Result.Message = Message;
		return Result;
	}
};

FVoxelLandM5Diagnostic VoxelLand::M5::MultiChunkRoute::Certify(TConstArrayView<FVoxelLandChunkData> Chunks,
	const FIntVector& MinInclusive, const FIntVector& MaxExclusive, int32 VoxelSizeCm, bool bUnlimitedM4,
	const FVoxelLandM5PointCm& StartFeet, const FVoxelLandM5PointCm& EndFeet, int64 RadiusCm, int64 HeightCm,
	int64 MarginCm, int64 ContactSkinCm, int32 MaxSlopeNumerator, int32 MaxSlopeDenominator,
	TConstArrayView<FVoxelLandM5ChunkFaceSelection> SupportFaces,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled)
{
	using E = EVoxelLandM5ErrorCode;
	constexpr int64 Limit = 1048576;
	constexpr int32 MaxChunks = 16;
	constexpr int32 MaxVertices = 34 * 34 * 34;
	constexpr int32 MaxTriangles = 32 * 32 * 32 * 3 * 2;
	if (IsCancelled())
	{
		return FVoxelLandM5MultiChunkRouteInternals::M5MultiRouteError(
			E::Cancelled, TEXT("Multi-chunk route cancelled"));
	}
	if (VoxelSizeCm < 1 || VoxelSizeCm > 134217 || RadiusCm < 1 || RadiusCm > Limit || HeightCm < 2 * RadiusCm
		|| HeightCm > Limit || MarginCm < 0 || MarginCm > Limit || ContactSkinCm < 0 || ContactSkinCm > Limit
		|| MaxSlopeNumerator < 0 || MaxSlopeDenominator <= 0 || SupportFaces.IsEmpty() || SupportFaces.Num() > 256)
	{
		return FVoxelLandM5MultiChunkRouteInternals::M5MultiRouteError(
			E::InvalidInput, TEXT("Invalid multi-chunk route inputs"));
	}
	const auto Agreement =
		ChunkSet::CheckSampleAgreement(Chunks, MinInclusive, MaxExclusive, MaxChunks, Budget, IsCancelled);
	if (Agreement.Code != E::None)
	{
		return Agreement;
	}
	// All multiplications fit int64: int32 chunk coordinate * 32 * 134217.
	const int64 Scale = VoxelSizeCm;
	const int64 Radius = RadiusCm + MarginCm;
	const FVoxelLandM5PointCm Points[] = {StartFeet, EndFeet};
	for (const auto& Point : Points)
	{
		const int64 Values[] = {Point.X, Point.Y, Point.Z};
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			if (Values[Axis] < -Limit || Values[Axis] > Limit)
			{
				return FVoxelLandM5MultiChunkRouteInternals::M5MultiRouteError(
					E::OutOfDomain, TEXT("Route point outside logical domain"));
			}
			const int64 Lower = Values[Axis] - (Axis == 2 ? MarginCm : Radius);
			const int64 Upper = Values[Axis] + (Axis == 2 ? HeightCm + MarginCm : Radius);
			const int64 BoxMin = int64(MinInclusive[Axis]) * 32 * Scale;
			const int64 BoxMax = int64(MaxExclusive[Axis]) * 32 * Scale;
			if (Lower <= BoxMin + 2 * Scale || Upper >= BoxMax - 2 * Scale)
			{
				return FVoxelLandM5MultiChunkRouteInternals::M5MultiRouteError(
					E::Unproven, TEXT("Sweep requires omitted neighboring mesh owners"));
			}
		}
	}
	// Reserve for all retained obstacles plus mesher/adapter temporary arrays.
	// Input chunks and selections are caller-owned. Constants bound a LOD-zero
	// mesher's maximum arrays; the fourfold payload includes TArray growth slack.
	const int64 PerChunkBytes =
		4 * int64(MaxVertices) * (sizeof(FVoxelLandMeshVertex) + sizeof(int32))
		+ 4 * int64(MaxTriangles) * (3 * sizeof(uint32) + 2 * sizeof(FVoxelLandM5ValidationTriangle) + sizeof(uint8));
	const int64 Bytes =
		int64(Chunks.Num()) * PerChunkBytes + 4 * int64(256) * sizeof(FVoxelLandM5FixedTriangleQ18) + 1048576;
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Lease;
	const int64 Work = int64(Chunks.Num()) * 4000000
					 + int64(Chunks.Num()) * MaxTriangles * (int64(SupportFaces.Num()) + 1)
					 + int64(SupportFaces.Num()) * (SupportFaces.Num() + 2);
	if (!Budget->TryReserveBytes(Bytes, Lease) || !Budget->TryChargeWork(Work))
	{
		return FVoxelLandM5MultiChunkRouteInternals::M5MultiRouteError(
			E::BudgetExceeded, TEXT("Multi-chunk mesh admission exhausted"));
	}
	for (int32 I = 0; I < SupportFaces.Num(); ++I)
	{
		if (IsCancelled())
		{
			return FVoxelLandM5MultiChunkRouteInternals::M5MultiRouteError(
				E::Cancelled, TEXT("Multi-chunk route cancelled"));
		}
		for (int32 J = 0; J < I; ++J)
		{
			if (SupportFaces[I].ChunkCoordinate == SupportFaces[J].ChunkCoordinate
				&& SupportFaces[I].FaceIndex == SupportFaces[J].FaceIndex)
			{
				return FVoxelLandM5MultiChunkRouteInternals::M5MultiRouteError(
					E::InvalidInput, TEXT("Duplicate support face"));
			}
		}
	}
	TArray<FVoxelLandM5ValidationTriangle> Obstacles;
	TArray<FVoxelLandM5FixedTriangleQ18> Support;
	Support.Reserve(SupportFaces.Num());
	int32 Matched = 0;
	for (const FVoxelLandChunkData& Chunk : Chunks)
	{
		if (IsCancelled())
		{
			return FVoxelLandM5MultiChunkRouteInternals::M5MultiRouteError(
				E::Cancelled, TEXT("Multi-chunk route cancelled"));
		}
		FVoxelLandMeshingScratch Scratch;
		FVoxelLandMeshData Mesh;
		const auto MeshResult =
			VoxelLandMeshing::BuildSurfaceNets(Chunk, float(VoxelSizeCm), Scratch, Mesh, IsCancelled, bUnlimitedM4);
		if (MeshResult != EVoxelLandMeshResult::Success)
		{
			return FVoxelLandM5MultiChunkRouteInternals::M5MultiRouteError(
				MeshResult == EVoxelLandMeshResult::Cancelled ? E::Cancelled : E::Unproven,
				TEXT("Actual CPU meshing failed"));
		}
		TArray<FVoxelLandM5ValidationTriangle> Triangles;
		const auto Adapted = MeshAdapter::Adapt(Chunk, float(VoxelSizeCm), bUnlimitedM4, Mesh, MaxTriangles,
			32 * 32 * 32 * 3 + MaxTriangles, IsCancelled, Triangles);
		if (Adapted.Code != E::None)
		{
			return Adapted;
		}
		for (int32 Index = 0; Index < Triangles.Num(); ++Index)
		{
			if ((Index & 63) == 0 && IsCancelled())
			{
				return FVoxelLandM5MultiChunkRouteInternals::M5MultiRouteError(
					E::Cancelled, TEXT("Multi-chunk route cancelled"));
			}
			bool bSupport = false;
			for (const auto& Face : SupportFaces)
			{
				if (Face.ChunkCoordinate == Chunk.Key.Coordinate && Face.FaceIndex == Index)
				{
					bSupport = true;
					break;
				}
			}
			if (bSupport)
			{
				FVoxelLandM5FixedTriangleQ18 Fixed;
				const auto Converted = FixedMesh::Convert(
					Triangles[Index], VoxelSizeCm, true, FVector3d::ZeroVector, Budget, IsCancelled, Fixed);
				if (Converted.Code != E::None)
				{
					return Converted;
				}
				Support.Add(Fixed);
				++Matched;
			}
			else
			{
				Obstacles.Add(Triangles[Index]);
			}
		}
	}
	if (Matched != SupportFaces.Num())
	{
		return FVoxelLandM5MultiChunkRouteInternals::M5MultiRouteError(
			E::InvalidInput, TEXT("Support face references an absent chunk or face"));
	}
	constexpr int64 Q = int64(1) << 18;
	FVoxelLandM5FixedPointQ18 A, B;
	A.X = StartFeet.X * Q;
	A.Y = StartFeet.Y * Q;
	A.Z = StartFeet.Z * Q;
	B.X = EndFeet.X * Q;
	B.Y = EndFeet.Y * Q;
	B.Z = EndFeet.Z * Q;
	const auto Contact = FixedSupport::CertifyLinearContactWithMargin(A, B, RadiusCm * Q, HeightCm * Q,
		ContactSkinCm * Q, MarginCm * Q, MaxSlopeNumerator, MaxSlopeDenominator, Support, Budget, IsCancelled);
	if (Contact.Code != E::None)
	{
		return Contact;
	}
	return SweptTriangle::Check(
		StartFeet, EndFeet, RadiusCm, HeightCm, MarginCm, FVector3d::ZeroVector, Obstacles, Budget, IsCancelled);
}

FVoxelLandM5Diagnostic VoxelLand::M5::MultiChunkRoute::Certify(const FVoxelLandM5ScratchMesh& Mesh,
	const FVoxelLandM5PointCm& StartFeet, const FVoxelLandM5PointCm& EndFeet, int64 RadiusCm, int64 HeightCm,
	int64 MarginCm, int64 ContactSkinCm, int32 MaxSlopeNumerator, int32 MaxSlopeDenominator,
	TConstArrayView<FVoxelLandM5ChunkFaceSelection> SupportFaces,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled)
{
	using E = EVoxelLandM5ErrorCode;
	using I = FVoxelLandM5MultiChunkRouteInternals;
	constexpr int64 Limit = 1048576, Q = int64(1) << 18;
	if (IsCancelled())
	{
		return I::M5MultiRouteError(E::Cancelled, TEXT("Multi-chunk route cancelled"));
	}
	const FVoxelLandM5ScratchChunks& Source = Mesh.GetSource();
	const int64 Scale = Source.GetVoxelSizeCm();
	const auto Triangles = Mesh.GetTriangles();
	const auto Faces = Mesh.GetFaces();
	if (!Mesh.IsOwnedBy(*Budget) || !Source.IsOwnedBy(*Budget) || Scale < 1 || Scale > 134217 || RadiusCm < 1
		|| RadiusCm > Limit || HeightCm < 2 * RadiusCm || HeightCm > Limit || MarginCm < 0 || MarginCm > Limit
		|| ContactSkinCm < 0 || ContactSkinCm > Limit || MaxSlopeNumerator < 0 || MaxSlopeDenominator <= 0
		|| SupportFaces.IsEmpty() || SupportFaces.Num() > 256 || Faces.Num() != Triangles.Num()
		|| Triangles.Num() > 196608)
	{
		return I::M5MultiRouteError(E::InvalidInput, TEXT("Invalid retained route inputs"));
	}
	const auto Agreement = ChunkSet::CheckSampleAgreement(
		Source.GetChunks(), Source.GetMinInclusive(), Source.GetMaxExclusive(), 16, Budget, IsCancelled);
	if (Agreement.Code != E::None)
	{
		return Agreement;
	}
	const FVoxelLandM5PointCm Points[] = {StartFeet, EndFeet};
	for (const auto& Point : Points)
	{
		const int64 Values[] = {Point.X, Point.Y, Point.Z};
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			if (Values[Axis] < -Limit || Values[Axis] > Limit)
			{
				return I::M5MultiRouteError(E::OutOfDomain, TEXT("Route point outside logical domain"));
			}
			const int64 Lower = Values[Axis] - (Axis == 2 ? MarginCm : RadiusCm + MarginCm);
			const int64 Upper = Values[Axis] + (Axis == 2 ? HeightCm + MarginCm : RadiusCm + MarginCm);
			const int64 BoxMin = int64(Source.GetMinInclusive()[Axis]) * 32 * Scale;
			const int64 BoxMax = int64(Source.GetMaxExclusive()[Axis]) * 32 * Scale;
			if (Lower <= BoxMin + 2 * Scale || Upper >= BoxMax - 2 * Scale)
			{
				return I::M5MultiRouteError(E::Unproven, TEXT("Sweep requires omitted neighboring mesh owners"));
			}
		}
	}
	const int64 Work = int64(Triangles.Num()) * (int64(SupportFaces.Num()) + 1)
					 + int64(SupportFaces.Num()) * (SupportFaces.Num() + 2) + 64;
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Lease;
	if (!Budget->TryChargeWork(Work)
		|| !Budget->TryReserveBytes(4096 + 4 * int64(SupportFaces.Num()) * sizeof(FVoxelLandM5FixedTriangleQ18), Lease))
	{
		return I::M5MultiRouteError(E::BudgetExceeded, TEXT("Retained route admission exhausted"));
	}
	for (int32 A = 0; A < SupportFaces.Num(); ++A)
	{
		for (int32 B = 0; B < A; ++B)
		{
			if (SupportFaces[A].ChunkCoordinate == SupportFaces[B].ChunkCoordinate
				&& SupportFaces[A].FaceIndex == SupportFaces[B].FaceIndex)
			{
				return I::M5MultiRouteError(E::InvalidInput, TEXT("Duplicate support face"));
			}
		}
	}
	TArray<FVoxelLandM5FixedTriangleQ18> Support;
	Support.Reserve(SupportFaces.Num());
	for (int32 Index = 0; Index < Triangles.Num(); ++Index)
	{
		if ((Index & 63) == 0 && IsCancelled())
		{
			return I::M5MultiRouteError(E::Cancelled, TEXT("Multi-chunk route cancelled"));
		}
		const auto& T = Triangles[Index];
		const FVoxelLandM5FixedPointQ18 Vertices[] = {T.A, T.B, T.C};
		for (const auto& V : Vertices)
		{
			if (V.X < -Limit * Q || V.X > Limit * Q || V.Y < -Limit * Q || V.Y > Limit * Q || V.Z < -Limit * Q
				|| V.Z > Limit * Q)
			{
				return I::M5MultiRouteError(E::Unproven, TEXT("Retained face outside fixed domain"));
			}
		}
		if (T.RendererErrorQ18.X != 0 || T.RendererErrorQ18.Y != 0 || T.RendererErrorQ18.Z != 0
			|| Faces[Index].FaceIndex < 0 || Faces[Index].FaceIndex != T.SourceTriangle)
		{
			return I::M5MultiRouteError(E::Unproven, TEXT("Retained face identity or uncertainty invalid"));
		}
		bool bSelected = false;
		for (const auto& Face : SupportFaces)
		{
			if (Face.ChunkCoordinate == Faces[Index].ChunkCoordinate && Face.FaceIndex == Faces[Index].FaceIndex)
			{
				bSelected = true;
				break;
			}
		}
		if (bSelected)
		{
			Support.Add(T);
		}
	}
	if (Support.Num() != SupportFaces.Num())
	{
		return I::M5MultiRouteError(E::InvalidInput, TEXT("Support face references an absent or duplicate face"));
	}
	FVoxelLandM5FixedPointQ18 A, B;
	A.X = StartFeet.X * Q;
	A.Y = StartFeet.Y * Q;
	A.Z = StartFeet.Z * Q;
	B.X = EndFeet.X * Q;
	B.Y = EndFeet.Y * Q;
	B.Z = EndFeet.Z * Q;
	FVoxelLandM5Diagnostic Contact = FixedSupport::CertifyLinearContactWithMargin(A, B, RadiusCm * Q, HeightCm * Q,
		ContactSkinCm * Q, MarginCm * Q, MaxSlopeNumerator, MaxSlopeDenominator, Support, Budget, IsCancelled);
	if (Contact.Code == E::Unproven)
	{
		Contact = PiecewiseSupport::CertifyLinearContactWithMargin(A, B, RadiusCm * Q, HeightCm * Q, ContactSkinCm * Q,
			MarginCm * Q, MaxSlopeNumerator, MaxSlopeDenominator, Support, Budget, IsCancelled);
	}
	if (Contact.Code != E::None)
	{
		return Contact;
	}
	// Validate the sweep domain and arithmetic environment even with no obstacles.
	const auto SweepInputs = SweptTriangle::Check(
		StartFeet, EndFeet, RadiusCm, HeightCm, MarginCm, FVector3d::ZeroVector, {}, Budget, IsCancelled);
	if (SweepInputs.Code != E::None)
	{
		return SweepInputs;
	}
	const auto FloorQ = [](int64 Value) {
		return Value / Q - int64(Value % Q < 0);
	};
	const auto CeilQ = [&FloorQ](int64 Value) {
		return -FloorQ(-Value);
	};
	for (int32 Index = 0; Index < Triangles.Num(); ++Index)
	{
		if ((Index & 63) == 0 && IsCancelled())
		{
			return I::M5MultiRouteError(E::Cancelled, TEXT("Multi-chunk route cancelled"));
		}
		bool bSelected = false;
		for (const auto& Face : SupportFaces)
		{
			if (Face.ChunkCoordinate == Faces[Index].ChunkCoordinate && Face.FaceIndex == Faces[Index].FaceIndex)
			{
				bSelected = true;
				break;
			}
		}
		if (bSelected)
		{
			continue;
		}
		const auto& T = Triangles[Index];
		FVoxelLandM5ValidationTriangle Obstacle;
		Obstacle.A = FVector3d(double(T.A.X) / Q, double(T.A.Y) / Q, double(T.A.Z) / Q);
		Obstacle.B = FVector3d(double(T.B.X) / Q, double(T.B.Y) / Q, double(T.B.Z) / Q);
		Obstacle.C = FVector3d(double(T.C.X) / Q, double(T.C.Y) / Q, double(T.C.Z) / Q);
		Obstacle.Bounds.Min.X = FloorQ(FMath::Min3(T.A.X, T.B.X, T.C.X));
		Obstacle.Bounds.Min.Y = FloorQ(FMath::Min3(T.A.Y, T.B.Y, T.C.Y));
		Obstacle.Bounds.Min.Z = FloorQ(FMath::Min3(T.A.Z, T.B.Z, T.C.Z));
		Obstacle.Bounds.Max.X = CeilQ(FMath::Max3(T.A.X, T.B.X, T.C.X));
		Obstacle.Bounds.Max.Y = CeilQ(FMath::Max3(T.A.Y, T.B.Y, T.C.Y));
		Obstacle.Bounds.Max.Z = CeilQ(FMath::Max3(T.A.Z, T.B.Z, T.C.Z));
		Obstacle.SourceTriangle = T.SourceTriangle;
		const auto Check = SweptTriangle::Check(StartFeet, EndFeet, RadiusCm, HeightCm, MarginCm, FVector3d::ZeroVector,
			MakeArrayView(&Obstacle, 1), Budget, IsCancelled);
		if (Check.Code != E::None)
		{
			return Check;
		}
	}
	if (IsCancelled())
	{
		return I::M5MultiRouteError(E::Cancelled, TEXT("Multi-chunk route cancelled"));
	}
	return {};
}
