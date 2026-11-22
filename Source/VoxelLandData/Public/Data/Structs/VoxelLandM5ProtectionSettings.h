#pragma once

#include "CoreMinimal.h"
#include "VoxelLandM5ProtectionSettings.generated.h"

// Constant baseline only. An additional immutable mask must be supplied separately.
USTRUCT()
struct VOXELLANDDATA_API FVoxelLandM5ProtectionSettings
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Category = "Protection",
		meta = (ToolTip =
					"Treat the entire planning domain as protected solid unless a future explicit policy states otherwise."))
	bool bProtectAll = false;
	UPROPERTY(EditAnywhere, Category = "Protection",
		meta = (ToolTip = "Require an immutable protection mask in addition to the constant baseline."))
	bool bRequireMask = false;
};
