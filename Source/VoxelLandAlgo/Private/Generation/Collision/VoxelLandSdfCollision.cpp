#include "Generation/Collision/VoxelLandSdfCollision.h"

#include "Generation/Collision/VoxelLandCollisionField.h"
#include "VoxelLandSdfCollisionGeometry.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

class FVoxelLandSdfCollisionSearch final
{
public:
	FVoxelLandSdfCollisionSearch(
		const FVoxelLandCollisionField& InField, const FVoxelLandCapsuleSweepRequest& InRequest)
		: Field(InField)
		, Request(InRequest)
	{
	}

	[[nodiscard]] FVoxelLandCapsuleSweepResult Run();

private:
	struct FCell
	{
		FInt64Vector Coordinate = FInt64Vector::ZeroValue;
		double Density[2][2][2] = {};
	};

	struct FRegion
	{
		FVector3d Offset = FVector3d::ZeroVector;
		double Width = 1.0;
		double LowerTime = 0.0;
		int32 CellIndex = 0;
		int32 Order = 0;
		int32 Depth = 0;

		bool operator<(const FRegion& Other) const
		{
			return LowerTime < Other.LowerTime || (LowerTime == Other.LowerTime && Order < Other.Order);
		}
	};

	struct FWitness
	{
		FVector3d Position = FVector3d::ZeroVector;
		double Time = TNumericLimits<double>::Max();
		int32 CellIndex = INDEX_NONE;
		bool bIsPenetrating = false;
	};

	static constexpr double NoTime = TNumericLimits<double>::Max();
	static constexpr double GeometryPad = FVoxelLandSdfCollisionGeometry::RoundingPad;
	// Absorb conversion and squared-distance roundoff at closed tangencies.
	static constexpr double ContactRoundingPad = GeometryPad * 0.125;
	static constexpr double EdgeFractionPad = 1.0e-12;

	const FVoxelLandCollisionField& Field;
	const FVoxelLandCapsuleSweepRequest& Request;
	FVoxelLandCapsuleSweepResult Result;
	FVoxelLandSdfCollisionGeometry Geometry;
	FInt64Vector Anchor = FInt64Vector::ZeroValue;
	TArray<FCell> Cells;
	TArray<FRegion> Pending;
	FWitness Best;
	double MissingTime = NoTime;
	double InvalidTime = NoTime;
	double UnresolvedTime = NoTime;
	double SolvedLowerTime = NoTime;
	int32 NextOrder = 0;
	bool bHasUncertainInitialOverlap = false;

	[[nodiscard]] bool Validate() const;
	[[nodiscard]] bool PrepareBounds(FInt64Vector& Min, FInt64Vector& Max);
	[[nodiscard]] bool IsWithinCellBudget(const FInt64Vector& Min, const FInt64Vector& Max) const;
	void GatherCells(const FInt64Vector& Min, const FInt64Vector& Max);
	void GatherCell(const FInt64Vector& Coordinate);
	[[nodiscard]] double PendingTime() const;
	[[nodiscard]] double FirstPossibleTime() const;
	[[nodiscard]] double TimeTolerance() const;
	[[nodiscard]] bool ShouldRefine() const;
	void Refine();
	void SolveRegion(const FRegion& Region);
	void SplitRegion(const FRegion& Region);
	void AddRegion(FRegion Region);
	void RecordUnavailable(EVoxelLandCollisionFieldStatus Status, const FVoxelLandChunkKey& Key, double Time);
	void RecordWitness(const FBox3d& Solid, const FRegion& Region, bool bIsCertifiedSolid);
	void RecordCorners(const FRegion& Region, const double (&Density)[2][2][2]);
	void RecordEdgeCrossings(const FRegion& Region, const double (&Density)[2][2][2]);
	[[nodiscard]] static FBox3d BoundPossibleSolid(const FBox3d& Box, const double (&Density)[2][2][2]);
	[[nodiscard]] static double GetCorner(const double (&Density)[2][2][2], int32 Index);
	[[nodiscard]] static FVector3d GetCornerOffset(int32 Index);
	[[nodiscard]] bool VerifyPenetration(const FBox3d& Solid, const FCell& Cell, const FVector3d& Nearest) const;
	[[nodiscard]] FBox3d GetBox(const FRegion& Region) const;
	[[nodiscard]] static double SampleRegion(const FCell& Cell, const FVector3d& Point, const FInt64Vector& Anchor);
	static void ReadRegion(const FCell& Cell, const FRegion& Region, double (&Density)[2][2][2]);
	static void GetDensityBounds(const double (&Density)[2][2][2], double& Min, double& Max);
	[[nodiscard]] FVoxelLandCapsuleSweepResult Finish();
	void CopyContact();
};

