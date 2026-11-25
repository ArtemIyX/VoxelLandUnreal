#include "Generation/VoxelLandGenerator.h"

#include "Data/Structs/VoxelLandCanonicalWriter.h"
#include "Generation/VoxelLandM6FilterPlan.h"
#include "Generation/VoxelLandM6OverlayValidation.h"
#include "Generation/VoxelLandM6Redistance.h"
#include "Generation/VoxelLandM6StampMath.h"

class FVoxelLandM6FilterPlanning final
{
public:
	static int64 DivideDown(int64 Value, int64 Divisor);
	static int64 DivideUp(int64 Value, int64 Divisor);
	static bool Ordered(const FInt64Vector& Min, const FInt64Vector& Max);
	static bool IsWithinMaskLimit(const FInt64Vector& Point);
	static void WritePoint(FCanonicalWriter& Writer, const FInt64Vector& Point);
	static FVoxelLandError ValidateStage(const FVoxelLandM6FilterStage& Stage);
	static bool IntersectsStoredBox(const FVoxelLandM6FilterStage& Stage, const FInt64Vector& StoredMin,
		int32 OutputHalo, int64 VoxelSizeCm);
};

int64 FVoxelLandM6FilterPlanning::DivideDown(int64 Value, int64 Divisor)
{
	return Value / Divisor - int64(Value % Divisor < 0);
}

int64 FVoxelLandM6FilterPlanning::DivideUp(int64 Value, int64 Divisor)
{
	return Value / Divisor + int64(Value % Divisor > 0);
}

bool FVoxelLandM6FilterPlanning::Ordered(const FInt64Vector& Min, const FInt64Vector& Max)
{
	return Min.X <= Max.X && Min.Y <= Max.Y && Min.Z <= Max.Z;
}

bool FVoxelLandM6FilterPlanning::IsWithinMaskLimit(const FInt64Vector& Point)
{
	constexpr int64 Limit = FVoxelLandM6StampMath::CoordinateLimitCm;
	return Ordered(FInt64Vector(-Limit), Point) && Ordered(Point, FInt64Vector(Limit));
}

void FVoxelLandM6FilterPlanning::WritePoint(FCanonicalWriter& Writer, const FInt64Vector& Point)
{
	const int64 Axes[] = {Point.X, Point.Y, Point.Z};
	for (int64 Axis : Axes)
	{
		const uint64 Bits = uint64(Axis);
		Writer.U32(uint32(Bits));
		Writer.U32(uint32(Bits >> 32));
	}
}

FVoxelLandError FVoxelLandM6FilterPlanning::ValidateStage(const FVoxelLandM6FilterStage& Stage)
{
	if (!Stage.Id.IsValid() || !Ordered(Stage.MinCm, Stage.MaxCm)
		|| !IsWithinMaskLimit(Stage.MinCm) || !IsWithinMaskLimit(Stage.MaxCm))
	{
		return {EVoxelLandErrorCode::InvalidParameter, TEXT("Invalid M6 region filter ID or mask")};
	}
	switch (Stage.Kind)
	{
	case EVoxelLandM6FilterKind::Smooth:
	case EVoxelLandM6FilterKind::Flatten:
		return {};
	case EVoxelLandM6FilterKind::MakeSpace:
	{
		const FInt64Vector Width = Stage.MaxCm - Stage.MinCm;
		constexpr int64 MaxWidth = 2 * FVoxelLandM6StampMath::MaxStampDimensionCm;
		if (Stage.Strength != MAX_uint16 || !Ordered(FInt64Vector(1), Width) || !Ordered(Width, FInt64Vector(MaxWidth)))
		{
			return {EVoxelLandErrorCode::InvalidParameter, TEXT("M6 make-space requires hard strength and positive box dimensions")};
		}
		return {};
	}
	default:
		return {EVoxelLandErrorCode::InvalidParameter, TEXT("Unsupported M6 region filter kind")};
	}
}

bool FVoxelLandM6FilterPlanning::IntersectsStoredBox(
	const FVoxelLandM6FilterStage& Stage, const FInt64Vector& StoredMin, int32 OutputHalo, int64 VoxelSizeCm)
{
	const FInt64Vector MaskMin(DivideUp(Stage.MinCm.X, VoxelSizeCm), DivideUp(Stage.MinCm.Y, VoxelSizeCm),
		DivideUp(Stage.MinCm.Z, VoxelSizeCm));
	const FInt64Vector MaskMax(DivideDown(Stage.MaxCm.X, VoxelSizeCm), DivideDown(Stage.MaxCm.Y, VoxelSizeCm),
		DivideDown(Stage.MaxCm.Z, VoxelSizeCm));
	const FInt64Vector RequiredMin = StoredMin - FInt64Vector(OutputHalo);
	const FInt64Vector RequiredMax = StoredMin + FInt64Vector(FVoxelLandChunkData::SamplesPerAxis - 1 + OutputHalo);
	return Ordered(RequiredMin.ComponentMax(MaskMin), RequiredMax.ComponentMin(MaskMax));
}

