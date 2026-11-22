#include "Generation/VoxelLandM5FloorSupport.h"

class FVoxelLandM5FloorSupportInternals final
{
public:
	static FVoxelLandM5Diagnostic FloorError(EVoxelLandM5ErrorCode Code, const TCHAR* Message);
	static bool GetRectangleTriangle(
		const FVoxelLandM5FloorTriangleCm& Triangle, int64 Z, FVoxelLandM5ClosedBoxCm& Bounds, uint8& OutMask);
};

FVoxelLandM5Diagnostic FVoxelLandM5FloorSupportInternals::FloorError(EVoxelLandM5ErrorCode Code, const TCHAR* Message)
{
	FVoxelLandM5Diagnostic Result;
	Result.Code = Code;
	Result.Stage = FName(TEXT("FloorSupport"));
	Result.Message = Message;
	return Result;
}

// Coordinates are compared only: no coordinate arithmetic, and therefore no
// cross-product overflow even at the ends of the int64 domain.
bool FVoxelLandM5FloorSupportInternals::GetRectangleTriangle(
	const FVoxelLandM5FloorTriangleCm& Triangle, int64 Z, FVoxelLandM5ClosedBoxCm& Bounds, uint8& OutMask)
{
	const FVoxelLandM5PointCm* Points[3] = {&Triangle.A, &Triangle.B, &Triangle.C};
	if (Points[0]->Z != Z || Points[1]->Z != Z || Points[2]->Z != Z)
	{
		return false;
	}
	Bounds.Min.X = FMath::Min3(Points[0]->X, Points[1]->X, Points[2]->X);
	Bounds.Max.X = FMath::Max3(Points[0]->X, Points[1]->X, Points[2]->X);
	Bounds.Min.Y = FMath::Min3(Points[0]->Y, Points[1]->Y, Points[2]->Y);
	Bounds.Max.Y = FMath::Max3(Points[0]->Y, Points[1]->Y, Points[2]->Y);
	Bounds.Min.Z = Bounds.Max.Z = Z;
	if (Bounds.Min.X == Bounds.Max.X || Bounds.Min.Y == Bounds.Max.Y)
	{
		return false;
	}
	int32 Corners[3];
	uint8 Mask = 0;
	for (int32 Index = 0; Index < 3; ++Index)
	{
		const auto& P = *Points[Index];
		if ((P.X != Bounds.Min.X && P.X != Bounds.Max.X) || (P.Y != Bounds.Min.Y && P.Y != Bounds.Max.Y))
		{
			return false;
		}
		Corners[Index] = (P.X == Bounds.Max.X ? 1 : 0) | (P.Y == Bounds.Max.Y ? 2 : 0);
		Mask |= uint8(1u << Corners[Index]);
	}
	const int32 AX = Corners[0] & 1, AY = Corners[0] >> 1;
	const int32 BX = Corners[1] & 1, BY = Corners[1] >> 1;
	const int32 CX = Corners[2] & 1, CY = Corners[2] >> 1;
	if ((BX - AX) * (CY - AY) - (BY - AY) * (CX - AX) <= 0)
	{
		return false;
	}
	OutMask = Mask;
	return true;
}

