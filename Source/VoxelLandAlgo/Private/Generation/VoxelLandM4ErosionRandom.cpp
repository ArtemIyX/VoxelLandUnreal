#include "Generation/VoxelLandM4ErosionRandom.h"
#include "Generation/VoxelLandGenerator.h"

class FVoxelLandM4ErosionRandomInternals final
{
public:
	static constexpr int32 M4ErosionRandomMaxCells = 1024;
	static constexpr int32 M4ErosionRandomMaxDroplets = 65536;
	static constexpr int32 M4ErosionRandomMaxDraws = 128;
	static uint64 M4ErosionRandomMix(uint64 Value);
	static FVoxelLandError M4ErosionRandomError(EVoxelLandErrorCode Code, const TCHAR* Message);
	static FVoxelLandError M4ErosionRandomDraw(
		uint64 Seed, uint64& Counter, uint64 Bound, int32& Out, TFunctionRef<bool()> IsCancelled);
};

uint64 FVoxelLandM4ErosionRandomInternals::M4ErosionRandomMix(uint64 Value)
{
	Value += 0x9E3779B97F4A7C15ull;
	Value = (Value ^ (Value >> 30)) * 0xBF58476D1CE4E5B9ull;
	Value = (Value ^ (Value >> 27)) * 0x94D049BB133111EBull;
	return Value ^ (Value >> 31);
}

FVoxelLandError FVoxelLandM4ErosionRandomInternals::M4ErosionRandomError(EVoxelLandErrorCode Code, const TCHAR* Message)
{
	FVoxelLandError Error;
	Error.Code = Code;
	Error.Message = Message;
	return Error;
}

FVoxelLandError FVoxelLandM4ErosionRandomInternals::M4ErosionRandomDraw(
	uint64 Seed, uint64& Counter, uint64 Bound, int32& Out, TFunctionRef<bool()> IsCancelled)
{
	const uint64 Threshold = (uint64(0) - Bound) % Bound;
	for (int32 Attempt = 0; Attempt < M4ErosionRandomMaxDraws; ++Attempt)
	{
		if (IsCancelled())
		{
			return M4ErosionRandomError(EVoxelLandErrorCode::Cancelled, TEXT("M4 droplet sampling cancelled"));
		}
		const uint64 Value = M4ErosionRandomMix(Seed + Counter++);
		if (Value >= Threshold)
		{
			Out = int32(Value % Bound);
			return {};
		}
	}
	return M4ErosionRandomError(
		EVoxelLandErrorCode::InvalidMathDomain, TEXT("M4 droplet sampling rejection limit reached"));
}

FVoxelLandError VoxelLandM4ErosionRandom::Start(uint64 WorldSeed, const FGuid& StageId, uint32 Salt,
	uint32 DropletIndex, int32 CellsX, int32 CellsY, FIntPoint& OutStart, TFunctionRef<bool()> IsCancelled)
{
	if (!StageId.IsValid() || CellsX < 1 || CellsY < 1
		|| CellsX > FVoxelLandM4ErosionRandomInternals::M4ErosionRandomMaxCells
		|| CellsY > FVoxelLandM4ErosionRandomInternals::M4ErosionRandomMaxCells
		|| DropletIndex >= uint32(FVoxelLandM4ErosionRandomInternals::M4ErosionRandomMaxDroplets))
	{
		return FVoxelLandM4ErosionRandomInternals::M4ErosionRandomError(
			EVoxelLandErrorCode::InvalidParameter, TEXT("Invalid M4 droplet stream parameters"));
	}
	const uint64 StageSeed = VoxelLandDeterminism::DeriveNodeSeed(WorldSeed, StageId, Salt);
	const uint64 Seed = FVoxelLandM4ErosionRandomInternals::M4ErosionRandomMix(
		StageSeed ^ FVoxelLandM4ErosionRandomInternals::M4ErosionRandomMix(DropletIndex));
	uint64 Counter = 0;
	FIntPoint StartPoint;
	FVoxelLandError Error = FVoxelLandM4ErosionRandomInternals::M4ErosionRandomDraw(
		Seed, Counter, uint64(CellsX) + 1, StartPoint.X, IsCancelled);
	if (Error)
	{
		return Error;
	}
	Error = FVoxelLandM4ErosionRandomInternals::M4ErosionRandomDraw(
		Seed, Counter, uint64(CellsY) + 1, StartPoint.Y, IsCancelled);
	if (Error)
	{
		return Error;
	}
	if (IsCancelled())
	{
		return FVoxelLandM4ErosionRandomInternals::M4ErosionRandomError(
			EVoxelLandErrorCode::Cancelled, TEXT("M4 droplet sampling cancelled"));
	}
	OutStart = StartPoint;
	return {};
}

FVoxelLandError VoxelLandM4ErosionRandom::Starts(uint64 WorldSeed, const FGuid& StageId, uint32 Salt, int32 Count,
	int32 CellsX, int32 CellsY, TArray<FIntPoint>& OutStarts, TFunctionRef<bool()> IsCancelled)
{
	if (!StageId.IsValid() || Count < 0 || Count > FVoxelLandM4ErosionRandomInternals::M4ErosionRandomMaxDroplets
		|| CellsX < 1 || CellsY < 1 || CellsX > FVoxelLandM4ErosionRandomInternals::M4ErosionRandomMaxCells
		|| CellsY > FVoxelLandM4ErosionRandomInternals::M4ErosionRandomMaxCells)
	{
		return FVoxelLandM4ErosionRandomInternals::M4ErosionRandomError(
			EVoxelLandErrorCode::InvalidParameter, TEXT("Invalid M4 droplet stream parameters"));
	}
	if (IsCancelled())
	{
		return FVoxelLandM4ErosionRandomInternals::M4ErosionRandomError(
			EVoxelLandErrorCode::Cancelled, TEXT("M4 droplet sampling cancelled"));
	}
	TArray<FIntPoint> Result;
	Result.Reserve(Count);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		FIntPoint Point;
		const FVoxelLandError Error =
			Start(WorldSeed, StageId, Salt, uint32(Index), CellsX, CellsY, Point, IsCancelled);
		if (Error)
		{
			return Error;
		}
		Result.Add(Point);
	}
	if (IsCancelled())
	{
		return FVoxelLandM4ErosionRandomInternals::M4ErosionRandomError(
			EVoxelLandErrorCode::Cancelled, TEXT("M4 droplet sampling cancelled"));
	}
	OutStarts = MoveTemp(Result);
	return {};
}