FVoxelLandError FVoxelLandM6OverlayValidation::ValidateFilters(const FVoxelLandM6FilterSettings& Settings, float VoxelSizeCm)
{
	if (Settings.Version != FVoxelLandM6FilterSettings::CurrentVersion
		|| Settings.Stages.Num() > FVoxelLandM6FilterSettings::MaxStages)
	{
		return {EVoxelLandErrorCode::InvalidParameter, TEXT("Unsupported M6 filter version or stage count")};
	}
	if (const FVoxelLandError Error = ValidateVoxelSize(VoxelSizeCm))
	{
		return Error;
	}
	int32 ConfiguredHalo = 0;
	for (int32 Index = 0; Index < Settings.Stages.Num(); ++Index)
	{
		const FVoxelLandM6FilterStage& Stage = Settings.Stages[Index];
		if (FVoxelLandError Error = FVoxelLandM6FilterPlanning::ValidateStage(Stage))
		{
			Error.Message = FString::Printf(TEXT("Filter %d: %s"), Index + 1, *Error.Message);
			return Error;
		}
		for (int32 Prior = 0; Prior < Index; ++Prior)
		{
			if (Settings.Stages[Prior].Id == Stage.Id)
			{
				return {EVoxelLandErrorCode::InvalidParameter, TEXT("Duplicate M6 region filter ID")};
			}
		}
		ConfiguredHalo += FVoxelLandM6FilterPlan::FilterRadius(Stage);
	}
	if (ConfiguredHalo > FVoxelLandM6FilterSettings::MaxHalo)
	{
		return {EVoxelLandErrorCode::InvalidParameter, TEXT("Configured M6 filters exceed the accumulated halo limit")};
	}
	return {};
}

FVoxelLandError FVoxelLandGenerator::InitializeM6Filters(const FVoxelLandM6FilterSettings& Settings, float VoxelSizeCm)
{
	if (const FVoxelLandError Error = FVoxelLandM6OverlayValidation::ValidateFilters(Settings, VoxelSizeCm))
	{
		return Error;
	}
	FCanonicalWriter Writer;
	constexpr uint32 IdentityTag = 0x4c46364d; // M6FL in little endian.
	constexpr int32 RecordBytes = 16 * sizeof(uint32) + sizeof(uint8) + sizeof(uint16);
	Writer.Bytes.Reserve(3 * sizeof(uint32) + Settings.Stages.Num() * RecordBytes);
	Writer.U32(IdentityTag);
	Writer.U32(Settings.Version);
	Writer.U32(uint32(Settings.Stages.Num()));
	for (const FVoxelLandM6FilterStage& Stage : Settings.Stages)
	{
		Writer.U32(Stage.Id.A);
		Writer.U32(Stage.Id.B);
		Writer.U32(Stage.Id.C);
		Writer.U32(Stage.Id.D);
		Writer.U8(uint8(Stage.Kind));
		FVoxelLandM6FilterPlanning::WritePoint(Writer, Stage.MinCm);
		FVoxelLandM6FilterPlanning::WritePoint(Writer, Stage.MaxCm);
		Writer.U16(Stage.Strength);
	}
	M6FilterIdentity = MoveTemp(Writer.Bytes);
	M6Filters.Append(Settings.Stages.GetData(), Settings.Stages.Num());
	return {};
}

FVoxelLandError FVoxelLandM6OverlayValidation::ValidateRedistance(const FVoxelLandM6RedistanceSettings& Settings,
	TConstArrayView<FVoxelLandM6FilterStage> Filters, float VoxelSizeCm)
{
	if (Settings.Version != FVoxelLandM6RedistanceSettings::CurrentVersion || Settings.BandVoxels < 1
		|| Settings.BandVoxels > FVoxelLandM6RedistanceSettings::MaxBandVoxels)
	{
		return {EVoxelLandErrorCode::InvalidParameter, TEXT("Unsupported M6 re-distance version or band")};
	}
	if (const FVoxelLandError Error = ValidateVoxelSize(VoxelSizeCm))
	{
		return Error;
	}

	int32 ConfiguredHalo = FVoxelLandM6Redistance::HaloForBand(Settings.BandVoxels);
	for (const FVoxelLandM6FilterStage& Stage : Filters)
	{
		ConfiguredHalo += FVoxelLandM6FilterPlan::FilterRadius(Stage);
	}
	if (ConfiguredHalo > FVoxelLandM6FilterSettings::MaxHalo)
	{
		return {EVoxelLandErrorCode::InvalidParameter, TEXT("M6 filters plus the re-distance endpoint halo exceed 8 voxels")};
	}
	return {};
}

