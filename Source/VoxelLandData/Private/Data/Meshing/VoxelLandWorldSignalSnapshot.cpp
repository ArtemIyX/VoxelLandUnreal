#include "Data/Meshing/VoxelLandWorldSignalSnapshot.h"

bool FVoxelLandWorldSignalSnapshot::Validate(const FVoxelLandWorldSignalGrid& Grid, int64& TotalSamples, FString& Error)
{
	constexpr int64 MaxSamples = 4 * 1024 * 1024;
	const int64 Count = int64(Grid.Size.X) * Grid.Size.Y;
	const bool bIsNumeric = Grid.Type == EVoxelLandGraphValueType::Float;
	const bool bIsInteger = Grid.Type == EVoxelLandGraphValueType::Int32;
	const bool bIsBoolean = Grid.Type == EVoxelLandGraphValueType::Bool;
	if (Grid.Name.IsNone() || Grid.OriginCm.ContainsNaN() || !FMath::IsFinite(Grid.SpacingCm) || Grid.SpacingCm <= 0.0
		|| Grid.Size.X <= 0 || Grid.Size.Y <= 0 || Count != Grid.Samples.Num() || Count > MaxSamples - TotalSamples
		|| (!bIsNumeric && !bIsInteger && !bIsBoolean))
	{
		Error =
			FString::Printf(TEXT("Invalid extent, type or sample count for world signal '%s'."), *Grid.Name.ToString());
		return false;
	}
	for (double Value : Grid.Samples)
	{
		if (!FMath::IsFinite(Value)
			|| (bIsInteger && (Value < MIN_int32 || Value > MAX_int32 || FMath::FloorToDouble(Value) != Value))
			|| (bIsBoolean && Value != 0.0 && Value != 1.0))
		{
			Error = FString::Printf(TEXT("Invalid sample in world signal '%s'."), *Grid.Name.ToString());
			return false;
		}
	}
	TotalSamples += Count;
	return true;
}

bool FVoxelLandWorldSignalSnapshot::Create(
	TArray<FVoxelLandWorldSignalGrid> Grids, uint64 Revision, FPtr& Out, FString& Error)
{
	constexpr int32 MaxChannels = 32;
	Error.Reset();
	if (Grids.Num() > MaxChannels)
	{
		Error = TEXT("A world signal snapshot supports up to 32 channels.");
		return false;
	}

	TSharedRef<FVoxelLandWorldSignalSnapshot, ESPMode::ThreadSafe> Result =
		MakeShared<FVoxelLandWorldSignalSnapshot, ESPMode::ThreadSafe>();
	Result->Revision = Revision;
	Result->Channels.Reserve(Grids.Num());
	int64 TotalSamples = 0;
	for (FVoxelLandWorldSignalGrid& Grid : Grids)
	{
		const FName Name = Grid.Name;
		if (Result->Channels.Contains(Name))
		{
			Error = FString::Printf(TEXT("Duplicate world signal '%s'."), *Name.ToString());
			return false;
		}
		if (!Validate(Grid, TotalSamples, Error))
		{
			return false;
		}
		Result->Channels.Add(Name, MoveTemp(Grid));
	}
	Out = MoveTemp(Result);
	return true;
}

bool FVoxelLandWorldSignalSnapshot::Sample(FName Name, const FVector2d& LogicalXY, double& Out) const
{
	const FVoxelLandWorldSignalGrid* Grid = Channels.Find(Name);
	if (!Grid || LogicalXY.ContainsNaN())
	{
		return false;
	}
	const FVector2d Cell = (LogicalXY - Grid->OriginCm) / Grid->SpacingCm;
	const int32 LastX = Grid->Size.X - 1;
	const int32 LastY = Grid->Size.Y - 1;
	if (Cell.ContainsNaN() || Cell.X < 0.0 || Cell.X > LastX || Cell.Y < 0.0 || Cell.Y > LastY)
	{
		return false;
	}
	if (Grid->Type != EVoxelLandGraphValueType::Float)
	{
		const int32 X = FMath::Min(LastX, FMath::FloorToInt32(Cell.X + 0.5));
		const int32 Y = FMath::Min(LastY, FMath::FloorToInt32(Cell.Y + 0.5));
		Out = Grid->Samples[Y * Grid->Size.X + X];
		return true;
	}
	const int32 X = FMath::FloorToInt32(Cell.X);
	const int32 Y = FMath::FloorToInt32(Cell.Y);
	const int32 East = FMath::Min(X + 1, LastX);
	const int32 North = FMath::Min(Y + 1, LastY);
	const double SouthValue =
		FMath::Lerp(Grid->Samples[Y * Grid->Size.X + X], Grid->Samples[Y * Grid->Size.X + East], Cell.X - X);
	const double NorthValue =
		FMath::Lerp(Grid->Samples[North * Grid->Size.X + X], Grid->Samples[North * Grid->Size.X + East], Cell.X - X);
	const double Value = FMath::Lerp(SouthValue, NorthValue, Cell.Y - Y);
	if (!FMath::IsFinite(Value))
	{
		return false;
	}
	Out = Value;
	return true;
}
