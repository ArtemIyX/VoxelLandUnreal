#include "World/Caves/Persistence/VoxelLandCavePublicationGuard.h"

#include "Data/Caves/Planning/VoxelLandCavePlan.h"
#include "Misc/ScopeLock.h"

class FVoxelLandM5PublicationState final
{
public:
	FCriticalSection Mutex;
	uint64 Epoch = 0;
	int32 Active = 0;
	int32 MaxActive = 0;
	int32 MaxKeyBytes = 0;
	bool bClosed = false;
	TArray<uint8> CurrentKey;
	FVoxelLandM5PublicationGuard::FPlan Published;
};

FVoxelLandM5RequestLease::FVoxelLandM5RequestLease(
	TSharedRef<FVoxelLandM5PublicationState, ESPMode::ThreadSafe> InState, uint64 InEpoch, TArray<uint8>&& InKey)
	: State(MoveTemp(InState))
	, Epoch(InEpoch)
	, Key(MoveTemp(InKey))
{
}

FVoxelLandM5RequestLease::~FVoxelLandM5RequestLease()
{
	FScopeLock Lock(&State->Mutex);
	--State->Active;
}

FVoxelLandM5PublicationGuard::FVoxelLandM5PublicationGuard(int32 MaxInFlight, int32 MaxKeyBytes)
	: State(MakeShared<FVoxelLandM5PublicationState, ESPMode::ThreadSafe>())
{
	State->MaxActive = MaxInFlight;
	State->MaxKeyBytes = MaxKeyBytes;
}

FVoxelLandM5PublicationGuard::~FVoxelLandM5PublicationGuard()
{
	Close();
}

bool FVoxelLandM5PublicationGuard::TryBegin(TConstArrayView<uint8> Key, FLease& OutLease)
{
	FLease NewLease;
	{
		FScopeLock Lock(&State->Mutex);
		if (State->bClosed || Key.IsEmpty() || Key.Num() > State->MaxKeyBytes || State->MaxActive <= 0
			|| State->Active >= State->MaxActive)
		{
			return false;
		}
		if (State->Epoch == MAX_uint64)
		{
			State->bClosed = true;
			State->CurrentKey.Reset();
			State->Published.Reset();
			return false;
		}
		TArray<uint8> NewKey(Key.GetData(), Key.Num());
		++State->Epoch;
		++State->Active;
		State->CurrentKey = NewKey;
		State->Published.Reset();
		NewLease = MakeShareable(new FVoxelLandM5RequestLease(State, State->Epoch, MoveTemp(NewKey)));
	}
	// OutLease can hold the final reference to an older lease from this guard.
	// Its destructor reacquires the mutex, so release it only after unlocking.
	OutLease = MoveTemp(NewLease);
	return true;
}

bool FVoxelLandM5PublicationGuard::TryPublish(const FLease& Lease, TConstArrayView<uint8> Key, const FPlan& Plan)
{
	FScopeLock Lock(&State->Mutex);
	if (State->bClosed || !Lease.IsValid() || !Plan.IsValid() || &Lease->State.Get() != &State.Get()
		|| Lease->bPublished || Lease->Epoch != State->Epoch || Key.Num() != Lease->Key.Num()
		|| Key.Num() != State->CurrentKey.Num())
	{
		return false;
	}
	for (int32 Index = 0; Index < Key.Num(); ++Index)
	{
		if (Key[Index] != Lease->Key[Index] || Key[Index] != State->CurrentKey[Index])
		{
			return false;
		}
	}
	Lease->bPublished = true;
	State->Published = Plan;
	return true;
}

void FVoxelLandM5PublicationGuard::Cancel()
{
	FScopeLock Lock(&State->Mutex);
	if (State->Epoch == MAX_uint64)
	{
		State->bClosed = true;
	}
	else
	{
		++State->Epoch;
	}
	State->CurrentKey.Reset();
	State->Published.Reset();
}

void FVoxelLandM5PublicationGuard::Close()
{
	FScopeLock Lock(&State->Mutex);
	State->bClosed = true;
	State->CurrentKey.Reset();
	State->Published.Reset();
}

FVoxelLandM5PublicationGuard::FPlan FVoxelLandM5PublicationGuard::GetPublished() const
{
	FScopeLock Lock(&State->Mutex);
	return State->Published;
}

int32 FVoxelLandM5PublicationGuard::GetInFlight() const
{
	FScopeLock Lock(&State->Mutex);
	return State->Active;
}

#if WITH_DEV_AUTOMATION_TESTS
void FVoxelLandM5PublicationGuard::SetEpochForTesting(uint64 Epoch)
{
	FScopeLock Lock(&State->Mutex);
	State->Epoch = Epoch;
}
#endif