FVoxelLandError FVoxelLandGenerator::InitializeM6Redistance(const FVoxelLandM6RedistanceSettings& Settings, float VoxelSizeCm)
{
	if (const FVoxelLandError Error = FVoxelLandM6OverlayValidation::ValidateRedistance(Settings, M6Filters, VoxelSizeCm))
	{
		return Error;
	}
	FCanonicalWriter Writer;
	constexpr uint32 IdentityTag = 0x4452364d; // M6RD in little endian.
	Writer.U32(IdentityTag);
	Writer.U32(Settings.Version);
	Writer.U8(Settings.BandVoxels);
	M6RedistanceIdentity = MoveTemp(Writer.Bytes);
	M6RedistanceBand = Settings.BandVoxels;
	return {};
}

FVoxelLandM6FilterPlan FVoxelLandGenerator::MakeM6FilterPlan(const FVoxelLandChunkKey& Key) const
{
	static_assert(FVoxelLandM6FilterSettings::MaxStages <= 8);
	const FInt64Vector StoredMin = FInt64Vector(Key.Coordinate) * int64(FVoxelLandChunkData::Cells) - FInt64Vector(1);
	const int64 VoxelSizeCm = int64(Profile.VoxelSizeCm);
	FVoxelLandM6FilterPlan Plan;
	Plan.RedistanceBand = M6RedistanceBand;
	Plan.Halo = FVoxelLandM6Redistance::HaloForBand(Plan.RedistanceBand);
	for (int32 Index = M6Filters.Num() - 1; Index >= 0; --Index)
	{
		const FVoxelLandM6FilterStage& Stage = M6Filters[Index];
		if (Stage.Strength == 0 || !FVoxelLandM6FilterPlanning::IntersectsStoredBox(Stage, StoredMin, Plan.Halo, VoxelSizeCm))
		{
			continue;
		}
		Plan.ActiveStages |= uint8(1 << Index);
		Plan.OutputHalo[Index] = uint8(Plan.Halo);
		Plan.Halo += FVoxelLandM6FilterPlan::FilterRadius(Stage);
	}
	return Plan;
}

FVoxelLandError FVoxelLandGenerator::ValidateM6FilterDemand(const FVoxelLandChunkKey& Key, const FVoxelLandM6FilterPlan& Plan) const
{
	constexpr int64 MaxWorkUnits = 64000000;
	constexpr int64 MaxLogicalScratchBytes = 2 * 1024 * 1024;
	constexpr int64 StoredAxis = FVoxelLandChunkData::SamplesPerAxis;
	const int64 BaseAxis = StoredAxis + 2 * Plan.Halo;
	const int64 BaseSamples = BaseAxis * BaseAxis * BaseAxis;
	// A baseline at this size uses at most two tiles per axis. Include both
	// compatibility halos on each tile and both slope rows/columns per tile.
	const int64 RawAxis = BaseAxis + 4 * GetM6CompatibilityHalo();
	const int64 HeightAxis = RawAxis + 4;
	const int64 RedistanceChecks =
		int64(FVoxelLandM6Redistance::EdgeChecksPerSample(Plan.RedistanceBand)) * FVoxelLandChunkData::SampleCount;
	int64 Work = RawAxis * RawAxis * RawAxis * 8 + HeightAxis * HeightAxis * 8
		+ BaseSamples * (M6Stamps.Num() + 30) + RedistanceChecks;
	for (int32 Index = 0; Index < M6Filters.Num(); ++Index)
	{
		if (Plan.HasStage(Index))
		{
			const int64 OutputAxis = StoredAxis + 2 * Plan.OutputHalo[Index];
			Work += OutputAxis * OutputAxis * OutputAxis * 32;
		}
	}

	constexpr int64 RawTileAxis = StoredAxis + 2;
	constexpr int64 RawTileSamples = RawTileAxis * RawTileAxis * RawTileAxis;
	const int64 ScratchBytes = 2 * BaseSamples * sizeof(int16) + RawTileSamples * sizeof(int16)
		+ HeightAxis * HeightAxis * 12 * sizeof(int32)
		+ FVoxelLandChunkData::SampleCount * (sizeof(int16) + 2 * sizeof(uint8));
	if (Work > MaxWorkUnits || ScratchBytes > MaxLogicalScratchBytes)
	{
		return {EVoxelLandErrorCode::InvalidParameter, TEXT("M6 pipeline work or scratch budget exceeded")};
	}
	return ValidateM6BaselineFootprint(Key, Plan.Halo);
}
