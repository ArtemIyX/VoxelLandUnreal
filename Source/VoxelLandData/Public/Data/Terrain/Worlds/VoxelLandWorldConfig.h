#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Data/Terrain/Controls/VoxelLandControlConfig.h"
#include "Data/Terrain/Shaping/VoxelLandHeightDetailConfig.h"
#include "Data/Caves/Planning/VoxelLandCaveSettings.h"
#include "Data/Collision/Chaos/VoxelLandChaosCollisionSettings.h"
#include "Data/Graph/Sources/VoxelLandParameterOverride.h"
#include "Data/TerrainOverlay/Configuration/VoxelLandTerrainOverlay.h"
#include "Data/World/Configuration/VoxelLandDensityBounds.h"
#include "Data/World/Configuration/VoxelLandTerrainSource.h"
#include "Data/World/Configuration/VoxelLandWorldExtent.h"
#include "VoxelLandWorldConfig.generated.h"

struct FVoxelLandGraphSnapshot;
class UVoxelLandMaterialSetup;
class UVoxelLandTerrainGraph;
struct FVoxelLandM4WorldSnapshot;
struct FVoxelLandM5WorldSnapshot;

// M4 authoring configuration.
UCLASS(BlueprintType, HideDropdown, meta = (DisplayName = "VoxelLand World Config"))
class VOXELLANDDATA_API UVoxelLandWorldConfig : public UDataAsset
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
	// Game-thread capture of an enabled M5 stage and its M4 base. This only
	// snapshots authoring data: the caller must independently validate the M5
	// descriptor, programs, providers and generated plan before publication.
	// Failure leaves OutSnapshot unchanged.
	bool CaptureM5Snapshot(FVoxelLandM5WorldSnapshot& OutSnapshot, FString& OutError) const;
	// Checks captured settings only; the caller must still verify graph contents,
	// graph identity and provider lifetime before publishing a result.
	bool MatchesM5Snapshot(const FVoxelLandM5WorldSnapshot& Snapshot) const;
#if WITH_EDITOR
	bool MatchesM5PreviewSnapshot(const FVoxelLandM5WorldSnapshot& Snapshot) const;
#endif
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
	bool CaptureSnapshotInternal(FVoxelLandM4WorldSnapshot& OutSnapshot, FString& OutError, bool bAllowM5) const;
	bool MatchesSnapshotInternal(
		const FVoxelLandM4WorldSnapshot& Snapshot, bool bCompareBakedCurves, bool bAllowM5) const;
	bool MatchesM5SnapshotInternal(const FVoxelLandM5WorldSnapshot& Snapshot, bool bCompareBakedCurves) const;