bool FVoxelLandSdfCollisionSearch::Validate() const
{
	return FMath::IsFinite(Field.GetVoxelSizeCm()) && Field.GetVoxelSizeCm() > 0.0 && !Request.Start.ContainsNaN()
		&& !Request.End.ContainsNaN() && FMath::IsFinite(Request.RadiusCm) && Request.RadiusCm > 0.0
		&& FMath::IsFinite(Request.HalfHeightCm) && Request.HalfHeightCm >= Request.RadiusCm
		&& FMath::IsFinite(Request.SkinCm) && Request.SkinCm >= 0.0 && FMath::IsFinite(Request.ContactToleranceCm)
		&& Request.ContactToleranceCm > 0.0 && Request.CellBudget > 0
		&& Request.CellBudget <= FVoxelLandCapsuleSweepRequest::MaxCellBudget && Request.RegionBudget > 0
		&& Request.RegionBudget <= FVoxelLandCapsuleSweepRequest::MaxRegionBudget && Request.SubdivisionDepth >= 0
		&& Request.SubdivisionDepth <= FVoxelLandCapsuleSweepRequest::MaxSubdivisionDepth;
}

bool FVoxelLandSdfCollisionSearch::PrepareBounds(FInt64Vector& Min, FInt64Vector& Max)
{
	const double Scale = Field.GetVoxelSizeCm();
	const double Margin = Request.SkinCm + GeometryPad * Scale;
	const FVector3d Extent(Request.RadiusCm + Margin, Request.RadiusCm + Margin, Request.HalfHeightCm + Margin);
	if (!Field.TryGetCell(Request.Start, Anchor)
		|| !Field.TryGetCell(Request.Start.ComponentMin(Request.End) - Extent, Min)
		|| !Field.TryGetCell(Request.Start.ComponentMax(Request.End) + Extent, Max))
	{
		return false;
	}

	Geometry.Start = Request.Start / Scale - FVector3d(Anchor);
	Geometry.Delta = (Request.End - Request.Start) / Scale;
	Geometry.Radius = Request.RadiusCm / Scale;
	Geometry.Skin = Request.SkinCm / Scale;
	Geometry.SpineHalfLength = (Request.HalfHeightCm - Request.RadiusCm) / Scale;
	return FMath::IsFinite(Geometry.Radius + Geometry.Skin + Geometry.SpineHalfLength) && !Geometry.Start.ContainsNaN()
		&& !Geometry.Delta.ContainsNaN();
}

bool FVoxelLandSdfCollisionSearch::IsWithinCellBudget(const FInt64Vector& Min, const FInt64Vector& Max) const
{
	int64 Count = 1;
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		const int64 Extent = Max[Axis] - Min[Axis] + 1;
		if (Extent <= 0 || Extent > Request.CellBudget / Count)
		{
			return false;
		}
		Count *= Extent;
	}
	return true;
}

