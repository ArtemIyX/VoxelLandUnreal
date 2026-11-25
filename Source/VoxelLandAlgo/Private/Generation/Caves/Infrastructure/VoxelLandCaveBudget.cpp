#include "Generation/Caves/Infrastructure/VoxelLandCaveBudget.h"

#include "Generation/Caves/Infrastructure/VoxelLandCaveBudgetLease.h"
#include "HAL/CriticalSection.h"
#include "Misc/ScopeLock.h"

class FVoxelLandM5BudgetState final
{
public:
	FVoxelLandM5BudgetState(int64 InMaxBytes, int64 InMaxWork)
		: MaxBytes(InMaxBytes)
		, MaxWork(InMaxWork)
	{
	}

	FCriticalSection Mutex;
	const int64 MaxBytes;
	const int64 MaxWork;
	int64 LiveBytes = 0;
	int64 PeakBytes = 0;
	int64 WorkUnits = 0;
	int32 LiveLeases = 0;
};

FVoxelLandM5BudgetLease::FVoxelLandM5BudgetLease(
	TSharedRef<FVoxelLandM5BudgetState, ESPMode::ThreadSafe> InState, int64 InBytes)
	: State(MoveTemp(InState))
	, Bytes(InBytes)
{
}

FVoxelLandM5BudgetLease::~FVoxelLandM5BudgetLease()
{
	FScopeLock Lock(&State->Mutex);
	State->LiveBytes -= Bytes;
	--State->LiveLeases;
}

FVoxelLandM5Budget::FVoxelLandM5Budget(TSharedRef<FVoxelLandM5BudgetState, ESPMode::ThreadSafe> InState)
	: State(MoveTemp(InState))
{
}

bool FVoxelLandM5Budget::TryCreate(
	int64 MaxBytes, int64 MaxWork, TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe>& OutBudget)
{
	if (MaxBytes < 0 || MaxWork < 0)
	{
		return false;
	}
	const TSharedRef<FVoxelLandM5BudgetState, ESPMode::ThreadSafe> NewState =
		MakeShared<FVoxelLandM5BudgetState, ESPMode::ThreadSafe>(MaxBytes, MaxWork);
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> NewBudget = MakeShareable(new FVoxelLandM5Budget(NewState));
	OutBudget = MoveTemp(NewBudget);
	return true;
}

bool FVoxelLandM5Budget::TryChargeWork(int64 Units)
{
	if (Units < 0)
	{
		return false;
	}
	FScopeLock Lock(&State->Mutex);
	if (Units > State->MaxWork - State->WorkUnits)
	{
		return false;
	}
	State->WorkUnits += Units;
	return true;
}

bool FVoxelLandM5Budget::TryReserveBytes(
	int64 Bytes, TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe>& OutLease)
{
	if (Bytes <= 0)
	{
		return false;
	}
	{
		FScopeLock Lock(&State->Mutex);
		if (Bytes > State->MaxBytes - State->LiveBytes || State->LiveLeases >= MaximumLiveLeases)
		{
			return false;
		}
		State->LiveBytes += Bytes;
		++State->LiveLeases;
		State->PeakBytes = FMath::Max(State->PeakBytes, State->LiveBytes);
	}
	// Assign outside the lock: replacing a prior lease may destroy it and
	// reacquire the same mutex in its destructor.
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> NewLease =
		MakeShareable(new FVoxelLandM5BudgetLease(State, Bytes));
	OutLease = MoveTemp(NewLease);
	return true;
}

bool FVoxelLandM5Budget::OwnsLease(const FVoxelLandM5BudgetLease& Lease) const
{
	return &State.Get() == &Lease.State.Get();
}

void FVoxelLandM5Budget::GetLimits(int64& MaxBytes, int64& MaxWork) const
{
	MaxBytes = State->MaxBytes;
	MaxWork = State->MaxWork;
}

void FVoxelLandM5Budget::GetUsage(int64& LiveBytes, int64& PeakBytes, int64& WorkUnits) const
{
	FScopeLock Lock(&State->Mutex);
	LiveBytes = State->LiveBytes;
	PeakBytes = State->PeakBytes;
	WorkUnits = State->WorkUnits;
}
