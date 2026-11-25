#include "Generation/VoxelLandM7LocalHeightSource.h"

#include "Data/Structs/VoxelLandM4UnlimitedConfig.h"
#include "Generation/VoxelLandGenerator.h"
#include "Generation/VoxelLandM4CurveLut.h"
#include "Generation/VoxelLandM4FixedLatticeSampling.h"

FVoxelLandM7LocalHeightSource::FVoxelLandM7LocalHeightSource(const FVoxelLandM4UnlimitedConfig* InConfig)
	: Config(InConfig)
{
	if (!Config)
	{
		return;
	}
	Heights.Reserve(1024);
	const FVoxelLandGenerator* Controls[] = {
		Config->ContinentalGenerator.Get(), Config->MountainGenerator.Get(), Config->PeaksGenerator.Get()};
	for (const FVoxelLandGenerator* Control : Controls)
	{
		check(Control);
		for (const FVoxelLandInstruction& Instruction : Control->GetProgram().GetData().Instructions)
		{
			switch (Instruction.Op)
			{
				case EVoxelLandOpcode::Fbm2:
					CostPerHeight += 32;
					break;
				case EVoxelLandOpcode::Value2:
					CostPerHeight += 8;
					break;
				default:
					++CostPerHeight;
					break;
			}
		}
	}
}

bool FVoxelLandM7LocalHeightSource::operator()(int64 X, int64 Y, int32& Height)
{
	check(Config);
	const FInt64Vector Key(X, Y, 0);
	if (const int32* Existing = Heights.Find(Key))
	{
		Height = *Existing;
		return true;
	}
	if (RemainingWork < CostPerHeight)
	{
		Error = {EVoxelLandErrorCode::InvalidParameter, TEXT("M7 local fixed-lattice control work budget exceeded")};
		return false;
	}
	RemainingWork -= CostPerHeight;
	const FVoxelLandGenerator* Controls[] = {
		Config->ContinentalGenerator.Get(), Config->MountainGenerator.Get(), Config->PeaksGenerator.Get()};
	const FVoxelLandM4CurveLut* Curves[] = {
		Config->ContinentalCurve.Get(), Config->MountainCurve.Get(), Config->PeaksCurve.Get()};
	int64 Result = Config->BaseQ8;
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Controls); ++Index)
	{
		uint16 Value = 0;
		Error = VoxelLandM4FixedLattice::EvaluateValidatedPoint(*Controls[Index], X, Y, Registers[Index], Value);
		if (Error)
		{
			return false;
		}
		Result += Curves[Index]->Evaluate(Value);
	}
	constexpr int64 MaxHeightQ8 = 512000000;
	if (Result < -MaxHeightQ8 || Result > MaxHeightQ8)
	{
		Error = {EVoxelLandErrorCode::InvalidMathDomain, TEXT("M4 base height exceeds Q8 range")};
		return false;
	}
	Height = int32(Result);
	if (Heights.Num() < MaxCacheEntries)
	{
		Heights.Add(Key, Height);
	}
	return true;
}

FVoxelLandError FVoxelLandM7LocalHeightSource::PreferSourceError(const FVoxelLandError& Fallback) const
{
	return Error ? Error : Fallback;
}