FVoxelLandCapsuleSweepResult FVoxelLandSdfCollisionSearch::Run()
{
	FInt64Vector Min;
	FInt64Vector Max;
	if (!Validate() || !PrepareBounds(Min, Max))
	{
		return Result;
	}
	if (!IsWithinCellBudget(Min, Max))
	{
		Result.Status = EVoxelLandCapsuleSweepStatus::Indeterminate;
		return Result;
	}

	constexpr int32 InitialCapacity = 64;
	Cells.Reserve(FMath::Min(InitialCapacity, Request.CellBudget));
	Pending.Reserve(FMath::Min(InitialCapacity, Request.RegionBudget));
	GatherCells(Min, Max);
	Refine();
	return Finish();
}

void FVoxelLandSdfCollisionSearch::GatherCells(const FInt64Vector& Min, const FInt64Vector& Max)
{
	for (int64 Z = Min.Z; Z <= Max.Z; ++Z)
	{
		for (int64 Y = Min.Y; Y <= Max.Y; ++Y)
		{
			for (int64 X = Min.X; X <= Max.X; ++X)
			{
				GatherCell(FInt64Vector(X, Y, Z));
			}
		}
	}
}

void FVoxelLandSdfCollisionSearch::GatherCell(const FInt64Vector& Coordinate)
{
	++Result.CellsVisited;
	FCell Cell;
	Cell.Coordinate = Coordinate;
	FVoxelLandChunkKey MissingKey;
	const EVoxelLandCollisionFieldStatus Status = Field.ReadCell(Coordinate, Cell.Density, &MissingKey);
	double Min = 0.0;
	double Max = 0.0;
	if (Status == EVoxelLandCollisionFieldStatus::Available)
	{
		GetDensityBounds(Cell.Density, Min, Max);
		if (Min > 0.0)
		{
			return;
		}
	}

	const FVector3d Corner(Coordinate - Anchor);
	const FBox3d Box(Corner, Corner + FVector3d(1.0));
	const FBox3d PossibleSolid =
		Status == EVoxelLandCollisionFieldStatus::Available && Max > 0.0 ? BoundPossibleSolid(Box, Cell.Density) : Box;
	double Lower = 0.0;
	double Upper = 0.0;
	if (!Geometry.Intersect(PossibleSolid, Geometry.Skin + GeometryPad, Lower, Upper))
	{
		return;
	}
	if (Status != EVoxelLandCollisionFieldStatus::Available)
	{
		RecordUnavailable(Status, MissingKey, Lower);
		return;
	}
	if (Pending.Num() >= Request.RegionBudget)
	{
		UnresolvedTime = FMath::Min(UnresolvedTime, Lower);
		bHasUncertainInitialOverlap |= Lower == 0.0 && Geometry.CanStartPenetrating(PossibleSolid);
		return;
	}

	FRegion Region;
	Region.CellIndex = Cells.Add(Cell);
	Region.LowerTime = Lower;
	Region.Order = NextOrder++;
	Pending.HeapPush(Region);
}

void FVoxelLandSdfCollisionSearch::RecordUnavailable(
	EVoxelLandCollisionFieldStatus Status, const FVoxelLandChunkKey& Key, double Time)
{
	if (Status == EVoxelLandCollisionFieldStatus::Missing && Time < MissingTime)
	{
		MissingTime = Time;
		Result.MissingKey = Key;
	}
	else if (Status == EVoxelLandCollisionFieldStatus::Invalid)
	{
		InvalidTime = FMath::Min(InvalidTime, Time);
	}
}

double FVoxelLandSdfCollisionSearch::PendingTime() const
{
	return Pending.IsEmpty() ? NoTime : Pending.HeapTop().LowerTime;
}

double FVoxelLandSdfCollisionSearch::FirstPossibleTime() const
{
	return FMath::Min3(FMath::Min3(PendingTime(), UnresolvedTime, SolvedLowerTime), MissingTime, InvalidTime);
}