FVoxelLandM5Diagnostic VoxelLand::M5::FloorSupport::CheckHorizontalRectangularSupport(
	const FVoxelLandM5ClosedBoxCm& Footprint, TConstArrayView<FVoxelLandM5FloorTriangleCm> Triangles, int64 MaxChecks,
	TFunctionRef<bool()> IsCancelled)
{
	if (IsCancelled())
	{
		return FVoxelLandM5FloorSupportInternals::FloorError(
			EVoxelLandM5ErrorCode::Cancelled, TEXT("Floor support check cancelled"));
	}
	if (Footprint.Min.X >= Footprint.Max.X || Footprint.Min.Y >= Footprint.Max.Y || Footprint.Min.Z != Footprint.Max.Z
		|| MaxChecks < 0)
	{
		return FVoxelLandM5FloorSupportInternals::FloorError(
			EVoxelLandM5ErrorCode::InvalidInput, TEXT("Invalid horizontal footprint or work budget"));
	}
	if (Triangles.Num() > 256)
	{
		return FVoxelLandM5FloorSupportInternals::FloorError(
			EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("Too many floor triangles"));
	}
	TArray<FVoxelLandM5ClosedBoxCm> TriangleBounds;
	TArray<uint8> Masks;
	TriangleBounds.Reserve(Triangles.Num());
	Masks.Reserve(Triangles.Num());
	for (const auto& Triangle : Triangles)
	{
		if (IsCancelled())
		{
			return FVoxelLandM5FloorSupportInternals::FloorError(
				EVoxelLandM5ErrorCode::Cancelled, TEXT("Floor support check cancelled"));
		}
		FVoxelLandM5ClosedBoxCm Bounds;
		uint8 Mask = 0;
		if (!FVoxelLandM5FloorSupportInternals::GetRectangleTriangle(Triangle, Footprint.Min.Z, Bounds, Mask))
		{
			return FVoxelLandM5FloorSupportInternals::FloorError(
				EVoxelLandM5ErrorCode::Unproven, TEXT("Unsupported or misoriented floor triangle"));
		}
		TriangleBounds.Add(Bounds);
		Masks.Add(Mask);
	}
	TArray<FVoxelLandM5ClosedBoxCm> Rectangles;
	TArray<int64> XBoundaries{Footprint.Min.X, Footprint.Max.X};
	int64 Checks = 0;
	for (int32 I = 0; I < Triangles.Num(); ++I)
	{
		for (int32 J = I + 1; J < Triangles.Num(); ++J)
		{
			if (IsCancelled())
			{
				return FVoxelLandM5FloorSupportInternals::FloorError(
					EVoxelLandM5ErrorCode::Cancelled, TEXT("Floor support check cancelled"));
			}
			if (Checks >= MaxChecks)
			{
				return FVoxelLandM5FloorSupportInternals::FloorError(
					EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("Floor support work budget exceeded"));
			}
			++Checks;
			const auto& A = TriangleBounds[I];
			const auto& B = TriangleBounds[J];
			const uint8 Common = Masks[I] & Masks[J];
			if (A.Min.X != B.Min.X || A.Max.X != B.Max.X || A.Min.Y != B.Min.Y || A.Max.Y != B.Max.Y
				|| (Masks[I] | Masks[J]) != 15 || (Common != 9 && Common != 6))
			{
				continue;
			}
			Rectangles.Add(A);
			XBoundaries.Add(FMath::Clamp(A.Min.X, Footprint.Min.X, Footprint.Max.X));
			XBoundaries.Add(FMath::Clamp(A.Max.X, Footprint.Min.X, Footprint.Max.X));
		}
	}
	XBoundaries.Sort();
	Rectangles.Sort(
		[](const FVoxelLandM5ClosedBoxCm& A, const FVoxelLandM5ClosedBoxCm& B) { return A.Min.Y < B.Min.Y; });
	for (int32 I = 0; I + 1 < XBoundaries.Num(); ++I)
	{
		if (XBoundaries[I] == XBoundaries[I + 1])
		{
			continue;
		}
		int64 CoveredY = Footprint.Min.Y;
		bool bStarted = false;
		for (const auto& Rectangle : Rectangles)
		{
			if (IsCancelled())
			{
				return FVoxelLandM5FloorSupportInternals::FloorError(
					EVoxelLandM5ErrorCode::Cancelled, TEXT("Floor support check cancelled"));
			}
			if (Checks >= MaxChecks)
			{
				return FVoxelLandM5FloorSupportInternals::FloorError(
					EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("Floor support work budget exceeded"));
			}
			++Checks;
			if (Rectangle.Min.X > XBoundaries[I] || Rectangle.Max.X < XBoundaries[I + 1]
				|| Rectangle.Max.Y < Footprint.Min.Y || Rectangle.Min.Y > CoveredY)
			{
				continue;
			}
			bStarted = true;
			CoveredY = FMath::Max(CoveredY, Rectangle.Max.Y);
			if (CoveredY >= Footprint.Max.Y)
			{
				break;
			}
		}
		if (!bStarted || CoveredY < Footprint.Max.Y)
		{
			return FVoxelLandM5FloorSupportInternals::FloorError(
				EVoxelLandM5ErrorCode::Unproven, TEXT("Floor tiles do not cover the complete footprint"));
		}
	}
	if (IsCancelled())
	{
		return FVoxelLandM5FloorSupportInternals::FloorError(
			EVoxelLandM5ErrorCode::Cancelled, TEXT("Floor support check cancelled"));
	}
	return FVoxelLandM5Diagnostic{};
}
