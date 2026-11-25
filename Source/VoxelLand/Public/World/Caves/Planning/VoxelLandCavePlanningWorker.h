#pragma once

#include "CoreMinimal.h"
#include "Data/Caves/Infrastructure/VoxelLandCaveDiagnostic.h"

// One worker per producer. Own immutable inputs and provider/budget leases until
// Wait returns. A failed Start leaves no running job. Poll is nonblocking;
// Cancel is idempotent; Wait joins the job.
// A successful diagnostic alone is never evidence permitting publication.
class VOXELLAND_API IVoxelLandM5PlanningWorker
{
public:
	virtual ~IVoxelLandM5PlanningWorker() = default;
	virtual FVoxelLandM5Diagnostic Start() = 0;
	virtual bool Poll(FVoxelLandM5Diagnostic& OutDiagnostic) = 0;
	virtual void Cancel() = 0;
	virtual void Wait() = 0;
};
