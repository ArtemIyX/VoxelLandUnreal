#include "World/Caves/Infrastructure/VoxelLandCaveRequestCoordinator.h"

#include "Misc/ScopeLock.h"
#include "World/Caves/Planning/VoxelLandCavePlanningWorker.h"
#include "World/Caves/Infrastructure/VoxelLandCaveRequestHandle.h"

class FVoxelLandM5RequestConsumer final
{
public:
	FCriticalSection Mutex;
	bool bFinished = false;
	FVoxelLandM5Diagnostic Diagnostic;
};

class FVoxelLandM5RequestEntry final
{
public:
	TArray<uint8> Key;
	TSharedPtr<IVoxelLandM5PlanningWorker, ESPMode::ThreadSafe> Worker;
	TArray<TWeakPtr<FVoxelLandM5RequestConsumer, ESPMode::ThreadSafe>> Consumers;
	bool bCancelled = false;
	bool bJoined = false;
};

class FVoxelLandM5RequestCoordinatorInternals final
{
public:
	static constexpr int32 MaxKeyBytes = 8 * 1024 * 1024 + 8192 + 64;
	static constexpr int32 MaxConsumers = 256;

	static FVoxelLandM5Diagnostic Error(EVoxelLandM5ErrorCode Code, const TCHAR* Message)
	{
		FVoxelLandM5Diagnostic Result;
		Result.Code = Code;
		Result.Stage = TEXT("M5.RequestCoordinator");
		Result.Message = Message;
		return Result;
	}

	static bool SameKey(const TArray<uint8>& A, TConstArrayView<uint8> B)
	{
		return A.Num() == B.Num() && FMemory::Memcmp(A.GetData(), B.GetData(), A.Num()) == 0;
	}

	static bool HasActiveConsumers(const FVoxelLandM5RequestEntry& Entry)
	{
		for (const TWeakPtr<FVoxelLandM5RequestConsumer, ESPMode::ThreadSafe>& Weak : Entry.Consumers)
		{
			const TSharedPtr<FVoxelLandM5RequestConsumer, ESPMode::ThreadSafe> Consumer = Weak.Pin();
			if (Consumer.IsValid())
			{
				FScopeLock Lock(&Consumer->Mutex);
				if (!Consumer->bFinished)
				{
					return true;
				}
			}
		}
		return false;
	}

	static void PruneConsumers(FVoxelLandM5RequestEntry& Entry)
	{
		for (int32 Index = Entry.Consumers.Num() - 1; Index >= 0; --Index)
		{
			const TSharedPtr<FVoxelLandM5RequestConsumer, ESPMode::ThreadSafe> Consumer = Entry.Consumers[Index].Pin();
			bool bRemove = !Consumer.IsValid();
			if (Consumer.IsValid())
			{
				FScopeLock Lock(&Consumer->Mutex);
				bRemove = Consumer->bFinished;
			}
			if (bRemove)
			{
				Entry.Consumers.RemoveAtSwap(Index);
			}
		}
	}

	static void Finish(FVoxelLandM5RequestEntry& Entry, const FVoxelLandM5Diagnostic& Diagnostic)
	{
		for (const TWeakPtr<FVoxelLandM5RequestConsumer, ESPMode::ThreadSafe>& Weak : Entry.Consumers)
		{
			const TSharedPtr<FVoxelLandM5RequestConsumer, ESPMode::ThreadSafe> Consumer = Weak.Pin();
			if (Consumer.IsValid())
			{
				FScopeLock Lock(&Consumer->Mutex);
				if (!Consumer->bFinished)
				{
					Consumer->Diagnostic = Diagnostic;
					Consumer->bFinished = true;
				}
			}
		}
	}
};

FVoxelLandM5RequestHandle::FVoxelLandM5RequestHandle(
	TSharedRef<FVoxelLandM5RequestConsumer, ESPMode::ThreadSafe> InConsumer)
	: Consumer(MoveTemp(InConsumer))
{
}

void FVoxelLandM5RequestHandle::Cancel()
{
	FScopeLock Lock(&Consumer->Mutex);
	if (!Consumer->bFinished)
	{
		Consumer->Diagnostic = FVoxelLandM5RequestCoordinatorInternals::Error(
			EVoxelLandM5ErrorCode::Cancelled, TEXT("Request consumer cancelled"));
		Consumer->bFinished = true;
	}
}

bool FVoxelLandM5RequestHandle::TryGetResult(FVoxelLandM5Diagnostic& OutDiagnostic) const
{
	FScopeLock Lock(&Consumer->Mutex);
	if (!Consumer->bFinished)
	{
		return false;
	}
	OutDiagnostic = Consumer->Diagnostic;
	return true;
}

FVoxelLandM5RequestCoordinator::FVoxelLandM5RequestCoordinator(int32 InMaxActive)
	: MaxActive(InMaxActive)
{
}

FVoxelLandM5RequestCoordinator::~FVoxelLandM5RequestCoordinator()
{
	Close();
}

