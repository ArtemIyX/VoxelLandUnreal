#include "World/VoxelLandM4PrepassCoordinator.h"

#include "Async/Async.h"
#include "Generation/VoxelLandM4GridLease.h"
#include "Generation/VoxelLandM4HeightGrid.h"
#include "Misc/ScopeLock.h"

class FM4PrepassCoordinatorEntry final
{
public:
	TArray<uint8> Key;
	FM4PrepassCoordinator::FLease Lease;
	FM4PrepassCoordinator::FGrid Grid;
	FString Error;
	TFuture<void> Task;
	TAtomic<bool> Cancelled{false};
	int32 Subscribers = 0;
	bool bFinished = false;
	bool bAbandoned = false;
};

class FM4PrepassCoordinatorState final
{
public:
	FCriticalSection Mutex;
	TArray<TSharedPtr<FM4PrepassCoordinatorEntry, ESPMode::ThreadSafe>> Entries;
	// Includes failed and evicted entries until their producer futures are
	// actually ready, not merely until the producer publishes its result.
	TArray<TSharedPtr<FM4PrepassCoordinatorEntry, ESPMode::ThreadSafe>> Tasks;
	int32 Active = 0;
	int32 MaxActive = 0;
	int32 MaxRetained = 0;
	bool bStopped = false;
};

namespace
{
void M4CoordinatorReap(FM4PrepassCoordinatorState& State)
{
	// Caller holds State.Mutex. Keep each admission lease until the producer
	// thread has returned and destroyed its callable and scratch captures.
	for (int32 Index = State.Tasks.Num() - 1; Index >= 0; --Index)
	{
		if (State.Tasks[Index]->Task.IsValid() && State.Tasks[Index]->Task.IsReady())
		{
			State.Tasks[Index]->Lease.Reset();
			State.Tasks.RemoveAt(Index);
		}
	}
}

// Caller holds State.Mutex. Removing a cache reference never invalidates a
// subscription or reader; the immutable grid itself retains its lease.
void M4CoordinatorPrune(FM4PrepassCoordinatorState& State)
{
	int32 Retained = 0;
	for (const auto& Entry : State.Entries)
	{
		if (Entry->bFinished && Entry->Grid.IsValid() && !Entry->bAbandoned)
		{
			++Retained;
		}
	}
	for (int32 Index = 0; Index < State.Entries.Num() && Retained > State.MaxRetained;)
	{
		const auto& Entry = State.Entries[Index];
		if (Entry->bFinished && Entry->Grid.IsValid() && !Entry->bAbandoned)
		{
			State.Entries.RemoveAt(Index);
			--Retained;
		}
		else
		{
			++Index;
		}
	}
}

void M4CoordinatorEvictIdle(FM4PrepassCoordinatorState& State)
{
	for (int32 Index = State.Entries.Num() - 1; Index >= 0; --Index)
	{
		const auto& Entry = State.Entries[Index];
		if (Entry->bFinished && Entry->Subscribers == 0)
		{
			State.Entries.RemoveAt(Index);
		}
	}
}
}

FM4PrepassSubscription::FM4PrepassSubscription(TSharedRef<FM4PrepassCoordinatorState, ESPMode::ThreadSafe> InState,
	TSharedRef<FM4PrepassCoordinatorEntry, ESPMode::ThreadSafe> InEntry)
	: State(MoveTemp(InState))
	  , Entry(MoveTemp(InEntry)) {}

FM4PrepassSubscription::~FM4PrepassSubscription()
{
	Cancel();
}

void FM4PrepassSubscription::Cancel()
{
	FScopeLock Lock(&State->Mutex);
	if (!bAttached)
	{
		return;
	}
	bAttached = false;
	check(Entry->Subscribers > 0);
	--Entry->Subscribers;
	if (Entry->Subscribers == 0 && !Entry->bFinished)
	{
		Entry->bAbandoned = true;
		Entry->Cancelled.Store(true);
	}
	M4CoordinatorPrune(*State);
	// A detached handle must not keep an evicted result and its reservation
	// alive merely because its owner retains the subscription object.
	Entry.Reset();
}

bool FM4PrepassSubscription::IsFinished() const
{
	FScopeLock Lock(&State->Mutex);
	return !bAttached || Entry->bFinished || State->bStopped;
}

bool FM4PrepassSubscription::ReadResult(
	TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe>& OutGrid, FString& OutError) const
{
	FScopeLock Lock(&State->Mutex);
	if (!bAttached)
	{
		OutError = TEXT("M4 prepass subscription cancelled");
		return false;
	}
	if (State->bStopped && !Entry->bFinished)
	{
		OutError = TEXT("M4 prepass coordinator is shut down");
		return false;
	}
	if (!Entry->bFinished)
	{
		OutError = TEXT("M4 prepass is pending");
		return false;
	}
	if (!Entry->Grid)
	{
		OutError = Entry->Error.IsEmpty() ? TEXT("M4 prepass failed") : Entry->Error;
		return false;
	}
	OutGrid = Entry->Grid;
	OutError.Reset();
	return true;
}

FM4PrepassCoordinator::FM4PrepassCoordinator(int32 MaxActiveProducers, int32 MaxRetainedResults)
	: State(MakeShared<FM4PrepassCoordinatorState, ESPMode::ThreadSafe>())
{
	State->MaxActive = FMath::Clamp(MaxActiveProducers, 1, 8);
	State->MaxRetained = FMath::Clamp(MaxRetainedResults, 0, 8);
}

