#include "VoxelLandClipmapInternals.h"

#include "Data/Field/Chunks/VoxelLandChunkData.h"
#include "Data/Meshing/VoxelLandMeshData.h"
#include "Generation/Clipmap/Planning/VoxelLandClipmap.h"

bool FVoxelLandM7ClipmapInternals::ValidateSettings(const FVoxelLandM7ClipmapSettings& Settings)
{
	float LevelSpacingCm = 0.f;
	return Settings.MaxLod <= MaxPlannerLod
		&& VoxelLandMeshCoordinates::TryGetLevelSpacing(Settings.BaseVoxelSizeCm, Settings.MaxLod, LevelSpacingCm)
		&& LevelSpacingCm > 0.f && FMath::IsFinite(Settings.ViewCm.X) && FMath::IsFinite(Settings.ViewCm.Y)
		&& FMath::IsFinite(Settings.ViewCm.Z) && FMath::IsFinite(Settings.HorizontalRadiusCm)
		&& Settings.HorizontalRadiusCm > 0.0 && FMath::IsFinite(Settings.VerticalRadiusCm)
		&& Settings.VerticalRadiusCm > 0.0 && FMath::IsFinite(Settings.FirstSplitDistanceCm)
		&& Settings.FirstSplitDistanceCm > 0.0 && Settings.FirstSplitDistanceCm <= Settings.HorizontalRadiusCm
		&& FMath::IsFinite(Settings.ExitHysteresis) && Settings.ExitHysteresis >= 0.0 && Settings.ExitHysteresis <= 1.0
		&& Settings.MaxLeaves > 0 && Settings.MaxLeaves <= FVoxelLandM7Clipmap::MaxPlannerLeaves
		&& Settings.FallbackReserveLeaves >= 0
		&& Settings.FallbackReserveLeaves <= FVoxelLandM7Clipmap::MaxPlannerLeaves;
}

bool FVoxelLandM7ClipmapInternals::AreSettingsCompatible(
	const FVoxelLandM7ClipmapSettings& A, const FVoxelLandM7ClipmapSettings& B)
{
	return A.BaseVoxelSizeCm == B.BaseVoxelSizeCm && A.MaxLod == B.MaxLod
		&& A.HorizontalRadiusCm == B.HorizontalRadiusCm && A.VerticalRadiusCm == B.VerticalRadiusCm
		&& A.FirstSplitDistanceCm == B.FirstSplitDistanceCm && A.ExitHysteresis == B.ExitHysteresis
		&& A.MaxLeaves == B.MaxLeaves && A.FallbackReserveLeaves == B.FallbackReserveLeaves;
}

bool FVoxelLandM7ClipmapInternals::AddRootAxis(double Low, double High, double Width, uint8 Lod, int64& Lo, int64& Hi)
{
	const double First = FMath::FloorToDouble(Low / Width);
	const double End = FMath::CeilToDouble(High / Width);
	const int64 Stride = BaseChunkStride(Lod);
	const int64 MinRoot = int64(MIN_int32) / Stride;
	const int64 MaxRootEnd = (int64(MAX_int32) + 1) / Stride;
	if (!FMath::IsFinite(First) || !FMath::IsFinite(End) || First < double(MinRoot) || End > double(MaxRootEnd)
		|| End <= First)
	{
		return false;
	}
	Lo = int64(First);
	Hi = int64(End);
	return true;
}

bool FVoxelLandM7ClipmapInternals::MakeRoots(const FVoxelLandM7ClipmapSettings& Settings, TArray<FKey>& Roots)
{
	const double Width =
		double(Settings.BaseVoxelSizeCm) * FVoxelLandChunkData::Cells * BaseChunkStride(Settings.MaxLod);
	FInt64Vector Lo = FInt64Vector::ZeroValue;
	FInt64Vector Hi = FInt64Vector::ZeroValue;
	int32 Count = 1;
	for (int32 Axis = 0; Axis < AxisCount; ++Axis)
	{
		const double Radius = Axis == 2 ? Settings.VerticalRadiusCm : Settings.HorizontalRadiusCm;
		if (!AddRootAxis(Settings.ViewCm[Axis] - Radius, Settings.ViewCm[Axis] + Radius, Width, Settings.MaxLod,
				Lo[Axis], Hi[Axis]))
		{
			return false;
		}
		const int64 Extent = Hi[Axis] - Lo[Axis];
		if (Extent > Settings.MaxLeaves / Count)
		{
			return false;
		}
		Count *= int32(Extent);
	}

	Roots.Reserve(Count);
	for (int64 Z = Lo.Z; Z < Hi.Z; ++Z)
	{
		for (int64 Y = Lo.Y; Y < Hi.Y; ++Y)
		{
			for (int64 X = Lo.X; X < Hi.X; ++X)
			{
				Roots.Add({FIntVector(int32(X), int32(Y), int32(Z)), Settings.MaxLod});
			}
		}
	}
	return true;
}