double FVoxelLandSdfCollisionSearch::TimeTolerance() const
{
	const double TravelCm = Geometry.Delta.Length() * Field.GetVoxelSizeCm();
	return TravelCm > 0.0 ? FMath::Min(1.0, Request.ContactToleranceCm / TravelCm) : 0.0;
}

bool FVoxelLandSdfCollisionSearch::ShouldRefine() const
{
	if (Pending.IsEmpty() || Best.bIsPenetrating || PendingTime() >= FMath::Min(MissingTime, InvalidTime))
	{
		return false;
	}
	if (Best.Time == 0.0 && PendingTime() == 0.0)
	{
		// Other regions touching the closed skin can still contain a strict
		// overlap with the actual capsule at t=0.
		return true;
	}
	return Best.CellIndex == INDEX_NONE || Best.Time > FirstPossibleTime() + TimeTolerance();
}

void FVoxelLandSdfCollisionSearch::Refine()
{
	while (ShouldRefine() && Result.RegionsVisited < Request.RegionBudget)
	{
		FRegion Region;
		Pending.HeapPop(Region, EAllowShrinking::No);
		++Result.RegionsVisited;
		SolveRegion(Region);
	}
	if (ShouldRefine())
	{
		UnresolvedTime = FMath::Min(UnresolvedTime, PendingTime());
		bHasUncertainInitialOverlap |= Best.Time == 0.0 && PendingTime() == 0.0;
	}
}

void FVoxelLandSdfCollisionSearch::SolveRegion(const FRegion& Region)
{
	const FCell& Cell = Cells[Region.CellIndex];
	const FBox3d Box = GetBox(Region);
	if (Best.Time == 0.0 && !Geometry.CanStartPenetrating(Box))
	{
		return;
	}

	double Density[2][2][2];
	ReadRegion(Cell, Region, Density);
	double Min;
	double Max;
	GetDensityBounds(Density, Min, Max);
	if (Max <= 0.0)
	{
		RecordWitness(Box, Region, true);
		return;
	}

	const FBox3d PossibleSolid = BoundPossibleSolid(Box, Density);
	RecordCorners(Region, Density);
	RecordEdgeCrossings(Region, Density);
	if (Best.bIsPenetrating || (Best.Time == 0.0 && !Geometry.CanStartPenetrating(PossibleSolid)))
	{
		return;
	}

	if (Region.Depth >= Request.SubdivisionDepth)
	{
		UnresolvedTime = FMath::Min(UnresolvedTime, Region.LowerTime);
		bHasUncertainInitialOverlap |= Region.LowerTime == 0.0 && Geometry.CanStartPenetrating(PossibleSolid);
		return;
	}
	SplitRegion(Region);
}

void FVoxelLandSdfCollisionSearch::SplitRegion(const FRegion& Region)
{
	const double Width = Region.Width * 0.5;
	for (int32 Corner = 0; Corner < 8; ++Corner)
	{
		const FVector3d Offset = GetCornerOffset(Corner);
		FRegion Child = Region;
		Child.Offset += Offset * Width;
		Child.Width = Width;
		++Child.Depth;
		AddRegion(Child);
	}
}

void FVoxelLandSdfCollisionSearch::AddRegion(FRegion Region)
{
	double Density[2][2][2];
	ReadRegion(Cells[Region.CellIndex], Region, Density);
	double Min;
	double Max;
	GetDensityBounds(Density, Min, Max);
	if (Min > 0.0)
	{
		return;
	}

	const FBox3d Box = GetBox(Region);
	const FBox3d PossibleSolid = Max > 0.0 ? BoundPossibleSolid(Box, Density) : Box;
	if (Best.Time == 0.0 && !Geometry.CanStartPenetrating(PossibleSolid))
	{
		return;
	}
	double Upper;
	if (!Geometry.Intersect(PossibleSolid, Geometry.Skin + GeometryPad, Region.LowerTime, Upper)
		|| Region.LowerTime > Best.Time)
	{
		return;
	}
	if (Pending.Num() >= Request.RegionBudget)
	{
		UnresolvedTime = FMath::Min(UnresolvedTime, Region.LowerTime);
		bHasUncertainInitialOverlap |= Region.LowerTime == 0.0 && Geometry.CanStartPenetrating(PossibleSolid);
		return;
	}
	Region.Order = NextOrder++;
	Pending.HeapPush(Region);
}