FM4PrepassCoordinator::~FM4PrepassCoordinator()
{
	Shutdown();
}

TSharedPtr<FM4PrepassSubscription, ESPMode::ThreadSafe> FM4PrepassCoordinator::Subscribe(
	TConstArrayView<uint8> ExactKey, TFunctionRef<FLease()> Reserve, FProducer&& Produce, FString& OutError)
{
	OutError.Reset();
	if (ExactKey.IsEmpty() || ExactKey.Num() > 8 * 1024 * 1024 || !Produce)
	{
		OutError = TEXT("Invalid M4 prepass identity or producer");
		return nullptr;
	}
	FScopeLock Lock(&State->Mutex);
	if (State->bStopped)
	{
		OutError = TEXT("M4 prepass coordinator is shut down");
		return nullptr;
	}
	M4CoordinatorReap(*State);
	for (const auto& Existing : State->Entries)
	{
		if (!Existing->bAbandoned && (!Existing->bFinished || Existing->Grid.IsValid())
		    && Existing->Key.Num() == ExactKey.Num()
		    && FMemory::Memcmp(Existing->Key.GetData(), ExactKey.GetData(), ExactKey.Num()) == 0)
		{
			++Existing->Subscribers;
			return MakeShareable(new FM4PrepassSubscription(State, Existing.ToSharedRef()));
		}
	}
	if (State->Tasks.Num() >= State->MaxActive)
	{
		OutError = TEXT("M4 prepass producer limit reached");
		return nullptr;
	}
	FLease Lease = Reserve();
	if (!Lease)
	{
		// Release only idle cache entries; external grid readers keep their
		// own lease. Retry admission after releasing coordinator references.
		M4CoordinatorEvictIdle(*State);
		M4CoordinatorReap(*State);
		Lease = Reserve();
	}
	if (!Lease)
	{
		OutError = TEXT("M4 prepass resource budget exhausted");
		return nullptr;
	}
	TSharedRef<FM4PrepassCoordinatorEntry, ESPMode::ThreadSafe> NewEntry =
		MakeShared<FM4PrepassCoordinatorEntry, ESPMode::ThreadSafe>();
	NewEntry->Key.Append(ExactKey.GetData(), ExactKey.Num());
	NewEntry->Lease = MoveTemp(Lease);
	NewEntry->Subscribers = 1;
	State->Entries.Add(NewEntry);
	State->Tasks.Add(NewEntry);
	++State->Active;
	const TSharedRef<FM4PrepassCoordinatorState, ESPMode::ThreadSafe> SharedState = State;
	NewEntry->Task = Async(EAsyncExecution::Thread, [SharedState, NewEntry, Builder = MoveTemp(Produce)]() mutable {
		FGrid Grid;
		FString Error;
		const auto IsCancelled = [NewEntry]() {
			return NewEntry->Cancelled.Load();
		};
		const bool bSuccess = Builder(NewEntry->Lease, IsCancelled, Grid, Error);
		// Destroy producer-owned snapshots and worker captures on the producer
		// thread, before its future becomes ready and its admission is reaped.
		Builder.Reset();
		FScopeLock CompletionLock(&SharedState->Mutex);
		if (bSuccess && Grid && Grid->RetainsLease(NewEntry->Lease) && !NewEntry->Cancelled.Load()
		    && !SharedState->bStopped)
		{
			NewEntry->Grid = MoveTemp(Grid);
		}
		else
		{
			NewEntry->Error =
				Error.IsEmpty() ? TEXT("M4 prepass failed, cancelled, or did not retain its lease") : MoveTemp(Error);
		}
		NewEntry->bFinished = true;
		--SharedState->Active;
		if (!NewEntry->Grid || NewEntry->bAbandoned || SharedState->bStopped)
		{
			SharedState->Entries.Remove(NewEntry);
			// This entry cannot be joined again. Drop potentially large key
			// bytes before signalling task completion and releasing admission.
			NewEntry->Key.Empty();
		}
		M4CoordinatorPrune(*SharedState);
	});
	return MakeShareable(new FM4PrepassSubscription(State, NewEntry));
}

void FM4PrepassCoordinator::ReleaseIdleResults()
{
	FScopeLock Lock(&State->Mutex);
	M4CoordinatorReap(*State);
	M4CoordinatorEvictIdle(*State);
}

void FM4PrepassCoordinator::Shutdown()
{
	TArray<TSharedPtr<FM4PrepassCoordinatorEntry, ESPMode::ThreadSafe>> Pending;
	{
		FScopeLock Lock(&State->Mutex);
		State->bStopped = true;
		for (const auto& Entry : State->Tasks)
		{
			Entry->bAbandoned = true;
			Entry->Cancelled.Store(true);
			Pending.Add(Entry);
		}
	}
	// Never hold the lock while waiting: producers acquire it to finish.
	for (const auto& Entry : Pending)
	{
		if (Entry->Task.IsValid())
		{
			Entry->Task.Wait();
		}
	}
	{
		FScopeLock Lock(&State->Mutex);
		for (const auto& Entry : State->Tasks)
		{
			Entry->Lease.Reset();
		}
		State->Tasks.Reset();
		State->Entries.Reset();
	}
}