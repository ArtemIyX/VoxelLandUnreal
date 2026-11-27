#pragma once

#include "CoreMinimal.h"

#include "VoxelLandPCGLayer.generated.h"

class UPCGGraphInterface;
class USkeletalMesh;
class UStaticMesh;

USTRUCT(BlueprintType)
struct VOXELLANDPCG_API FVoxelLandPCGLayer
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Placement")
	FName LayerId = TEXT("Forest");

	UPROPERTY(EditAnywhere, Category = "Placement")
	TObjectPtr<UPCGGraphInterface> Graph;

	UPROPERTY(EditAnywhere, Category = "Placement")
	TObjectPtr<UStaticMesh> StaticMesh;

	UPROPERTY(EditAnywhere, Category = "Placement")
	TObjectPtr<USkeletalMesh> SkeletalMesh;

	UPROPERTY(EditAnywhere, Category = "Placement")
	int32 SeedOffset = 0;

	UPROPERTY(EditAnywhere, Category = "Streaming", meta = (ClampMin = "0.0", Units = "cm"))
	double GenerationRadiusCm = 25000.0;

	UPROPERTY(EditAnywhere, Category = "Streaming", meta = (ClampMin = "0.0", Units = "cm"))
	double RetirementMarginCm = 6400.0;

	UPROPERTY(EditAnywhere, Category = "Streaming", meta = (ClampMin = "0.0", Units = "cm"))
	double VisibilityDistanceCm = 30000.0;

	UPROPERTY(EditAnywhere, Category = "Streaming", meta = (ClampMin = "0.0", Units = "cm"))
	double NeighborMarginCm = 1000.0;

	UPROPERTY(EditAnywhere, Category = "Streaming")
	bool bOverrideVerticalBounds = false;

	UPROPERTY(EditAnywhere, Category = "Streaming", meta = (EditCondition = "bOverrideVerticalBounds", Units = "cm"))
	FVector2D LogicalHeightRangeCm = FVector2D(-3200.0, 3200.0);
};