bool FVoxelLandSdfCollisionSearch::VerifyPenetration(
	const FBox3d& Solid, const FCell& Cell, const FVector3d& Nearest) const
{
	const double Margin = Geometry.GetInteriorMargin(Nearest);
	if (Margin <= GeometryPad)
	{
		return false;
	}
	if (SampleRegion(Cell, Nearest, Anchor) < 0.0)
	{
		return true;
	}
	const FVector3d TowardInterior = Solid.GetCenter() - Nearest;
	const double Distance = TowardInterior.Length();
	if (Distance <= GeometryPad)
	{
		return false;
	}
	const double Fraction = FMath::Min(1.0, Margin * 0.5 / Distance);
	return SampleRegion(Cell, Nearest + TowardInterior * Fraction, Anchor) < 0.0;
}

void FVoxelLandSdfCollisionSearch::RecordWitness(const FBox3d& Solid, const FRegion& Region, bool bIsCertifiedSolid)
{
	double Lower;
	double Upper;
	if (!Geometry.Intersect(Solid, Geometry.Skin + ContactRoundingPad, Lower, Upper))
	{
		if (bIsCertifiedSolid)
		{
			// The expanded numerical envelope touched. Keep its lower bound
			// unless the closed capsule supplies a witnessed upper bound.
			UnresolvedTime = FMath::Min(UnresolvedTime, Region.LowerTime);
		}
		return;
	}
	SolvedLowerTime = FMath::Min(SolvedLowerTime, bIsCertifiedSolid ? Region.LowerTime : Lower);

	const FVector3d Position = Geometry.GetNearestBoxPoint(Solid, Upper);
	const bool bIsPenetrating = Upper == 0.0 && VerifyPenetration(Solid, Cells[Region.CellIndex], Position);
	if (Upper < Best.Time || (bIsPenetrating && !Best.bIsPenetrating))
	{
		Best.Position = Position;
		Best.Time = Upper;
		Best.CellIndex = Region.CellIndex;
		Best.bIsPenetrating = bIsPenetrating;
	}
}

void FVoxelLandSdfCollisionSearch::RecordCorners(const FRegion& Region, const double (&Density)[2][2][2])
{
	const FVector3d Min = GetBox(Region).Min;
	for (int32 Z = 0; Z < 2 && !Best.bIsPenetrating; ++Z)
	{
		for (int32 Y = 0; Y < 2 && !Best.bIsPenetrating; ++Y)
		{
			for (int32 X = 0; X < 2 && !Best.bIsPenetrating; ++X)
			{
				if (Density[Z][Y][X] <= 0.0)
				{
					const FVector3d Point = Min + FVector3d(X, Y, Z) * Region.Width;
					RecordWitness(FBox3d(Point, Point), Region, false);
				}
			}
		}
	}
}