bool FVoxelLandM7ClipmapInternals::ConsumeWork(int32 Units, int32& Remaining)
{
	if (Remaining < Units)
	{
		Remaining = -1;
		return false;
	}
	Remaining -= Units;
	return true;
}

int64 FVoxelLandM7ClipmapInternals::FloorDivide(int64 Value, int64 PositiveDivisor)
{
	return Value / PositiveDivisor - int64(Value % PositiveDivisor < 0);
}

int64 FVoxelLandM7ClipmapInternals::BaseChunkStride(uint8 Lod)
{
	return int64(1) << Lod;
}

FInt64Vector FVoxelLandM7ClipmapInternals::BaseChunkMinimum(const FKey& Key)
{
	const int64 Stride = BaseChunkStride(Key.LOD);
	return FInt64Vector(
		int64(Key.Coordinate.X) * Stride, int64(Key.Coordinate.Y) * Stride, int64(Key.Coordinate.Z) * Stride);
}

bool FVoxelLandM7ClipmapInternals::TryKeyContaining(const FInt64Vector& BaseChunk, uint8 Lod, FKey& OutKey)
{
	const int64 Stride = BaseChunkStride(Lod);
	const FInt64Vector Coordinate(
		FloorDivide(BaseChunk.X, Stride), FloorDivide(BaseChunk.Y, Stride), FloorDivide(BaseChunk.Z, Stride));
	for (int32 Axis = 0; Axis < AxisCount; ++Axis)
	{
		if (Coordinate[Axis] < MIN_int32 || Coordinate[Axis] > MAX_int32)
		{
			return false;
		}
	}
	OutKey = {FIntVector(int32(Coordinate.X), int32(Coordinate.Y), int32(Coordinate.Z)), Lod};
	return true;
}

FInt64Vector FVoxelLandM7ClipmapInternals::FaceProbe(const FKey& Key, int32 Axis, int32 Sign)
{
	FInt64Vector Probe = BaseChunkMinimum(Key);
	Probe[Axis] += Sign < 0 ? -1 : BaseChunkStride(Key.LOD);
	return Probe;
}

FVoxelLandM7ClipmapInternals::FKey FVoxelLandM7ClipmapInternals::Child(const FKey& Parent, int32 ChildIndex)
{
	const FIntVector Offset(ChildIndex & 1, (ChildIndex >> 1) & 1, ChildIndex >> 2);
	return {Parent.Coordinate * 2 + Offset, uint8(Parent.LOD - 1)};
}

double FVoxelLandM7ClipmapInternals::SquaredDistanceCm(const FKey& Leaf, const FVoxelLandM7ClipmapSettings& Settings)
{
	const FInt64Vector Min = BaseChunkMinimum(Leaf);
	const int64 Stride = BaseChunkStride(Leaf.LOD);
	const double BaseWidth = double(Settings.BaseVoxelSizeCm) * FVoxelLandChunkData::Cells;
	double Squared = 0.0;
	for (int32 Axis = 0; Axis < AxisCount; ++Axis)
	{
		const double Low = double(Min[Axis]) * BaseWidth;
		const double High = double(Min[Axis] + Stride) * BaseWidth;
		const double Offset = FMath::Max(0.0, FMath::Max(Low - Settings.ViewCm[Axis], Settings.ViewCm[Axis] - High));
		Squared += FMath::Square(Offset);
	}
	return Squared;
}

double FVoxelLandM7ClipmapInternals::SquaredSplitDistanceCm(
	const FKey& Leaf, const FVoxelLandM7ClipmapSettings& Settings, bool bUseExit)
{
	const double Hysteresis = bUseExit ? 1.0 + Settings.ExitHysteresis : 1.0;
	const double Threshold = Settings.FirstSplitDistanceCm * double(BaseChunkStride(Leaf.LOD - 1)) * Hysteresis;
	return FMath::Square(Threshold);
}

FVoxelLandM7ClipmapInternals::FPriority FVoxelLandM7ClipmapInternals::MakePriority(
	const FKey& Key, const FVoxelLandM7ClipmapSettings& Settings)
{
	return {SquaredDistanceCm(Key, Settings), Key};
}

bool FVoxelLandM7ClipmapInternals::KeyLess(const FKey& A, const FKey& B)
{
	if (A.LOD != B.LOD)
	{
		return A.LOD > B.LOD;
	}
	if (A.Coordinate.X != B.Coordinate.X)
	{
		return A.Coordinate.X < B.Coordinate.X;
	}
	if (A.Coordinate.Y != B.Coordinate.Y)
	{
		return A.Coordinate.Y < B.Coordinate.Y;
	}
	return A.Coordinate.Z < B.Coordinate.Z;
}

bool FVoxelLandM7ClipmapInternals::IsHigherPriority(const FPriority& A, const FPriority& B)
{
	return A.Key == B.Key ? KeyLess(A.Value, B.Value) : A.Key < B.Key;
}