TSharedPtr<FVoxelLandM5RequestHandle, ESPMode::ThreadSafe> FVoxelLandM5RequestCoordinator::Request(
	TConstArrayView<uint8> Key, TSharedRef<IVoxelLandM5PlanningWorker, ESPMode::ThreadSafe> Worker,
	FVoxelLandM5Diagnostic& OutDiagnostic)
{
	check(IsInGameThread());
	if (bClosed || bBusy || Key.IsEmpty() || Key.Num() > FVoxelLandM5RequestCoordinatorInternals::MaxKeyBytes
		|| MaxActive <= 0)
	{
		OutDiagnostic = FVoxelLandM5RequestCoordinatorInternals::Error(
			EVoxelLandM5ErrorCode::InvalidInput, TEXT("Request coordinator closed or key invalid"));
		return nullptr;
	}
	FVoxelLandM5RequestEntry* Entry = nullptr;
	for (const TUniquePtr<FVoxelLandM5RequestEntry>& Existing : Entries)
	{
		if (!Existing->bCancelled && FVoxelLandM5RequestCoordinatorInternals::HasActiveConsumers(*Existing)
			&& FVoxelLandM5RequestCoordinatorInternals::SameKey(Existing->Key, Key))
		{
			Entry = Existing.Get();
			break;
		}
	}
	if (!Entry)
	{
		if (Entries.Num() >= MaxActive)
		{
			OutDiagnostic = FVoxelLandM5RequestCoordinatorInternals::Error(
				EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("Request producer limit reached"));
			return nullptr;
		}
		TArray<uint8> CapturedKey(Key.GetData(), Key.Num());
		bBusy = true;
		FVoxelLandM5Diagnostic StartError = Worker->Start();
		if (bClosed && StartError.Code == EVoxelLandM5ErrorCode::None)
		{
			Worker->Cancel();
			Worker->Wait();
		}
		bBusy = false;
		if (bClosed)
		{
			Close();
			OutDiagnostic = FVoxelLandM5RequestCoordinatorInternals::Error(
				EVoxelLandM5ErrorCode::Cancelled, TEXT("Request coordinator closed during worker start"));
			return nullptr;
		}
		if (StartError.Code != EVoxelLandM5ErrorCode::None)
		{
			OutDiagnostic = MoveTemp(StartError);
			return nullptr;
		}
		TUniquePtr<FVoxelLandM5RequestEntry> NewEntry = MakeUnique<FVoxelLandM5RequestEntry>();
		NewEntry->Key = MoveTemp(CapturedKey);
		NewEntry->Worker = Worker;
		Entry = NewEntry.Get();
		Entries.Add(MoveTemp(NewEntry));
	}
	FVoxelLandM5RequestCoordinatorInternals::PruneConsumers(*Entry);
	if (Entry->Consumers.Num() >= FVoxelLandM5RequestCoordinatorInternals::MaxConsumers)
	{
		OutDiagnostic = FVoxelLandM5RequestCoordinatorInternals::Error(
			EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("Request consumer limit reached"));
		return nullptr;
	}
	TSharedRef<FVoxelLandM5RequestConsumer, ESPMode::ThreadSafe> Consumer =
		MakeShared<FVoxelLandM5RequestConsumer, ESPMode::ThreadSafe>();
	Entry->Consumers.Add(Consumer);
	OutDiagnostic = {};
	return MakeShared<FVoxelLandM5RequestHandle, ESPMode::ThreadSafe>(Consumer);
}

void FVoxelLandM5RequestCoordinator::Poll()
{
	check(IsInGameThread());
	if (bBusy || bClosed)
	{
		return;
	}
	bBusy = true;
	for (int32 Index = Entries.Num() - 1; Index >= 0 && !bClosed; --Index)
	{
		FVoxelLandM5RequestEntry& Entry = *Entries[Index];
		if (!Entry.bCancelled && !FVoxelLandM5RequestCoordinatorInternals::HasActiveConsumers(Entry))
		{
			Entry.bCancelled = true;
			Entry.Worker->Cancel();
		}
		if (bClosed)
		{
			break;
		}
		FVoxelLandM5Diagnostic Diagnostic;
		if (!Entry.Worker->Poll(Diagnostic))
		{
			continue;
		}
		if (bClosed)
		{
			break;
		}
		Entry.Worker->Wait();
		Entry.bJoined = true;
		if (bClosed)
		{
			break;
		}
		if (Diagnostic.Code == EVoxelLandM5ErrorCode::None)
		{
			Diagnostic = FVoxelLandM5RequestCoordinatorInternals::Error(
				EVoxelLandM5ErrorCode::Unproven, TEXT("Planning completed without publishable final-field evidence"));
		}
		FVoxelLandM5RequestCoordinatorInternals::Finish(Entry, Diagnostic);
		Entries.RemoveAt(Index);
	}
	bBusy = false;
	if (bClosed)
	{
		Close();
	}
}

void FVoxelLandM5RequestCoordinator::Close()
{
	check(IsInGameThread());
	bClosed = true;
	if (bBusy || bDrained)
	{
		return;
	}
	bBusy = true;
	for (const TUniquePtr<FVoxelLandM5RequestEntry>& Entry : Entries)
	{
		if (!Entry->bCancelled && !Entry->bJoined)
		{
			Entry->bCancelled = true;
			Entry->Worker->Cancel();
		}
	}
	for (const TUniquePtr<FVoxelLandM5RequestEntry>& Entry : Entries)
	{
		if (!Entry->bJoined)
		{
			Entry->Worker->Wait();
			Entry->bJoined = true;
		}
		FVoxelLandM5RequestCoordinatorInternals::Finish(
			*Entry, FVoxelLandM5RequestCoordinatorInternals::Error(
						EVoxelLandM5ErrorCode::Cancelled, TEXT("Owning request coordinator closed")));
	}
	Entries.Reset();
	bDrained = true;
	bBusy = false;
}