void FVoxelLandSdfCollisionSearch::RecordEdgeCrossings(const FRegion& Region, const double (&Density)[2][2][2])
{
	const FCell& Cell = Cells[Region.CellIndex];
	const FVector3d Min = GetBox(Region).Min;
	for (int32 Axis = 0; Axis < 3 && !Best.bIsPenetrating; ++Axis)
	{
		const int32 Bit = 1 << Axis;
		for (int32 Corner = 0; Corner < 8 && !Best.bIsPenetrating; ++Corner)
		{
			if ((Corner & Bit) != 0)
			{
				continue;
			}
			const double Lower = GetCorner(Density, Corner);
			const double Upper = GetCorner(Density, Corner | Bit);
			if ((Lower < 0.0 && Upper > 0.0) || (Lower > 0.0 && Upper < 0.0))
			{
				// Bias the edge witness slightly into solid, then verify against the root.
				FVector3d Offset = GetCornerOffset(Corner);
				Offset[Axis] = FMath::Clamp(
					Lower / (Lower - Upper) + (Lower < 0.0 ? -EdgeFractionPad : EdgeFractionPad), 0.0, 1.0);
				const FVector3d Point = Min + Offset * Region.Width;
				if (SampleRegion(Cell, Point, Anchor) <= 0.0)
				{
					RecordWitness(FBox3d(Point, Point), Region, false);
				}
			}
		}
	}
}

FBox3d FVoxelLandSdfCollisionSearch::BoundPossibleSolid(const FBox3d& Box, const double (&Density)[2][2][2])
{
	// On a slice perpendicular to an axis, the bilinear interpolant cannot
	// be negative until one of the four edges bounding that slice is negative.
	// Restricting this range on all three axes keeps the whole possible solid.
	FVector3d Min(1.0);
	FVector3d Max(0.0);
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		const int32 Bit = 1 << Axis;
		for (int32 Corner = 0; Corner < 8; ++Corner)
		{
			if ((Corner & Bit) != 0)
			{
				continue;
			}
			const double Lower = GetCorner(Density, Corner);
			const double Upper = GetCorner(Density, Corner | Bit);
			if (Lower <= 0.0)
			{
				Min[Axis] = 0.0;
			}
			if (Upper <= 0.0)
			{
				Max[Axis] = 1.0;
			}
			if (Lower > 0.0 && Upper <= 0.0)
			{
				Min[Axis] = FMath::Min(Min[Axis], FMath::Max(0.0, Lower / (Lower - Upper) - EdgeFractionPad));
			}
			else if (Lower <= 0.0 && Upper > 0.0)
			{
				Max[Axis] = FMath::Max(Max[Axis], FMath::Min(1.0, Lower / (Lower - Upper) + EdgeFractionPad));
			}
		}
	}
	const FVector3d Width = Box.Max - Box.Min;
	return FBox3d(Box.Min + Min * Width, Box.Min + Max * Width);
}

double FVoxelLandSdfCollisionSearch::GetCorner(const double (&Density)[2][2][2], int32 Index)
{
	return Density[(Index >> 2) & 1][(Index >> 1) & 1][Index & 1];
}

FVector3d FVoxelLandSdfCollisionSearch::GetCornerOffset(int32 Index)
{
	return FVector3d(Index & 1, (Index >> 1) & 1, (Index >> 2) & 1);
}

FBox3d FVoxelLandSdfCollisionSearch::GetBox(const FRegion& Region) const
{
	const FVector3d Min = FVector3d(Cells[Region.CellIndex].Coordinate - Anchor) + Region.Offset;
	return FBox3d(Min, Min + FVector3d(Region.Width));
}

double FVoxelLandSdfCollisionSearch::SampleRegion(const FCell& Cell, const FVector3d& Point, const FInt64Vector& Anchor)
{
	return FVoxelLandCollisionField::EvaluateCell(Cell.Density, Point - FVector3d(Cell.Coordinate - Anchor));
}

void FVoxelLandSdfCollisionSearch::ReadRegion(const FCell& Cell, const FRegion& Region, double (&Density)[2][2][2])
{
	for (int32 Z = 0; Z < 2; ++Z)
	{
		for (int32 Y = 0; Y < 2; ++Y)
		{
			for (int32 X = 0; X < 2; ++X)
			{
				const FVector3d Fraction = Region.Offset + FVector3d(X, Y, Z) * Region.Width;
				Density[Z][Y][X] = FVoxelLandCollisionField::EvaluateCell(Cell.Density, Fraction);
			}
		}
	}
}

