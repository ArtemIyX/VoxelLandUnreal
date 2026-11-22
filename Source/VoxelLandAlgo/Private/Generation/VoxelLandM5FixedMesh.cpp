#include "Generation/VoxelLandM5FixedMesh.h"

#include "Data/Structs/VoxelLandMeshData.h"
#include "Generation/VoxelLandM5Budget.h"
#include <cmath>
#include <limits>

namespace
{
FVoxelLandM5Diagnostic M5FixedMeshFailure(EVoxelLandM5ErrorCode Code, const TCHAR* Message)
{
	FVoxelLandM5Diagnostic Error;
	Error.Code = Code;
	Error.Stage = TEXT("M5.FixedMesh");
	Error.Message = Message;
	return Error;
}

bool M5FixedMeshCoordinate(double Value, int64 GridQ18, int64& Out)
{
	constexpr double LimitCm = 1048576.0;
	constexpr double Scale = 262144.0;
	// Inspect the representation before comparisons or arithmetic: when the
	// host enables DAZ, a nonzero subnormal can otherwise compare as zero and
	// be incorrectly accepted as an on-grid coordinate.
	uint64 Bits = 0;
	FMemory::Memcpy(&Bits, &Value, sizeof(Bits));
	const bool bSubnormal = (Bits & 0x7ff0000000000000ull) == 0 && (Bits & 0x000fffffffffffffull) != 0;
	if (!std::isfinite(Value) || bSubnormal || Value < -LimitCm || Value > LimitCm)
	{
		return false;
	}
	// Power-of-two multiplication is exact for the bounded binary64 input;
	// the cast is safe because its magnitude is at most 2^38.
	const double Scaled = Value * Scale;
	const int64 Integer = static_cast<int64>(Scaled);
	if (double(Integer) != Scaled || Integer % GridQ18 != 0)
	{
		return false;
	}
	Out = Integer;
	return true;
}

bool M5FixedMeshErrorBound(double ErrorCm, int64& Out)
{
	constexpr double LimitCm = 1048576.0;
	constexpr double Scale = 262144.0;
	uint64 Bits = 0;
	FMemory::Memcpy(&Bits, &ErrorCm, sizeof(Bits));
	const bool bSubnormal = (Bits & 0x7ff0000000000000ull) == 0 && (Bits & 0x000fffffffffffffull) != 0;
	if (!std::isfinite(ErrorCm) || bSubnormal || ErrorCm < 0.0 || ErrorCm > LimitCm)
	{
		return false;
	}
	if (ErrorCm == 0.0)
	{
		Out = 0;
		return true;
	}
	// Multiplication by a power of two is exact except possible underflow;
	// nextafter and ceil ensure even tiny positive errors consume one unit.
	const double Upper = std::nextafter(ErrorCm * Scale, std::numeric_limits<double>::infinity());
	if (!std::isfinite(Upper) || Upper > LimitCm * Scale + 1.0)
	{
		return false;
	}
	Out = static_cast<int64>(std::ceil(Upper));
	return true;
}
}

FVoxelLandM5Diagnostic VoxelLand::M5::FixedMesh::Convert(const FVoxelLandM5ValidationTriangle& Triangle,
	int32 VoxelSizeCm, bool bVerifiedProvenance, const FVector3d& RendererErrorCm,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled,
	FVoxelLandM5FixedTriangleQ18& OutTriangle)
{
	using E = EVoxelLandM5ErrorCode;
	if (IsCancelled())
	{
		return M5FixedMeshFailure(E::Cancelled, TEXT("Conversion cancelled"));
	}
	if (!bVerifiedProvenance)
	{
		return M5FixedMeshFailure(E::Unproven, TEXT("Mesh provenance is not established"));
	}
	if (VoxelSizeCm < 1 || VoxelSizeCm > 134217 || Triangle.SourceTriangle < 0)
	{
		return M5FixedMeshFailure(E::InvalidInput, TEXT("Invalid voxel size or triangle index"));
	}
	// Nine coordinates, three error bounds, and bounded fixed overhead. The
	// caller separately accounts for the input and output storage lifetimes.
	if (!Budget->TryChargeWork(32))
	{
		return M5FixedMeshFailure(E::BudgetExceeded, TEXT("Fixed mesh conversion work exhausted"));
	}
	const double Grid = VoxelLandMeshCoordinates::Grid(static_cast<float>(VoxelSizeCm));
	const int64 GridQ18 = static_cast<int64>(Grid * 262144.0);
	if (GridQ18 <= 0 || double(GridQ18) != Grid * 262144.0)
	{
		return M5FixedMeshFailure(E::Unproven, TEXT("Unsupported mesh snap grid"));
	}
	FVoxelLandM5FixedTriangleQ18 Scratch;
	Scratch.SourceTriangle = Triangle.SourceTriangle;
	const FVector3d* Vertices[3] = {&Triangle.A, &Triangle.B, &Triangle.C};
	FVoxelLandM5FixedPointQ18* Fixed[3] = {&Scratch.A, &Scratch.B, &Scratch.C};
	for (int32 Index = 0; Index < 3; ++Index)
	{
		if (IsCancelled())
		{
			return M5FixedMeshFailure(E::Cancelled, TEXT("Conversion cancelled"));
		}
		if (!M5FixedMeshCoordinate(Vertices[Index]->X, GridQ18, Fixed[Index]->X)
			|| !M5FixedMeshCoordinate(Vertices[Index]->Y, GridQ18, Fixed[Index]->Y)
			|| !M5FixedMeshCoordinate(Vertices[Index]->Z, GridQ18, Fixed[Index]->Z))
		{
			return M5FixedMeshFailure(E::Unproven, TEXT("Mesh vertex is outside the envelope or snap grid"));
		}
	}
	if (!M5FixedMeshErrorBound(RendererErrorCm.X, Scratch.RendererErrorQ18.X)
		|| !M5FixedMeshErrorBound(RendererErrorCm.Y, Scratch.RendererErrorQ18.Y)
		|| !M5FixedMeshErrorBound(RendererErrorCm.Z, Scratch.RendererErrorQ18.Z))
	{
		return M5FixedMeshFailure(E::Unproven, TEXT("Renderer uncertainty is invalid or unbounded"));
	}
	if (IsCancelled())
	{
		return M5FixedMeshFailure(E::Cancelled, TEXT("Conversion cancelled before publication"));
	}
	OutTriangle = Scratch;
	return FVoxelLandM5Diagnostic();
}