public:
	// Replaces all three baked curves atomically after validation.
	bool SetBakedCurves(TConstArrayView<int32> ContinentalSamples, TConstArrayView<int32> MountainSamples,
		TConstArrayView<int32> PeaksSamples, int32 BakeVersion, FString& OutError);
	void ClearBakedCurves();
	bool ValidateBakedCurves(FString& OutError) const;

	UPROPERTY(VisibleAnywhere, Category = "World")
	int32 FormatVersion = 1;
	UPROPERTY()
	FVoxelLandM4ControlConfig Continental;
	UPROPERTY()
	FVoxelLandM4ControlConfig Mountain;
	UPROPERTY()
	FVoxelLandM4ControlConfig Peaks;
	UPROPERTY(EditAnywhere, Category = "World")
	int64 Seed = 1234;
	UPROPERTY(EditAnywhere, Category = "Height Terrain",
		meta = (ClampMin = "1", ClampMax = "2000000",
			EditCondition = "TerrainSource == EVoxelLandTerrainSource::HeightTerrain", EditConditionHides))
	int32 VoxelSizeCm = 100;
	UPROPERTY()
	int32 BaseHeightQ8 = 0;
	UPROPERTY(EditAnywhere, Category = "Height Terrain",
		meta = (DisplayName = "Base Elevation", Units = "cm", ClampMin = "-2000000", ClampMax = "2000000",
			EditCondition = "TerrainSource == EVoxelLandTerrainSource::HeightTerrain", EditConditionHides))
	float BaseElevationCm = 0.f;
	UPROPERTY(EditAnywhere, Category = "Height Terrain",
		meta = (EditCondition = "TerrainSource == EVoxelLandTerrainSource::HeightTerrain", EditConditionHides))
	bool bNormalizeSlope = false;
	// Versioned final density filter, applied after overhang. Older assets default disabled.
	UPROPERTY(VisibleAnywhere, Category = "Height Terrain|Smooth",
		meta = (EditCondition = "TerrainSource == EVoxelLandTerrainSource::HeightTerrain", EditConditionHides))
	int32 SmoothVersion = 1;
	UPROPERTY(EditAnywhere, Category = "Height Terrain|Smooth",
		meta = (EditCondition = "TerrainSource == EVoxelLandTerrainSource::HeightTerrain", EditConditionHides))
	bool bSmoothEnabled = false;
	UPROPERTY(EditAnywhere, Category = "Height Terrain|Smooth",
		meta = (ClampMin = "0.0", ClampMax = "1.0",
			EditCondition = "TerrainSource == EVoxelLandTerrainSource::HeightTerrain", EditConditionHides))
	float SmoothStrength = 0.5f;
	UPROPERTY(EditAnywhere, Category = "Height Terrain|Height Detail",
		meta = (EditCondition = "TerrainSource == EVoxelLandTerrainSource::HeightTerrain", EditConditionHides))
	FVoxelLandM4HeightDetailConfig HeightDetail;
	// Persist the disabled configuration without exposing unfinished controls.
	// M4-only paths reject enabled stages until M5 initialization exists.
	UPROPERTY()
	FVoxelLandM5CaveSettings Caves;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "World")
	EVoxelLandTerrainSource TerrainSource = EVoxelLandTerrainSource::HeightTerrain;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "World")
	EVoxelLandWorldExtent WorldExtent = EVoxelLandWorldExtent::Finite;

	// Presentation policy is captured independently of the terrain generation identity.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chaos Terrain Collision", meta = (ShowOnlyInnerProperties))
	FVoxelLandChaosCollisionSettings ChaosCollision;

	bool IsInfinite() const { return WorldExtent == EVoxelLandWorldExtent::Infinite; }
	bool IsDirectDensity() const { return TerrainSource == EVoxelLandTerrainSource::DirectDensity; }
	bool ValidateDirectDensitySettings(FString& OutError) const;
	bool CaptureBoundOutput(FVoxelLandGraphSnapshot& Snapshot, FString& OutError) const;

	UPROPERTY(EditAnywhere, Category = "World", meta = (DisplayName = "Terrain Graph"))
	TObjectPtr<UVoxelLandTerrainGraph> Graph;

	UPROPERTY()
	FGuid TerrainOutputId;

	UPROPERTY()
	FString TerrainOutputLabel;

	UPROPERTY(EditAnywhere, Category = "Height Terrain",
		meta = (DisplayName = "Height Scale", ClampMin = "1", ClampMax = "2000000", Units = "cm",
			EditCondition = "TerrainSource == EVoxelLandTerrainSource::HeightTerrain", EditConditionHides))
	int32 HeightScaleCm = 1000;

	UPROPERTY(EditAnywhere, Category = "Density",
		meta = (ClampMin = "0.001", EditCondition = "TerrainSource == EVoxelLandTerrainSource::DirectDensity",
			EditConditionHides))
	float DirectVoxelSizeCm = 100.f;

	UPROPERTY(EditAnywhere, Category = "Density",
		meta = (EditCondition = "TerrainSource == EVoxelLandTerrainSource::DirectDensity", EditConditionHides))
	TArray<FVoxelLandParameterOverride> Overrides;

	UPROPERTY(EditAnywhere, Category = "Density",
		meta = (EditCondition =
					"TerrainSource == EVoxelLandTerrainSource::DirectDensity && WorldExtent == EVoxelLandWorldExtent::Finite",
			EditConditionHides))
	FVoxelLandDensityBounds DensityBounds;

	UPROPERTY(EditAnywhere, Category = "Height Terrain|Overlay",
		meta = (EditCondition = "TerrainSource == EVoxelLandTerrainSource::HeightTerrain", EditConditionHides))
	FVoxelLandM6WorldOverlay Overlay;
	UPROPERTY(EditAnywhere, Category = "Height Terrain|Domain",
		meta = (EditCondition =
					"TerrainSource == EVoxelLandTerrainSource::HeightTerrain && WorldExtent == EVoxelLandWorldExtent::Finite",
			EditConditionHides))
	int64 OriginXCm = -819200;
	UPROPERTY(EditAnywhere, Category = "Height Terrain|Domain",
		meta = (EditCondition =
					"TerrainSource == EVoxelLandTerrainSource::HeightTerrain && WorldExtent == EVoxelLandWorldExtent::Finite",
			EditConditionHides))
	int64 OriginYCm = -819200;
	UPROPERTY(EditAnywhere, Category = "Height Terrain|Domain",
		meta = (ClampMin = "1", ClampMax = "1024",
			EditCondition =
				"TerrainSource == EVoxelLandTerrainSource::HeightTerrain && WorldExtent == EVoxelLandWorldExtent::Finite",
			EditConditionHides))
	int32 CellsX = 256;
	UPROPERTY(EditAnywhere, Category = "Height Terrain|Domain",
		meta = (ClampMin = "1", ClampMax = "1024",
			EditCondition =
				"TerrainSource == EVoxelLandTerrainSource::HeightTerrain && WorldExtent == EVoxelLandWorldExtent::Finite",
			EditConditionHides))
	int32 CellsY = 256;
	UPROPERTY(EditAnywhere, Category = "Height Terrain|Domain",
		meta = (ClampMin = "1",
			EditCondition =
				"TerrainSource == EVoxelLandTerrainSource::HeightTerrain && WorldExtent == EVoxelLandWorldExtent::Finite",
			EditConditionHides))
	int64 GridSpacingCm = 6400;
	UPROPERTY(EditAnywhere, Category = "Height Terrain|Erosion",
		meta = (EditCondition =
					"TerrainSource == EVoxelLandTerrainSource::HeightTerrain && WorldExtent == EVoxelLandWorldExtent::Finite",
			EditConditionHides))
	bool bGlobalErosion = true;
	UPROPERTY(EditAnywhere, Category = "Height Terrain|Erosion",
		meta = (EditCondition =
					"TerrainSource == EVoxelLandTerrainSource::HeightTerrain && WorldExtent == EVoxelLandWorldExtent::Finite",
			EditConditionHides))
	FGuid ErosionStageId = FGuid(0x564c4d34, 0x45524f53, 1, 1);
	UPROPERTY(EditAnywhere, Category = "Height Terrain|Erosion",
		meta = (ClampMin = "0",
			EditCondition =
				"TerrainSource == EVoxelLandTerrainSource::HeightTerrain && WorldExtent == EVoxelLandWorldExtent::Finite",
			EditConditionHides))
	int32 ErosionSalt = 0;
	UPROPERTY(EditAnywhere, Category = "Height Terrain|Erosion",
		meta = (ClampMin = "0", ClampMax = "65536",
			EditCondition =
				"TerrainSource == EVoxelLandTerrainSource::HeightTerrain && WorldExtent == EVoxelLandWorldExtent::Finite",
			EditConditionHides))
	int32 DropletCount = 4096;
	UPROPERTY(EditAnywhere, Category = "Height Terrain|Erosion",
		meta = (ClampMin = "0", ClampMax = "128",
			EditCondition =
				"TerrainSource == EVoxelLandTerrainSource::HeightTerrain && WorldExtent == EVoxelLandWorldExtent::Finite",
			EditConditionHides))
	int32 MaxSteps = 64;
	UPROPERTY(EditAnywhere, Category = "Height Terrain|Erosion",
		meta = (ClampMin = "0", ClampMax = "32",
			EditCondition =
				"TerrainSource == EVoxelLandTerrainSource::HeightTerrain && WorldExtent == EVoxelLandWorldExtent::Finite",
			EditConditionHides))
	int32 ThermalIterations = 8;
	UPROPERTY(EditAnywhere, Category = "Height Terrain|Erosion",
		meta = (ClampMin = "0", ClampMax = "512000000",
			EditCondition =
				"TerrainSource == EVoxelLandTerrainSource::HeightTerrain && WorldExtent == EVoxelLandWorldExtent::Finite",
			EditConditionHides))
	int64 CapacityMaxQ8 = 512000000;
	UPROPERTY(EditAnywhere, Category = "Height Terrain|Erosion",
		meta = (ClampMin = "0", ClampMax = "512000000",
			EditCondition =
				"TerrainSource == EVoxelLandTerrainSource::HeightTerrain && WorldExtent == EVoxelLandWorldExtent::Finite",
			EditConditionHides))
	int64 TalusQ8 = 0;
	UPROPERTY(EditAnywhere, Category = "Height Terrain|Erosion",
		meta = (ClampMin = "0", ClampMax = "512000000",
			EditCondition =
				"TerrainSource == EVoxelLandTerrainSource::HeightTerrain && WorldExtent == EVoxelLandWorldExtent::Finite",
			EditConditionHides))
	int64 MaxTransferQ8 = 512000000;
	UPROPERTY(EditAnywhere, Category = "Height Terrain|Overhang",
		meta = (EditCondition = "TerrainSource == EVoxelLandTerrainSource::HeightTerrain", EditConditionHides))
	bool bOverhangEnabled = false;
	UPROPERTY(EditAnywhere, Category = "Height Terrain|Overhang",
		meta = (ClampMin = "0", ClampMax = "128000",
			EditCondition = "TerrainSource == EVoxelLandTerrainSource::HeightTerrain", EditConditionHides))
	int32 OverhangAmplitudeQ8 = 0;
	UPROPERTY(EditAnywhere, Category = "Height Terrain|Overhang",
		meta = (ClampMin = "1", ClampMax = "1280000",
			EditCondition = "TerrainSource == EVoxelLandTerrainSource::HeightTerrain", EditConditionHides))
	int32 OverhangBandQ8 = 256000;
	UPROPERTY(EditAnywhere, Category = "Height Terrain|Overhang",
		meta = (ClampMin = "0", EditCondition = "TerrainSource == EVoxelLandTerrainSource::HeightTerrain",
			EditConditionHides))
	float OverhangFrequencyPerCm = 0.001f;
	UPROPERTY(EditAnywhere, Category = "Height Terrain|Overhang",
		meta = (EditCondition = "TerrainSource == EVoxelLandTerrainSource::HeightTerrain", EditConditionHides))
	FGuid OverhangNodeId = FGuid(0x564c4d34, 0x4f564552, 1, 1);
	UPROPERTY(EditAnywhere, Category = "Height Terrain|Overhang",
		meta = (ClampMin = "0", EditCondition = "TerrainSource == EVoxelLandTerrainSource::HeightTerrain",
			EditConditionHides))
	int32 OverhangSalt = 0;
	UPROPERTY(EditAnywhere, Category = "World")
	TSoftObjectPtr<UVoxelLandMaterialSetup> MaterialSetup;

	UPROPERTY(EditAnywhere, Category = "Appearance",
		meta = (ToolTip = "Optional Material graph. Without a graph, the Material Setup rules assign surfaces."))
	TSoftObjectPtr<UVoxelLandTerrainGraph> MaterialGraph;

	UPROPERTY(EditAnywhere, Category = "Appearance",
		meta = (ClampMin = "0.0", ClampMax = "1.0", DisplayName = "Terrain Ambient Occlusion Strength",
			ToolTip = "Geometry occlusion for palette terrain. Zero skips the geometry bake."))
	float TerrainAmbientOcclusionStrength = 0.0f;
	UPROPERTY(EditAnywhere, Category = "Streaming", meta = (ClampMin = "1", ClampMax = "8"))
	int32 HorizontalRadius = 1;
	UPROPERTY(EditAnywhere, Category = "Streaming", meta = (ClampMin = "1", ClampMax = "4"))
	int32 VerticalRadius = 1;
#if WITH_EDITOR

private:
	// Transient and deliberately false until this cook's PreSave succeeds.
	bool bCookPreparationSucceeded = false;
#endif
};
