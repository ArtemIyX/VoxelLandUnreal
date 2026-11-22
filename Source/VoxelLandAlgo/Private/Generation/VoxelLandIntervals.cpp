#include "Generation/VoxelLandGenerator.h"
#include "Data/Structs/VoxelLandDensityInterval.h"
#include <cfloat>
#include <cmath>
#include <limits>

class FVoxelLandIntervalsInternals final
{
public:
	static FVoxelLandDensityInterval Known(float Minimum, float Maximum);
	static FVoxelLandDensityInterval Rounded(double Minimum, double Maximum);
	static FVoxelLandDensityInterval Coordinates(int32 Coordinate, float VoxelSize, int32 Halo);
	static FVoxelLandDensityInterval Unary(EVoxelLandOpcode Op, const FVoxelLandDensityInterval& A);
	static FVoxelLandDensityInterval Binary(
		EVoxelLandOpcode Op, const FVoxelLandDensityInterval& A, const FVoxelLandDensityInterval& B);
};

FVoxelLandDensityInterval FVoxelLandIntervalsInternals::Known(float Minimum, float Maximum)
{
	FVoxelLandDensityInterval Result;
	if (FMath::IsFinite(Minimum) && FMath::IsFinite(Maximum) && Minimum <= Maximum &&
	    (Minimum == 0.f || FMath::Abs(Minimum) >= FLT_MIN) && (Maximum == 0.f || FMath::Abs(Maximum) >= FLT_MIN))
	{
		Result.Minimum = Minimum;
		Result.Maximum = Maximum;
		Result.bKnown = true;
	}
	return Result;
}

FVoxelLandDensityInterval FVoxelLandIntervalsInternals::Rounded(double Minimum, double Maximum)
{
	if (!FMath::IsFinite(Minimum) || !FMath::IsFinite(Maximum) || Minimum < -FLT_MAX || Maximum > FLT_MAX)
	{
		return {};
	}
	const float Down = std::nextafterf(float(Minimum), -std::numeric_limits<float>::infinity());
	const float Up = std::nextafterf(float(Maximum), std::numeric_limits<float>::infinity());
	return Known(Down, Up);
}

FVoxelLandDensityInterval FVoxelLandIntervalsInternals::Coordinates(int32 Coordinate, float VoxelSize, int32 Halo)
{
	const double Minimum = double(int64(Coordinate) * 32 - 1 - Halo) * double(VoxelSize);
	const double Maximum = double(int64(Coordinate) * 32 + 33 + Halo) * double(VoxelSize);
	return Rounded(Minimum, Maximum);
}

FVoxelLandDensityInterval FVoxelLandIntervalsInternals::Unary(EVoxelLandOpcode Op, const FVoxelLandDensityInterval& A)
{
	if (!A.bKnown) { return {}; }
	if (Op == EVoxelLandOpcode::Negate) { return Known(-A.Maximum, -A.Minimum); }
	if (Op == EVoxelLandOpcode::Abs)
	{
		return Known(A.Minimum <= 0.f && A.Maximum >= 0.f
			             ? 0.f
			             : FMath::Min(FMath::Abs(A.Minimum), FMath::Abs(A.Maximum)),
			FMath::Max(FMath::Abs(A.Minimum), FMath::Abs(A.Maximum)));
	}
	return {};
}

FVoxelLandDensityInterval FVoxelLandIntervalsInternals::Binary(
	EVoxelLandOpcode Op, const FVoxelLandDensityInterval& A, const FVoxelLandDensityInterval& B)
{
	if (!A.bKnown || !B.bKnown) { return {}; }
	switch (Op)
	{
		case EVoxelLandOpcode::Add:
			return Rounded(double(A.Minimum) + B.Minimum, double(A.Maximum) + B.Maximum);
		case EVoxelLandOpcode::Subtract:
			return Rounded(double(A.Minimum) - B.Maximum, double(A.Maximum) - B.Minimum);
		case EVoxelLandOpcode::Min:
		case EVoxelLandOpcode::Union:
			return Known(FMath::Min(A.Minimum, B.Minimum), FMath::Min(A.Maximum, B.Maximum));
		case EVoxelLandOpcode::Max:
		case EVoxelLandOpcode::Intersect:
			return Known(FMath::Max(A.Minimum, B.Minimum), FMath::Max(A.Maximum, B.Maximum));
		case EVoxelLandOpcode::Multiply:
		{
			const double P[] = {double(A.Minimum) * B.Minimum, double(A.Minimum) * B.Maximum,
			                    double(A.Maximum) * B.Minimum, double(A.Maximum) * B.Maximum};
			return Rounded(FMath::Min(FMath::Min(P[0], P[1]), FMath::Min(P[2], P[3])),
				FMath::Max(FMath::Max(P[0], P[1]), FMath::Max(P[2], P[3])));
		}
		default:
			return {};
	}
}

