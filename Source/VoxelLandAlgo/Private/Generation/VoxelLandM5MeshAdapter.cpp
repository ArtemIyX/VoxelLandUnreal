#include "Generation/VoxelLandM5MeshAdapter.h"

#include "Data/Structs/VoxelLandChunkData.h"
#include "Data/Structs/VoxelLandMeshData.h"
#include <cmath>
#include <limits>

class FVoxelLandM5MeshAdapterInternals final
{
public:
	static FVoxelLandM5Diagnostic M5MeshAdapterError(EVoxelLandM5ErrorCode Code, const TCHAR* Message);
	static int32 M5MeshSampleIndex(int32 X, int32 Y, int32 Z);
	static bool M5MeshEnclose(double Origin, float Local, double& Value, int64& Min, int64& Max);
	static void M5MeshExpand(double& Low, double& High);
	static void M5MeshSubtract(double A0, double A1, double B0, double B1, double& Low, double& High);
	static void M5MeshMultiply(double A0, double A1, double B0, double B1, double& Low, double& High);
	static bool M5MeshProveNondegenerate(const FVector3f& A, const FVector3f& B, const FVector3f& C);
};

FVoxelLandM5Diagnostic FVoxelLandM5MeshAdapterInternals::M5MeshAdapterError(
	EVoxelLandM5ErrorCode Code, const TCHAR* Message)
{
	FVoxelLandM5Diagnostic Error;
	Error.Code = Code;
	Error.Stage = TEXT("MeshAdapter");
	Error.Message = Message;
	return Error;
}

int32 FVoxelLandM5MeshAdapterInternals::M5MeshSampleIndex(int32 X, int32 Y, int32 Z)
{
	return (X + 1) + 35 * ((Y + 1) + 35 * (Z + 1));
}

// The exact sum lies between the adjacent binary64 values on either side of
// the rounded sum. Outward floor/ceil therefore enclose even a rounded sum.
bool FVoxelLandM5MeshAdapterInternals::M5MeshEnclose(double Origin, float Local, double& Value, int64& Min, int64& Max)
{
	constexpr double Limit = 1048576.0;
	uint32 Bits = 0;
	FMemory::Memcpy(&Bits, &Local, sizeof(Bits));
	const bool bSubnormal = (Bits & 0x7f800000u) == 0 && (Bits & 0x007fffffu) != 0;
	if (!std::isfinite(Origin) || !std::isfinite(Local) || bSubnormal)
	{
		return false;
	}
	Value = Origin + double(Local);
	if (!std::isfinite(Value) || Value < -Limit || Value > Limit)
	{
		return false;
	}
	const double Lower = std::floor(std::nextafter(Value, -std::numeric_limits<double>::infinity()));
	const double Upper = std::ceil(std::nextafter(Value, std::numeric_limits<double>::infinity()));
	Min = static_cast<int64>(Lower);
	Max = static_cast<int64>(Upper);
	return true;
}

// Conservative binary64 arithmetic intervals. Inputs are finite binary32
// mesh coordinates whose global values are within the M5 envelope. Their
// differences and products are far from binary64 overflow. The relative pad
// exceeds several ulps per operation; the absolute pad covers zero and very
// small results even on hardware that flushes binary64 subnormals to zero.
void FVoxelLandM5MeshAdapterInternals::M5MeshExpand(double& Low, double& High)
{
	const double Magnitude = FMath::Max(FMath::Abs(Low), FMath::Abs(High));
	const double Pad = Magnitude * 1.e-14 + 1.e-200;
	Low -= Pad;
	High += Pad;
}

void FVoxelLandM5MeshAdapterInternals::M5MeshSubtract(
	double A0, double A1, double B0, double B1, double& Low, double& High)
{
	Low = A0 - B1;
	High = A1 - B0;
	M5MeshExpand(Low, High);
}

void FVoxelLandM5MeshAdapterInternals::M5MeshMultiply(
	double A0, double A1, double B0, double B1, double& Low, double& High)
{
	const double P[4] = {A0 * B0, A0 * B1, A1 * B0, A1 * B1};
	Low = FMath::Min(FMath::Min(P[0], P[1]), FMath::Min(P[2], P[3]));
	High = FMath::Max(FMath::Max(P[0], P[1]), FMath::Max(P[2], P[3]));
	M5MeshExpand(Low, High);
}

