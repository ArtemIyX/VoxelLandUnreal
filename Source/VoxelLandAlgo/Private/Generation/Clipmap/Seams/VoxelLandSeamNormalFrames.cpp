#include "Generation/Clipmap/Seams/VoxelLandSeamNormalFrames.h"

#include "Data/Clipmap/Seams/VoxelLandPairedSeamNormalFrame.h"
#include "Data/Meshing/VoxelLandMeshData.h"

bool FVoxelLandM7NormalFrames::MakeAnchor(
	const FVector& LogicalCm, int64 BaseVoxelSizeCm, FVoxelLandM7NormalAnchor& OutAnchor)
{
	if (LogicalCm.ContainsNaN() || BaseVoxelSizeCm < 1 || BaseVoxelSizeCm > 2000000)
	{
		return false;
	}
	const FVector Lattice = LogicalCm / double(BaseVoxelSizeCm);
	FVoxelLandM7NormalAnchor Candidate;
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		const double Component = Lattice[Axis];
		if (Component < -double(MaxBaseCell) || Component > double(MaxBaseCell) + 1.0)
		{
			return false;
		}
		int64 Cell = FMath::FloorToInt64(Component);
		int32 Fraction = int32(FMath::FloorToInt64((Component - double(Cell)) * FractionOne + 0.5));
		if (Fraction == FractionOne)
		{
			++Cell;
			Fraction = 0;
		}
		Candidate.BaseCell[Axis] = Cell;
		Candidate.FractionQ16[Axis] = Fraction;
	}
	if (!IsCanonical(Candidate))
	{
		return false;
	}
	OutAnchor = Candidate;
	return true;
}

bool FVoxelLandM7NormalFrames::IsCanonical(const FVoxelLandM7NormalAnchor& Anchor)
{
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		if (Anchor.BaseCell[Axis] < -MaxBaseCell || Anchor.BaseCell[Axis] > MaxBaseCell || Anchor.FractionQ16[Axis] < 0
			|| Anchor.FractionQ16[Axis] >= FractionOne)
		{
			return false;
		}
	}
	return true;
}

int16 FVoxelLandM7NormalFrames::Sample(TConstArrayView<int16> Stencil, const FIntVector& Cell)
{
	const FIntVector Offset = Cell + FIntVector(1);
	return Stencil[Offset.X + StencilAxis * (Offset.Y + StencilAxis * Offset.Z)];
}

FVector3f FVoxelLandM7NormalFrames::FirstCrossingDirection(TConstArrayView<int16> Stencil)
{
	// X, then Y, then Z. On each axis, enumerate the transverse corners
	// in fixed order and orient the first crossing from solid toward air.
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		for (int32 V = 0; V <= 1; ++V)
		{
			for (int32 U = 0; U <= 1; ++U)
			{
				FIntVector First = FIntVector::ZeroValue;
				First[(Axis + 1) % 3] = U;
				First[(Axis + 2) % 3] = V;
				FIntVector Second = First;
				Second[Axis] = 1;
				const bool bFirstSolid = Sample(Stencil, First) < 0;
				if (bFirstSolid != (Sample(Stencil, Second) < 0))
				{
					FVector3f Direction = FVector3f::ZeroVector;
					Direction[Axis] = bFirstSolid ? 1.f : -1.f;
					return Direction;
				}
			}
		}
	}
	return FVector3f::UpVector;
}

bool FVoxelLandM7NormalFrames::Evaluate(
	TConstArrayView<int16> Stencil, const FVoxelLandM7NormalAnchor& Anchor, FVoxelLandM7NormalFrame& OutFrame)
{
	if (Stencil.Num() != StencilSamples || !IsCanonical(Anchor))
	{
		return false;
	}

	const FVector Fraction = FVector(Anchor.FractionQ16) / double(FractionOne);
	const FIntVector Steps[] = {FIntVector(1, 0, 0), FIntVector(0, 1, 0), FIntVector(0, 0, 1)};
	FVector Interpolated = FVector::ZeroVector;
	for (int32 Z = 0; Z <= 1; ++Z)
	{
		for (int32 Y = 0; Y <= 1; ++Y)
		{
			for (int32 X = 0; X <= 1; ++X)
			{
				const FIntVector Corner(X, Y, Z);
				const int32 DX = int32(Sample(Stencil, Corner + Steps[0])) - int32(Sample(Stencil, Corner - Steps[0]));
				const int32 DY = int32(Sample(Stencil, Corner + Steps[1])) - int32(Sample(Stencil, Corner - Steps[1]));
				const int32 DZ = int32(Sample(Stencil, Corner + Steps[2])) - int32(Sample(Stencil, Corner - Steps[2]));
				const double WX = X ? Fraction.X : 1.0 - Fraction.X;
				const double WY = Y ? Fraction.Y : 1.0 - Fraction.Y;
				const double WZ = Z ? Fraction.Z : 1.0 - Fraction.Z;
				Interpolated += FVector(DX, DY, DZ) * (WX * WY * WZ);
			}
		}
	}

	FVoxelLandM7NormalFrame Frame;
	Frame.bFallback = Interpolated.SizeSquared() <= GradientToleranceSquared;
	Frame.Normal = Frame.bFallback ? FirstCrossingDirection(Stencil)
								   : FVector3f(Interpolated.GetSafeNormal(GradientToleranceSquared));
	Frame.Tangent = TangentForNormal(Frame.Normal);
	OutFrame = Frame;
	return true;
}

