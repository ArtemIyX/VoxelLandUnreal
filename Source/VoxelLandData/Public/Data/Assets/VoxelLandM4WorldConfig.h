#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Data/Structs/VoxelLandM4ControlConfig.h"
#include "VoxelLandM4WorldConfig.generated.h"

class UVoxelLandMaterialSetup;
struct FVoxelLandM4WorldSnapshot;

// Finite M4 authoring configuration. It is separate from M3 world settings so
// old assets and their serialized program identity remain unchanged.
UCLASS(BlueprintType)
class VOXELLANDDATA_API UVoxelLandM4WorldConfig : public UDataAsset
{
	GENERATED_BODY()
public:
	// Game-thread validation/capture only. Graph compilation and proof of 2D
	// evaluation, cooked program validation and prepass execution are separate.
	bool ValidateSettings(FString& OutError) const;
	bool CaptureSnapshot(FVoxelLandM4WorldSnapshot& OutSnapshot, FString& OutError) const;
	// Checks density-affecting authored values, not the material, graph identity,
	// or graph contents. Caller must verify graph inputs and refresh the current
	// material separately on the game thread before publishing.
	bool MatchesSnapshot(const FVoxelLandM4WorldSnapshot& Snapshot) const;

	UPROPERTY(VisibleAnywhere, Category = "VoxelLand|M4")
	int32 FormatVersion = 1;
	UPROPERTY(EditAnywhere, Category = "VoxelLand|M4|Controls")
	FVoxelLandM4ControlConfig Continental;
	UPROPERTY(EditAnywhere, Category = "VoxelLand|M4|Controls")
	FVoxelLandM4ControlConfig Mountain;
	UPROPERTY(EditAnywhere, Category = "VoxelLand|M4|Controls")
	FVoxelLandM4ControlConfig Peaks;
	UPROPERTY(EditAnywhere, Category = "VoxelLand|M4")
	int64 Seed = 1234;
	UPROPERTY(EditAnywhere, Category = "VoxelLand|M4", meta = (ClampMin = "1", ClampMax = "2000000"))
	int32 VoxelSizeCm = 100;
	UPROPERTY(EditAnywhere, Category = "VoxelLand|M4")
	int32 BaseHeightQ8 = 0;
	UPROPERTY(EditAnywhere, Category = "VoxelLand|M4")
	bool bNormalizeSlope = false;
	UPROPERTY(EditAnywhere, Category = "VoxelLand|M4|Domain")
	int64 OriginXCm = -819200;
	UPROPERTY(EditAnywhere, Category = "VoxelLand|M4|Domain")
	int64 OriginYCm = -819200;
	UPROPERTY(EditAnywhere, Category = "VoxelLand|M4|Domain", meta = (ClampMin = "1", ClampMax = "1024"))
	int32 CellsX = 256;
	UPROPERTY(EditAnywhere, Category = "VoxelLand|M4|Domain", meta = (ClampMin = "1", ClampMax = "1024"))
	int32 CellsY = 256;
	UPROPERTY(EditAnywhere, Category = "VoxelLand|M4|Domain", meta = (ClampMin = "1"))
	int64 GridSpacingCm = 6400;
	UPROPERTY(EditAnywhere, Category = "VoxelLand|M4|Erosion")
	bool bGlobalErosion = true;
	UPROPERTY(EditAnywhere, Category = "VoxelLand|M4|Erosion")
	FGuid ErosionStageId = FGuid(0x564c4d34, 0x45524f53, 1, 1);
	UPROPERTY(EditAnywhere, Category = "VoxelLand|M4|Erosion", meta = (ClampMin = "0"))
	int32 ErosionSalt = 0;
	UPROPERTY(EditAnywhere, Category = "VoxelLand|M4|Erosion", meta = (ClampMin = "0", ClampMax = "65536"))
	int32 DropletCount = 4096;
	UPROPERTY(EditAnywhere, Category = "VoxelLand|M4|Erosion", meta = (ClampMin = "0", ClampMax = "128"))
	int32 MaxSteps = 64;
	UPROPERTY(EditAnywhere, Category = "VoxelLand|M4|Erosion", meta = (ClampMin = "0", ClampMax = "32"))
	int32 ThermalIterations = 8;
	UPROPERTY(EditAnywhere, Category = "VoxelLand|M4|Erosion", meta = (ClampMin = "0", ClampMax = "512000000"))
	int64 CapacityMaxQ8 = 512000000;
	UPROPERTY(EditAnywhere, Category = "VoxelLand|M4|Erosion", meta = (ClampMin = "0", ClampMax = "512000000"))
	int64 TalusQ8 = 0;
	UPROPERTY(EditAnywhere, Category = "VoxelLand|M4|Erosion", meta = (ClampMin = "0", ClampMax = "512000000"))
	int64 MaxTransferQ8 = 512000000;
	UPROPERTY(EditAnywhere, Category = "VoxelLand|M4|Overhang")
	bool bOverhangEnabled = false;
	UPROPERTY(EditAnywhere, Category = "VoxelLand|M4|Overhang", meta = (ClampMin = "0", ClampMax = "128000"))
	int32 OverhangAmplitudeQ8 = 0;
	UPROPERTY(EditAnywhere, Category = "VoxelLand|M4|Overhang", meta = (ClampMin = "1", ClampMax = "1280000"))
	int32 OverhangBandQ8 = 256000;
	UPROPERTY(EditAnywhere, Category = "VoxelLand|M4|Overhang", meta = (ClampMin = "0"))
	float OverhangFrequencyPerCm = 0.001f;
	UPROPERTY(EditAnywhere, Category = "VoxelLand|M4|Overhang")
	FGuid OverhangNodeId = FGuid(0x564c4d34, 0x4f564552, 1, 1);
	UPROPERTY(EditAnywhere, Category = "VoxelLand|M4|Overhang", meta = (ClampMin = "0"))
	int32 OverhangSalt = 0;
	UPROPERTY(EditAnywhere, Category = "VoxelLand|M4")
	TSoftObjectPtr<UVoxelLandMaterialSetup> MaterialSetup;
	UPROPERTY(EditAnywhere, Category = "VoxelLand|M4|Bounds", meta = (ClampMin = "1", ClampMax = "8"))
	int32 HorizontalRadius = 1;
	UPROPERTY(EditAnywhere, Category = "VoxelLand|M4|Bounds", meta = (ClampMin = "1", ClampMax = "4"))
	int32 VerticalRadius = 1;
};