bool FVoxelLandM5MeshAdapterInternals::M5MeshProveNondegenerate(
	const FVector3f& A, const FVector3f& B, const FVector3f& C)
{
	double ULow[3], UHigh[3], VLow[3], VHigh[3];
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		M5MeshSubtract(double(B[Axis]), double(B[Axis]), double(A[Axis]), double(A[Axis]), ULow[Axis], UHigh[Axis]);
		M5MeshSubtract(double(C[Axis]), double(C[Axis]), double(A[Axis]), double(A[Axis]), VLow[Axis], VHigh[Axis]);
	}
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		const int32 J = (Axis + 1) % 3, K = (Axis + 2) % 3;
		double P0, P1, Q0, Q1, Cross0, Cross1;
		M5MeshMultiply(ULow[J], UHigh[J], VLow[K], VHigh[K], P0, P1);
		M5MeshMultiply(ULow[K], UHigh[K], VLow[J], VHigh[J], Q0, Q1);
		M5MeshSubtract(P0, P1, Q0, Q1, Cross0, Cross1);
		if (Cross0 > 0.0 || Cross1 < 0.0)
		{
			return true;
		}
	}
	return false;
}

FVoxelLandM5Diagnostic VoxelLand::M5::MeshAdapter::Adapt(const FVoxelLandChunkData& Chunk, float VoxelSizeCm,
	bool bUnlimitedM4, const FVoxelLandMeshData& Mesh, int32 MaxTriangles, int64 MaxWork,
	TFunctionRef<bool()> IsCancelled, TArray<FVoxelLandM5ValidationTriangle>& OutTriangles)
{
	using E = EVoxelLandM5ErrorCode;
	if (Chunk.Key.LOD != 0 || Chunk.Density.Num() != FVoxelLandChunkData::SampleCount || !FMath::IsFinite(VoxelSizeCm)
		|| VoxelSizeCm < 1.f || VoxelSizeCm > 134217.f || FMath::FloorToDouble(double(VoxelSizeCm)) != VoxelSizeCm
		|| !VoxelLandMeshCoordinates::IsKeyInPrecisionRange(Chunk.Key.Coordinate, VoxelSizeCm, bUnlimitedM4)
		|| MaxTriangles < 0 || MaxWork < 0 || Mesh.Indices.Num() % 3 != 0)
	{
		return FVoxelLandM5MeshAdapterInternals::M5MeshAdapterError(
			E::InvalidInput, TEXT("Invalid chunk, voxel size, mesh, or budget"));
	}
	// All 35 stored samples, including the meshing halo, must fit even if the
	// chunk has no emitted triangles. Key and size bounds above make these
	// int64 products representable before the envelope comparison.
	constexpr int64 CoordinateLimit = 1048576;
	const int64 ScaleCm = static_cast<int64>(VoxelSizeCm);
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		const int64 CellOrigin = int64(Chunk.Key.Coordinate[Axis]) * 32;
		const int64 First = (CellOrigin - 1) * ScaleCm;
		const int64 Last = (CellOrigin + 33) * ScaleCm;
		if (First < -CoordinateLimit || Last > CoordinateLimit)
		{
			return FVoxelLandM5MeshAdapterInternals::M5MeshAdapterError(
				E::OutOfDomain, TEXT("Chunk sample halo outside logical envelope"));
		}
	}
	int64 Work = 0;
	int32 EdgeCount = 0;
	for (int32 Z = 0; Z < 32; ++Z)
	{
		if (IsCancelled())
		{
			return FVoxelLandM5MeshAdapterInternals::M5MeshAdapterError(
				E::Cancelled, TEXT("Cancelled while counting owned edges"));
		}
		for (int32 Y = 0; Y < 32; ++Y)
		{
			for (int32 X = 0; X < 32; ++X)
			{
				for (int32 Axis = 0; Axis < 3; ++Axis)
				{
					if (Work >= MaxWork)
					{
						return FVoxelLandM5MeshAdapterInternals::M5MeshAdapterError(
							E::BudgetExceeded, TEXT("Mesh validation work exhausted"));
					}
					++Work;
					const int32 EndX = X + (Axis == 0), EndY = Y + (Axis == 1), EndZ = Z + (Axis == 2);
					const bool bA = Chunk.Density[FVoxelLandM5MeshAdapterInternals::M5MeshSampleIndex(X, Y, Z)] < 0;
					const bool bB =
						Chunk.Density[FVoxelLandM5MeshAdapterInternals::M5MeshSampleIndex(EndX, EndY, EndZ)] < 0;
					EdgeCount += bA != bB;
				}
			}
		}
	}
	const int32 ExpectedTriangles = EdgeCount * 2; // at most 32^3 * 3 * 2
	if (ExpectedTriangles > MaxTriangles)
	{
		return FVoxelLandM5MeshAdapterInternals::M5MeshAdapterError(
			E::BudgetExceeded, TEXT("Triangle admission limit exceeded"));
	}
	if (Mesh.Indices.Num() / 3 != ExpectedTriangles)
	{
		return FVoxelLandM5MeshAdapterInternals::M5MeshAdapterError(
			E::Unproven, TEXT("A sign-changing edge did not emit two triangles"));
	}
	const FVector Origin = VoxelLandMeshCoordinates::RenderOrigin(Chunk.Key.Coordinate, VoxelSizeCm);
	TArray<FVoxelLandM5ValidationTriangle> Scratch;
	Scratch.Reserve(ExpectedTriangles);
	for (int32 I = 0; I < ExpectedTriangles; ++I)
	{
		if ((I & 63) == 0 && IsCancelled())
		{
			return FVoxelLandM5MeshAdapterInternals::M5MeshAdapterError(
				E::Cancelled, TEXT("Cancelled while adapting triangles"));
		}
		if (Work >= MaxWork)
		{
			return FVoxelLandM5MeshAdapterInternals::M5MeshAdapterError(
				E::BudgetExceeded, TEXT("Mesh validation work exhausted"));
		}
		++Work;
		const uint32 IA = Mesh.Indices[3 * I], IB = Mesh.Indices[3 * I + 1], IC = Mesh.Indices[3 * I + 2];
		if (IA >= uint32(Mesh.Vertices.Num()) || IB >= uint32(Mesh.Vertices.Num()) || IC >= uint32(Mesh.Vertices.Num())
			|| IA == IB || IB == IC || IC == IA)
		{
			return FVoxelLandM5MeshAdapterInternals::M5MeshAdapterError(
				E::InvalidInput, TEXT("Invalid triangle indices"));
		}
		const FVector3f A = Mesh.Vertices[IA].Position, B = Mesh.Vertices[IB].Position, C = Mesh.Vertices[IC].Position;
		FVoxelLandM5ValidationTriangle Triangle;
		Triangle.SourceTriangle = I;
		const FVector3f Vertices[3] = {A, B, C};
		FVector3d* Results[3] = {&Triangle.A, &Triangle.B, &Triangle.C};
		int64 Low[3] = {MAX_int64, MAX_int64, MAX_int64};
		int64 High[3] = {MIN_int64, MIN_int64, MIN_int64};
		for (int32 V = 0; V < 3; ++V)
		{
			for (int32 Axis = 0; Axis < 3; ++Axis)
			{
				double Value = 0;
				int64 Min = 0, Max = 0;
				if (!FVoxelLandM5MeshAdapterInternals::M5MeshEnclose(Origin[Axis], Vertices[V][Axis], Value, Min, Max))
				{
					return FVoxelLandM5MeshAdapterInternals::M5MeshAdapterError(
						E::OutOfDomain, TEXT("Mesh position outside logical envelope"));
				}
				(*Results[V])[Axis] = Value;
				Low[Axis] = FMath::Min(Low[Axis], Min);
				High[Axis] = FMath::Max(High[Axis], Max);
			}
		}
		if (!FVoxelLandM5MeshAdapterInternals::M5MeshProveNondegenerate(A, B, C))
		{
			return FVoxelLandM5MeshAdapterInternals::M5MeshAdapterError(
				E::Unproven, TEXT("Triangle nondegeneracy cannot be proven"));
		}
		Triangle.Bounds.Min.X = Low[0];
		Triangle.Bounds.Min.Y = Low[1];
		Triangle.Bounds.Min.Z = Low[2];
		Triangle.Bounds.Max.X = High[0];
		Triangle.Bounds.Max.Y = High[1];
		Triangle.Bounds.Max.Z = High[2];
		Scratch.Add(MoveTemp(Triangle));
	}
	if (IsCancelled())
	{
		return FVoxelLandM5MeshAdapterInternals::M5MeshAdapterError(
			E::Cancelled, TEXT("Cancelled before publishing adapted mesh"));
	}
	OutTriangles = MoveTemp(Scratch);
	return FVoxelLandM5Diagnostic();
}