void FVoxelLandSdfCollisionSearch::GetDensityBounds(const double (&Density)[2][2][2], double& Min, double& Max)
{
	Min = Density[0][0][0];
	Max = Min;
	for (const double (&Layer)[2][2] : Density)
	{
		for (const double (&Row)[2] : Layer)
		{
			for (const double Value : Row)
			{
				Min = FMath::Min(Min, Value);
				Max = FMath::Max(Max, Value);
			}
		}
	}
}

void FVoxelLandSdfCollisionSearch::CopyContact()
{
	const FCell& Cell = Cells[Best.CellIndex];
	FVector3d Gradient;
	const double Value = FVoxelLandCollisionField::EvaluateCell(
		Cell.Density, Best.Position - FVector3d(Cell.Coordinate - Anchor), &Gradient);
	checkSlow(Value <= UE_DOUBLE_KINDA_SMALL_NUMBER);
	Result.Point = (FVector3d(Anchor) + Best.Position) * Field.GetVoxelSizeCm();
	const FVector3d OppositeMotion = (-Geometry.Delta).GetSafeNormal(UE_DOUBLE_SMALL_NUMBER, FVector3d::UpVector);
	const FVector3d AwayFromPoint = Geometry.GetNearestSpinePoint(Best.Position, Best.Time) - Best.Position;
	Result.Normal = Gradient.GetSafeNormal(
		UE_DOUBLE_SMALL_NUMBER, AwayFromPoint.GetSafeNormal(UE_DOUBLE_SMALL_NUMBER, OppositeMotion));
	Result.Time = Best.Time;
}

FVoxelLandCapsuleSweepResult FVoxelLandSdfCollisionSearch::Finish()
{
	const double First = FirstPossibleTime();
	if (Best.bIsPenetrating)
	{
		Result.Status = EVoxelLandCapsuleSweepStatus::StartPenetrating;
		CopyContact();
		Result.MissingKey.Reset();
		return Result;
	}
	if (First == NoTime)
	{
		Result.Status = EVoxelLandCapsuleSweepStatus::Clear;
		Result.Time = 1.0;
		Result.LastSafeTime = 1.0;
		return Result;
	}

	Result.Time = First;
	Result.LastSafeTime = FMath::Max(0.0, First - GeometryPad / FMath::Max(1.0, Geometry.Delta.Length()));
	if (MissingTime <= First && MissingTime < InvalidTime)
	{
		Result.Status = EVoxelLandCapsuleSweepStatus::Unavailable;
		return Result;
	}
	Result.MissingKey.Reset();
	if (Best.CellIndex != INDEX_NONE && Best.Time < FMath::Min(MissingTime, InvalidTime)
		&& Best.Time <= First + TimeTolerance() && !(Best.Time == 0.0 && bHasUncertainInitialOverlap))
	{
		Result.Status = EVoxelLandCapsuleSweepStatus::Contact;
		CopyContact();
		return Result;
	}
	Result.Status =
		InvalidTime <= First ? EVoxelLandCapsuleSweepStatus::Invalid : EVoxelLandCapsuleSweepStatus::Indeterminate;
	return Result;
}

FVoxelLandCapsuleSweepResult FVoxelLandSdfCollision::SweepCapsule(
	const FVoxelLandCollisionField& Field, const FVoxelLandCapsuleSweepRequest& Request)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(VoxelLand_SdfCapsuleSweep);
	FVoxelLandSdfCollisionSearch Search(Field, Request);
	return Search.Run();
}

FVoxelLandCapsuleSweepResult FVoxelLandSdfCollision::OverlapCapsule(
	const FVoxelLandCollisionField& Field, const FVoxelLandCapsuleSweepRequest& Request)
{
	FVoxelLandCapsuleSweepRequest Overlap = Request;
	Overlap.End = Overlap.Start;
	return SweepCapsule(Field, Overlap);
}
