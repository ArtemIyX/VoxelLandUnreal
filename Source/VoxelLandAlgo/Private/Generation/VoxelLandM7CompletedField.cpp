#include "Generation/VoxelLandGenerator.h"

#include "Generation/VoxelLandM4HeightGrid.h"
#include "Generation/VoxelLandM6Redistance.h"
#include "Generation/VoxelLandM6StampMath.h"
#include "Generation/VoxelLandM7CompletedBox.h"

class FVoxelLandM7FieldBounds final
{
public:
	static bool Ordered(const FInt64Vector& First, const FInt64Vector& Last)
	{
		return First.X <= Last.X && First.Y <= Last.Y && First.Z <= Last.Z;
	}

	static bool Inside(const FInt64Vector& First, const FInt64Vector& Last, int64 Limit)
	{
		return Ordered(FInt64Vector(-Limit), First) && Ordered(Last, FInt64Vector(Limit));
	}

	static int64 Volume(int64 Axis)
	{
		return Axis * Axis * Axis;
	}
};

bool FVoxelLandGenerator::SupportsM7CompletedField() const
{
	return !M5Lease.IsValid() && (M4HeightGrid.IsValid() || M4Unlimited.IsValid());
}

FVoxelLandError FVoxelLandGenerator::ValidateM7RawFootprint(const FInt64Vector& First,
	const FInt64Vector& Last) const
{
	constexpr int64 MaxBaseCoordinate = int64(1) << 27;
	constexpr int64 MaxBaselineZCm = 2000000;
	constexpr int64 MaxExactFloatCm = 16777216;
	if (!SupportsM7CompletedField())
	{
		return {EVoxelLandErrorCode::UnsupportedLod, TEXT("M7 completed fields require ordinary M4 terrain")};
	}
	const FInt64Vector Margin(int64(bM4NormalizeSlope), int64(bM4NormalizeSlope), 0);
	if (!FVoxelLandM7FieldBounds::Ordered(First, Last)
		|| !FVoxelLandM7FieldBounds::Inside(First, Last, MaxBaseCoordinate)
		|| !FVoxelLandM7FieldBounds::Inside(First - Margin, Last + Margin, MaxBaseCoordinate))
	{
		return {EVoxelLandErrorCode::NonFiniteCoordinate, TEXT("M7 footprint exceeds the base lattice range")};
	}
	const int64 VoxelSizeCm = int64(Profile.VoxelSizeCm);
	const FInt64Vector Min = (First - Margin) * VoxelSizeCm;
	const FInt64Vector Max = (Last + Margin) * VoxelSizeCm;
	if (Min.Z < -MaxBaselineZCm || Max.Z > MaxBaselineZCm)
	{
		return {EVoxelLandErrorCode::NonFiniteCoordinate, TEXT("M7 completed field exceeds the M4 Z range")};
	}
	if (M4Unlimited)
	{
		return {};
	}
	const bool bNative = !bM4FixedLatticeShaping
		&& ((M4Overhang.Displacement.bEnabled && M4Overhang.Displacement.AmplitudeQ8 != 0)
			|| (M4HeightDetail.bEnabled && M4HeightDetail.AmplitudeQ8 != 0));
	if (bNative && (Min.X < -MaxExactFloatCm || Max.X > MaxExactFloatCm
		|| Min.Y < -MaxExactFloatCm || Max.Y > MaxExactFloatCm))
	{
		return {EVoxelLandErrorCode::NonFiniteCoordinate, TEXT("M7 field exceeds the native exact XY lattice")};
	}
	int32 Height = 0;
	if (!M4HeightGrid->Sample(Min.X, Min.Y, Height) || !M4HeightGrid->Sample(Max.X, Max.Y, Height))
	{
		return {EVoxelLandErrorCode::InvalidParameter, TEXT("M7 footprint including slope reads is outside the M4 grid")};
	}
	return {};
}

