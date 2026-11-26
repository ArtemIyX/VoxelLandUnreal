#include "VoxelLandLocalTerrainHeightSource.h"

#include "Data/Terrain/ChunkLocal/VoxelLandChunkLocalTerrainConfig.h"
#include "Generation/Field/VoxelLandGenerator.h"
#include "Generation/Terrain/Curves/VoxelLandCurveLut.h"
#include "../../Terrain/ChunkLocal/VoxelLandFixedLatticeSampling.h"

namespace
{
int64 EstimateWork(const FVoxelLandGenerator& Source)
{
	int64 Work = 0;
	for (const FVoxelLandInstruction& Instruction : Source.GetProgram().GetData().Instructions)
	{
		switch (Instruction.Op)
		{
			case EVoxelLandOpcode::Fbm2:
				Work += 32;
				break;
			case EVoxelLandOpcode::Value2:
				Work += 8;
				break;
			default:
				++Work;
				break;
		}
	}
	return Work;
}
}

FVoxelLandM7LocalHeightSource::FVoxelLandM7LocalHeightSource(const FVoxelLandM4UnlimitedConfig* InConfig)
	: Config(InConfig)
{
	if (!Config)
	{
		return;
	}
	Heights.Reserve(1024);
	if (Config->AuthoredHeightGenerator)
	{
		CostPerHeight = EstimateWork(*Config->AuthoredHeightGenerator);
		return;
	}

	if (!Config->ContinentalGenerator || !Config->MountainGenerator || !Config->PeaksGenerator
		|| !Config->ContinentalCurve || !Config->MountainCurve || !Config->PeaksCurve)
	{
		Error = {EVoxelLandErrorCode::InvalidParameter,
			TEXT("M7 local terrain has no complete fixed-lattice height source")};
		return;
	}
	CostPerHeight = EstimateWork(*Config->ContinentalGenerator) + EstimateWork(*Config->MountainGenerator)
				  + EstimateWork(*Config->PeaksGenerator);
}

bool FVoxelLandM7LocalHeightSource::operator()(int64 X, int64 Y, int32& Height)
{
	if (!Config || Error)
	{
		return false;
	}
	const FInt64Vector Key(X, Y, 0);
	if (const int32* Existing = Heights.Find(Key))
	{
		Height = *Existing;
		return true;
	}
	if (RemainingWork < CostPerHeight)
	{
		Error = {EVoxelLandErrorCode::InvalidParameter, TEXT("M7 local fixed-lattice height work budget exceeded")};
		return false;
	}
	RemainingWork -= CostPerHeight;
	int32 Sample = 0;
	if (Config->AuthoredHeightGenerator)
	{
		Error = VoxelLandM4FixedLattice::EvaluateValidatedHeightPoint(
			*Config->AuthoredHeightGenerator, X, Y, Config->BaseQ8, Config->HeightScaleCm, Registers[0], Sample);
	}
	else
	{
		Error = SampleLegacyControls(X, Y, Sample);
	}
	if (Error)
	{
		return false;
	}
	if (Heights.Num() < MaxCacheEntries)
	{
		Heights.Add(Key, Sample);
	}
	Height = Sample;
	return true;
}

FVoxelLandError FVoxelLandM7LocalHeightSource::SampleLegacyControls(int64 X, int64 Y, int32& Height)
{
	const FVoxelLandGenerator* Controls[] = {
		Config->ContinentalGenerator.Get(), Config->MountainGenerator.Get(), Config->PeaksGenerator.Get()};
	const FVoxelLandM4CurveLut* Curves[] = {
		Config->ContinentalCurve.Get(), Config->MountainCurve.Get(), Config->PeaksCurve.Get()};
	int64 Result = Config->BaseQ8;
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Controls); ++Index)
	{
		uint16 Value = 0;
		if (const FVoxelLandError Failure =
				VoxelLandM4FixedLattice::EvaluateValidatedPoint(*Controls[Index], X, Y, Registers[Index], Value))
		{
			return Failure;
		}
		Result += Curves[Index]->Evaluate(Value);
	}
	constexpr int64 MaxHeightQ8 = 512000000;
	if (Result < -MaxHeightQ8 || Result > MaxHeightQ8)
	{
		return {EVoxelLandErrorCode::InvalidMathDomain, TEXT("M4 base height exceeds Q8 range")};
	}
	Height = int32(Result);
	return {};
}

FVoxelLandError FVoxelLandM7LocalHeightSource::PreferSourceError(const FVoxelLandError& Fallback) const
{
	return Error ? Error : Fallback;
}
