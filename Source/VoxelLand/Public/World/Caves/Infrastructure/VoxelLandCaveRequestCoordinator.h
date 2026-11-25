#pragma once

#include "CoreMinimal.h"
#include "Data/Caves/Infrastructure/VoxelLandCaveDiagnostic.h"

class FVoxelLandM5RequestEntry;
class FVoxelLandM5RequestHandle;
class IVoxelLandM5PlanningWorker;

// World-owned, game-thread service. Key must contain the full, validated
// identity and complete canonical request closure. Keys are limited to the
// existing maximum identity plus 4160 bytes of closure framing. No result
// authorizes terrain. Calls are game-thread only; nested Close defers draining
// until the current worker callback returns. The owner must outlive callbacks.
class VOXELLAND_API FVoxelLandM5RequestCoordinator final
{
public:
	explicit FVoxelLandM5RequestCoordinator(int32 InMaxActive = 2);
	~FVoxelLandM5RequestCoordinator();
	FVoxelLandM5RequestCoordinator(const FVoxelLandM5RequestCoordinator&) = delete;
	FVoxelLandM5RequestCoordinator& operator=(const FVoxelLandM5RequestCoordinator&) = delete;

	TSharedPtr<FVoxelLandM5RequestHandle, ESPMode::ThreadSafe> Request(TConstArrayView<uint8> Key,
		TSharedRef<IVoxelLandM5PlanningWorker, ESPMode::ThreadSafe> Worker, FVoxelLandM5Diagnostic& OutDiagnostic);
	void Poll();
	void Close();

private:
	TArray<TUniquePtr<FVoxelLandM5RequestEntry>> Entries;
	int32 MaxActive;
	bool bClosed = false;
	bool bBusy = false;
	bool bDrained = false;
};