EVoxelLandChunkClassification FVoxelLandGenerator::ClassifyChunkBeforeSampling(const FVoxelLandChunkKey& Key) const
{
	if (M4HeightGrid || M4Unlimited)
	{
		return EVoxelLandChunkClassification::NeedsSamples;
	}
	const FVoxelLandProgramDraft& Draft = Program->GetData();
	if (Key.LOD != 0 || Draft.ExternalFilterIndex != INDEX_NONE || Profile.VoxelSizeCm < 1.e-12f)
	{
		return EVoxelLandChunkClassification::NeedsSamples;
	}
	const int32 Halo = bSmoothEnabled && SmoothStrength > 0.f ? 1 : 0;
	const FVoxelLandDensityInterval X =
		FVoxelLandIntervalsInternals::Coordinates(Key.Coordinate.X, Profile.VoxelSizeCm, Halo);
	const FVoxelLandDensityInterval Y =
		FVoxelLandIntervalsInternals::Coordinates(Key.Coordinate.Y, Profile.VoxelSizeCm, Halo);
	const FVoxelLandDensityInterval Z =
		FVoxelLandIntervalsInternals::Coordinates(Key.Coordinate.Z, Profile.VoxelSizeCm, Halo);
	TArray<FVoxelLandDensityInterval> Registers;
	Registers.SetNum(Draft.RegisterCount);
	for (const FVoxelLandInstruction& I : Draft.Instructions)
	{
		if (I.Op == EVoxelLandOpcode::StoreDensity)
		{
			const FVoxelLandDensityInterval& Bound = Registers[I.A];
			if (Bound.bKnown && Bound.Minimum >= 0.f) { return EVoxelLandChunkClassification::Empty; }
			if (Bound.bKnown && Bound.Maximum <= -Profile.VoxelSizeCm) { return EVoxelLandChunkClassification::Solid; }
			return EVoxelLandChunkClassification::NeedsSamples;
		}
		FVoxelLandDensityInterval Value;
		switch (I.Op)
		{
			case EVoxelLandOpcode::LoadX:
				Value = X;
				break;
			case EVoxelLandOpcode::LoadY:
				Value = Y;
				break;
			case EVoxelLandOpcode::LoadZ:
				Value = Z;
				break;
			case EVoxelLandOpcode::LoadConstant:
				Value = FVoxelLandIntervalsInternals::Known(Draft.Constants[I.Imm], Draft.Constants[I.Imm]);
				break;
			case EVoxelLandOpcode::LoadFloatParameter:
				Value = FVoxelLandIntervalsInternals::Known(FloatParameters[I.Imm], FloatParameters[I.Imm]);
				break;
			case EVoxelLandOpcode::Negate:
			case EVoxelLandOpcode::Abs:
				Value = FVoxelLandIntervalsInternals::Unary(I.Op, Registers[I.A]);
				break;
			case EVoxelLandOpcode::Add:
			case EVoxelLandOpcode::Subtract:
			case EVoxelLandOpcode::Multiply:
			case EVoxelLandOpcode::Min:
			case EVoxelLandOpcode::Max:
			case EVoxelLandOpcode::Union:
			case EVoxelLandOpcode::Intersect:
				Value = FVoxelLandIntervalsInternals::Binary(I.Op, Registers[I.A], Registers[I.B]);
				break;
			default:
				break;
		}
		Registers[I.Dst] = Value;
	}
	return EVoxelLandChunkClassification::NeedsSamples;
}