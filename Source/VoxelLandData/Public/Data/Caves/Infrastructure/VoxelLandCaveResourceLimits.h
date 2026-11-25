#pragma once

#include "CoreMinimal.h"
#include "VoxelLandCaveResourceLimits.generated.h"

// Admission limits are caps, not performance guarantees. Validate before allocation.
USTRUCT()
struct VOXELLANDDATA_API FVoxelLandM5ResourceLimits
{
	GENERATED_BODY()
	UPROPERTY(
		EditAnywhere, Category = "Limits", meta = (ClampMin = "1", ToolTip = "Maximum candidate chambers per plan."))
	int32 MaxCandidates = 256;
	UPROPERTY(
		EditAnywhere, Category = "Limits", meta = (ClampMin = "1", ToolTip = "Maximum candidate connections per plan."))
	int32 MaxEdges = 32640;
	UPROPERTY(EditAnywhere, Category = "Limits", meta = (ClampMin = "1", ToolTip = "Maximum tunnel segments per plan."))
	int32 MaxSegments = 8192;
	UPROPERTY(EditAnywhere, Category = "Limits",
		meta = (ClampMin = "1", ToolTip = "Maximum explicit route junctions per plan."))
	int32 MaxJunctions = 8192;
	UPROPERTY(EditAnywhere, Category = "Limits", meta = (ClampMin = "1", ToolTip = "Maximum routes per plan."))
	int32 MaxRoutes = 16384;
	UPROPERTY(
		EditAnywhere, Category = "Limits", meta = (ClampMin = "1", ToolTip = "Maximum total route legs across a plan."))
	int32 MaxRouteLegs = 65536;
	UPROPERTY(EditAnywhere, Category = "Limits",
		meta = (ClampMin = "1", ToolTip = "Maximum total covered-feature references across a plan."))
	int32 MaxRouteFeatureRefs = 131072;
	UPROPERTY(
		EditAnywhere, Category = "Limits", meta = (ClampMin = "1", ToolTip = "Maximum deterministic plan attempts."))
	int32 MaxAttempts = 8;
	UPROPERTY(EditAnywhere, Category = "Limits",
		meta = (ClampMin = "1", ToolTip = "Maximum accounted plan and scratch memory in bytes."))
	int64 MaxBytes = 268435456;
	UPROPERTY(EditAnywhere, Category = "Limits",
		meta = (ClampMin = "1", ToolTip = "Maximum units of validation work per plan."))
	int64 MaxValidationWork = 10000000;
};
