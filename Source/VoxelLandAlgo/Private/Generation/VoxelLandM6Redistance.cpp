#include "Generation/VoxelLandM6Redistance.h"

#include "Data/Structs/VoxelLandChunkData.h"
#include "Data/Structs/VoxelLandM6RedistanceSettings.h"
#include "Generation/VoxelLandQuantizedDensity.h"

int32 FVoxelLandM6Redistance::NearestDistance(int32 Squared, int32 BandQ)
{
	int32 Low = 0;
	int32 High = BandQ + 1;
	while (High - Low > 1)
	{
		const int32 Mid = Low + (High - Low) / 2;
		if (Mid * Mid <= Squared)
		{
			Low = Mid;
		}
		else
		{
			High = Mid;
		}
	}
	// An integer squared distance crosses (Low + 0.5)^2 when its
	// excess over Low^2 is greater than Low.
	return Low + int32(Squared - Low * Low > Low);
}

int16 FVoxelLandM6Redistance::ProcessSample(TConstArrayView<int16> Input, int32 CenterIndex, int32 InputAxis, int32 Band)
{
	const int16 Original = Input[CenterIndex];
	if (Original == 0)
	{
		return 0;
	}

	const int32 BandQ = Band * UnitsPerVoxel;
	const int32 BandSquared = BandQ * BandQ;
	int32 BestSquared = BandSquared + 1;
	const int32 Strides[] = {1, InputAxis, InputAxis * InputAxis};
	constexpr int32 Axes = 3;
	for (int32 Axis = 0; Axis < Axes; ++Axis)
	{
		const int32 Along = Strides[Axis];
		const int32 AcrossU = Strides[(Axis + 1) % Axes];
		const int32 AcrossV = Strides[(Axis + 2) % Axes];
		for (int32 V = -Band; V <= Band; ++V)
		{
			const int32 VQ = V * UnitsPerVoxel;
			for (int32 U = -Band; U <= Band; ++U)
			{
				const int32 UQ = U * UnitsPerVoxel;
				const int32 PerpendicularSquared = UQ * UQ + VQ * VQ;
				const int32 Start = CenterIndex + U * AcrossU + V * AcrossV;
				// Include the edge ending exactly on the negative band limit
				// as well as the one starting on its positive limit.
				for (int32 P = -Band - 1; P <= Band; ++P)
				{
					const int32 LowerIndex = Start + P * Along;
					const int32 Lower = Input[LowerIndex];
					const int32 Upper = Input[LowerIndex + Along];
					if ((Lower < 0) == (Upper < 0))
					{
						continue;
					}
					const int32 Fraction = FVoxelLandQuantizedDensity::RoundRatioAwayFromZero(
						int64(FMath::Abs(Lower)) * UnitsPerVoxel, FMath::Abs(Upper - Lower));
					const int32 AlongQ = P * UnitsPerVoxel + Fraction;
					const int32 Squared = PerpendicularSquared + AlongQ * AlongQ;
					if (Squared < BestSquared)
					{
						BestSquared = Squared;
					}
				}
			}
		}
	}

	if (BestSquared > BandSquared)
	{
		return Original;
	}
	const int32 Magnitude = FMath::Max(1, NearestDistance(BestSquared, BandQ));
	return int16(Original < 0 ? -Magnitude : Magnitude);
}

FVoxelLandError FVoxelLandM6Redistance::Apply(TConstArrayView<int16> Input, int32 InputAxis, int32 Band,
	TArray<int16>& Output, TFunctionRef<bool()> IsCancelled)
{
	constexpr int32 StoredAxis = FVoxelLandChunkData::SamplesPerAxis;
	constexpr int32 MaxBand = FVoxelLandM6RedistanceSettings::MaxBandVoxels;
	constexpr int32 MaxOffsetQ = (MaxBand + 1) * UnitsPerVoxel;
	static_assert(MaxOffsetQ <= (MAX_int32 / 3) / MaxOffsetQ);
	static_assert(EdgeChecksPerSample(MaxBand) * FVoxelLandChunkData::SampleCount == 19293750);
	const int32 Halo = HaloForBand(Band);
	check(Band > 0 && Band <= MaxBand && InputAxis == StoredAxis + 2 * Halo);
	check(Input.Num() == InputAxis * InputAxis * InputAxis);
	Output.SetNumUninitialized(FVoxelLandChunkData::SampleCount);
	int32 Destination = 0;
	for (int32 Z = 0; Z < StoredAxis; ++Z)
	{
		for (int32 Y = 0; Y < StoredAxis; ++Y)
		{
			if (IsCancelled())
			{
				return {EVoxelLandErrorCode::Cancelled, TEXT("M6 re-distance cancelled")};
			}
			const int32 FirstCenter = Halo + InputAxis * ((Y + Halo) + InputAxis * (Z + Halo));
			for (int32 X = 0; X < StoredAxis; ++X)
			{
				Output[Destination++] = ProcessSample(Input, FirstCenter + X, InputAxis, Band);
			}
		}
	}
	return {};
}
