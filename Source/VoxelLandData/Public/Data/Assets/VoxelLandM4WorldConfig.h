#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Data/Structs/VoxelLandM4ControlConfig.h"
#include "Data/Enums/VoxelLandM4UnlimitedBackend.h"
#include "Data/Structs/VoxelLandM4HeightDetailConfig.h"
#include "VoxelLandM4WorldConfig.generated.h"

class UVoxelLandMaterialSetup;
struct FVoxelLandM4WorldSnapshot;

// M4 authoring configuration. It is separate from M3 world settings so
// old assets and their serialized program identity remain unchanged.
UCLASS(BlueprintType)
class VOXELLANDDATA_API UVoxelLandM4WorldConfig : public UDataAsset
{
	GENERATED_BODY()
public:
	virtual void Serialize(FArchive& Ar) override;
#if WITH_EDITOR
	virtual void PostSaveRoot(FObjectPostSaveRootContext ObjectSaveContext) override;
	// Set by the editor's first concurrent cook PreSave after full source and LUT validation.
	void SetCookPreparationResult(bool bSucceeded) { bCookPreparationSucceeded = bSucceeded; }
	void EnforceCookPreparation(FArchive& Ar) const;
	// Data validation checks the persisted authoring settings and baked LUTs.
	// Full compiled-program/2D validation still runs in the editor bake hook.
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
	// Game-thread validation/capture only. Graph compilation and proof of 2D
	// evaluation, cooked program validation and prepass execution are separate.
	bool ValidateSettings(FString& OutError) const;
	bool CaptureSnapshot(FVoxelLandM4WorldSnapshot& OutSnapshot, FString& OutError) const;
	// Checks density-affecting authored values, not the material, graph identity,
	// or graph contents. Caller must verify graph inputs and refresh the current
	// material separately on the game thread before publishing.
	bool MatchesSnapshot(const FVoxelLandM4WorldSnapshot& Snapshot) const;
#if WITH_EDITOR
	// Preview must independently rebake and validate captured keys; this check
	// ignores only persisted LUT cache bytes and never validates graph programs.
	bool MatchesPreviewSnapshot(const FVoxelLandM4WorldSnapshot& Snapshot) const;
#endif
private:
	bool MatchesSnapshotInternal(const FVoxelLandM4WorldSnapshot& Snapshot, bool bCompareBakedCurves) const;

public:
	// Replaces all three baked curves atomically after validation.
	bool SetBakedCurves(TConstArrayView<int32> ContinentalSamples, TConstArrayView<int32> MountainSamples,
		TConstArrayView<int32> PeaksSamples, int32 BakeVersion, FString& OutError);
	void ClearBakedCurves();
	bool ValidateBakedCurves(FString& OutError) const;

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
	// Versioned final density filter, applied after overhang. Older assets default disabled.
	UPROPERTY(VisibleAnywhere, Category = "VoxelLand|M4|Smooth")
	int32 SmoothVersion = 1;
	UPROPERTY(EditAnywhere, Category = "VoxelLand|M4|Smooth")
	bool bSmoothEnabled = false;
	UPROPERTY(EditAnywhere, Category = "VoxelLand|M4|Smooth", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SmoothStrength = 0.5f;
	UPROPERTY(EditAnywhere, Category = "VoxelLand|M4|Height Detail")
	FVoxelLandM4HeightDetailConfig HeightDetail;
	// Unlimited mode evaluates controls per chunk and requires global erosion off.
	UPROPERTY(EditAnywhere, Category = "VoxelLand|M4|Domain")
	bool bUnlimited = false;
	// Fixed lattice v2 uses a distinct terrain appearance and restricted graphs.
	UPROPERTY(EditAnywhere, Category = "VoxelLand|M4|Domain")
	EVoxelLandM4UnlimitedBackend UnlimitedBackend = EVoxelLandM4UnlimitedBackend::LegacyFloat;
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
#if WITH_EDITOR
private:
	// Transient and deliberately false until this cook's PreSave succeeds.
	bool bCookPreparationSucceeded = false;
#endif
};