FVoxelLandError FVoxelLandGenerator::ValidateM7StampBox(const FInt64Vector& First, const FInt64Vector& Last) const
{
	if (!bM6LegacySphereEnvelope)
	{
		return {};
	}
	const int64 Size = int64(Profile.VoxelSizeCm);
	if (!FVoxelLandM7FieldBounds::Inside(
			First * Size, Last * Size, FVoxelLandM6StampMath::LegacySphereLimitCm))
	{
		return {EVoxelLandErrorCode::NonFiniteCoordinate, TEXT("M7 footprint exceeds the legacy sphere envelope")};
	}
	return {};
}

FVoxelLandError FVoxelLandGenerator::PlanM7CompletedBox(const FInt64Vector& First,
	int32 Axis, FVoxelLandM7CompletedBox& Out) const
{
	constexpr int32 MaxOutputAxis = FVoxelLandChunkData::SamplesPerAxis;
	if (Axis < 1 || Axis > MaxOutputAxis)
	{
		return {EVoxelLandErrorCode::InvalidParameter, TEXT("M7 completed box must fit a base tile")};
	}
	FVoxelLandM7CompletedBox Box;
	Box.First = First;
	Box.Axis = Axis;
	Box.Plan = MakeM6FilterPlan(First, Axis);
	const int64 CompatibilityHalo = GetM6CompatibilityHalo();
	const FInt64Vector RawMargin(Box.Plan.Halo + CompatibilityHalo);
	const FInt64Vector Last = First + FInt64Vector(Axis - 1);
	if (const FVoxelLandError Error = ValidateM7RawFootprint(First - RawMargin, Last + RawMargin))
	{
		return Error;
	}
	if (const FVoxelLandError Error = ValidateM7StampBox(
			First - FInt64Vector(Box.Plan.Halo), Last + FInt64Vector(Box.Plan.Halo)))
	{
		return Error;
	}
	const int64 InputAxis = Axis + 2 * Box.Plan.Halo;
	// At this extent tiling introduces at most two compatibility halos per axis.
	const int64 RawAxis = InputAxis + (InputAxis > MaxOutputAxis ? 4 : 2) * CompatibilityHalo;
	Box.Work = FVoxelLandM7FieldBounds::Volume(RawAxis)
		+ FVoxelLandM7FieldBounds::Volume(InputAxis) * (1 + M6Stamps.Num())
		+ FVoxelLandM7FieldBounds::Volume(Axis) * FVoxelLandM6Redistance::EdgeChecksPerSample(Box.Plan.RedistanceBand);
	for (int32 Index = 0; Index < M6Filters.Num(); ++Index)
	{
		if (Box.Plan.HasStage(Index))
		{
			const int64 StageAxis = Axis + 2 * Box.Plan.OutputHalo[Index];
			Box.Work += FVoxelLandM7FieldBounds::Volume(StageAxis)
				* (FVoxelLandM6FilterPlan::FilterRadius(M6Filters[Index]) ? 27 : 1);
		}
	}
	Out = Box;
	return {};
}

FVoxelLandError FVoxelLandGenerator::ReadM7CompletedBox(const FVoxelLandM7CompletedBox& Box,
	FVoxelLandWorkerContext& Worker, TArray<int16>& Out, TFunctionRef<bool()> IsCancelled,
	TFunctionRef<bool(int64, int64, int32&)>* LocalHeight) const
{
	const FInt64Vector BaseMin = Box.First - FInt64Vector(Box.Plan.Halo);
	const int32 InputAxis = Box.Axis + 2 * Box.Plan.Halo;
	if (const FVoxelLandError Error = GenerateM4TiledBaselineBox(BaseMin, InputAxis, Worker, Out, IsCancelled, LocalHeight))
	{
		return Error;
	}
	return ApplyM6Box(Box.First * int64(Profile.VoxelSizeCm), Box.Axis, Box.Plan, Out, IsCancelled);
}