FVector3f FVoxelLandM7NormalFrames::TangentForNormal(const FVector3f& Normal)
{
	const FVector3f Abs = Normal.GetAbs();
	const FVector3f Reference = Abs.X <= Abs.Y && Abs.X <= Abs.Z
								  ? FVector3f::ForwardVector
								  : (Abs.Y <= Abs.Z ? FVector3f::RightVector : FVector3f::UpVector);
	return FVector3f::CrossProduct(Reference, Normal).GetSafeNormal();
}

FVoxelLandM7NormalFrame FVoxelLandM7NormalFrames::BlendFromInterior(
	const FVoxelLandM7NormalFrame& Interior, const FVoxelLandM7NormalFrame& Shared, uint16 SharedWeight)
{
	if (SharedWeight == 0)
	{
		return Interior;
	}
	if (SharedWeight == MAX_uint16)
	{
		return Shared;
	}

	FVoxelLandM7NormalFrame Result;
	const double Weight = double(SharedWeight) / MAX_uint16;
	const FVector Normal = FVector(Interior.Normal) * (1.0 - Weight) + FVector(Shared.Normal) * Weight;
	Result.bFallback = Normal.SizeSquared() <= GradientToleranceSquared;
	Result.Normal = Result.bFallback ? (Weight >= 0.5 ? Shared.Normal : Interior.Normal)
									 : FVector3f(Normal.GetSafeNormal(GradientToleranceSquared));
	Result.Tangent = TangentForNormal(Result.Normal);
	return Result;
}

bool FVoxelLandM7NormalFrames::IsValidFrame(const FVoxelLandM7NormalFrame& Frame)
{
	constexpr float Tolerance = 1.e-4f;
	return !Frame.Normal.ContainsNaN() && !Frame.Tangent.ContainsNaN()
		&& FMath::IsNearlyEqual(Frame.Normal.SizeSquared(), 1.f, Tolerance)
		&& FMath::IsNearlyEqual(Frame.Tangent.SizeSquared(), 1.f, Tolerance)
		&& FMath::Abs(FVector3f::DotProduct(Frame.Normal, Frame.Tangent)) <= Tolerance;
}

bool FVoxelLandM7NormalFrames::SameVectorBits(const FVector3f& First, const FVector3f& Second)
{
	return FMemory::Memcmp(&First.X, &Second.X, sizeof(float)) == 0
		&& FMemory::Memcmp(&First.Y, &Second.Y, sizeof(float)) == 0
		&& FMemory::Memcmp(&First.Z, &Second.Z, sizeof(float)) == 0;
}

bool FVoxelLandM7NormalFrames::RecordFrame(
	TMap<int32, int32>& Targets, int32 Vertex, int32 PairIndex, TConstArrayView<FVoxelLandM7PairedNormalFrame> Pairs)
{
	if (const int32* Prior = Targets.Find(Vertex))
	{
		return SameVectorBits(Pairs[*Prior].Frame.Normal, Pairs[PairIndex].Frame.Normal)
			&& SameVectorBits(Pairs[*Prior].Frame.Tangent, Pairs[PairIndex].Frame.Tangent);
	}
	Targets.Add(Vertex, PairIndex);
	return true;
}

bool FVoxelLandM7NormalFrames::ApplyPairedFrames(
	TConstArrayView<FVoxelLandM7PairedNormalFrame> Pairs, FVoxelLandMeshData& First, FVoxelLandMeshData& Second)
{
	if (&First == &Second || Pairs.Num() > MaxAnchorsPerBatch)
	{
		return false;
	}
	TMap<int32, int32> FirstTargets, SecondTargets;
	FirstTargets.Reserve(Pairs.Num());
	SecondTargets.Reserve(Pairs.Num());
	for (int32 Index = 0; Index < Pairs.Num(); ++Index)
	{
		const FVoxelLandM7PairedNormalFrame& Pair = Pairs[Index];
		if (!First.Vertices.IsValidIndex(Pair.FirstVertex) || !Second.Vertices.IsValidIndex(Pair.SecondVertex)
			|| !IsValidFrame(Pair.Frame) || !RecordFrame(FirstTargets, Pair.FirstVertex, Index, Pairs)
			|| !RecordFrame(SecondTargets, Pair.SecondVertex, Index, Pairs))
		{
			return false;
		}
	}

	for (const FVoxelLandM7PairedNormalFrame& Pair : Pairs)
	{
		First.Vertices[Pair.FirstVertex].Normal = Pair.Frame.Normal;
		First.Vertices[Pair.FirstVertex].Tangent = Pair.Frame.Tangent;
		Second.Vertices[Pair.SecondVertex].Normal = Pair.Frame.Normal;
		Second.Vertices[Pair.SecondVertex].Tangent = Pair.Frame.Tangent;
	}
	return true;
}
