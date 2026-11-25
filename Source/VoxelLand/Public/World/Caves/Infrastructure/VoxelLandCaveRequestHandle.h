#pragma once

#include "CoreMinimal.h"
#include "Data/Caves/Infrastructure/VoxelLandCaveDiagnostic.h"

class FVoxelLandM5RequestConsumer;

// One consumer may detach without cancelling other consumers of the producer.
class VOXELLAND_API FVoxelLandM5RequestHandle final
{
public:
	explicit FVoxelLandM5RequestHandle(TSharedRef<FVoxelLandM5RequestConsumer, ESPMode::ThreadSafe> InConsumer);
	void Cancel();
	bool TryGetResult(FVoxelLandM5Diagnostic& OutDiagnostic) const;

private:
	TSharedRef<FVoxelLandM5RequestConsumer, ESPMode::ThreadSafe> Consumer;
};
