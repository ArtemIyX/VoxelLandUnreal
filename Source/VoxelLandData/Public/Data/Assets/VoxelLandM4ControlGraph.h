#pragma once

#include "CoreMinimal.h"
#include "Data/Assets/VoxelLandTerrainGraph.h"
#include "Data/Enums/VoxelLandM4ControlChannel.h"
#include "VoxelLandM4ControlGraph.generated.h"

// A shared authored graph whose three named terminals are compiled separately.
// Existing terrain graphs and their single compiled program are unchanged.
UCLASS(BlueprintType)
class VOXELLANDDATA_API UVoxelLandM4ControlGraph : public UVoxelLandTerrainGraph
{
	GENERATED_BODY()
public:
	virtual void Serialize(FArchive& Ar) override;
#if WITH_EDITOR
	virtual void PostSaveRoot(FObjectPostSaveRootContext ObjectSaveContext) override;
	void SetCookPreparationResult(bool bSucceeded) { bCookPreparationSucceeded = bSucceeded; }
#endif
	// When set, this terminal's three inputs define all named control outputs.
	// Empty preserves the earlier three-terminal asset format.
	UPROPERTY(VisibleAnywhere, Category = "VoxelLand|M4|Outputs")
	FGuid HeightOutputId;
	UPROPERTY(VisibleAnywhere, Category = "VoxelLand|M4|Outputs")
	FGuid ContinentalOutputId;
	UPROPERTY(VisibleAnywhere, Category = "VoxelLand|M4|Outputs")
	FGuid MountainOutputId;
	UPROPERTY(VisibleAnywhere, Category = "VoxelLand|M4|Outputs")
	FGuid PeaksOutputId;

	UPROPERTY()
	FVoxelLandSerializedProgram ContinentalProgram;
	UPROPERTY()
	FVoxelLandSerializedProgram MountainProgram;
	UPROPERTY()
	FVoxelLandSerializedProgram PeaksProgram;

	FGuid GetOutputId(EVoxelLandM4ControlChannel Channel) const;
	const FVoxelLandSerializedProgram& GetProgram(EVoxelLandM4ControlChannel Channel) const;
	bool ProjectSnapshot(const FVoxelLandGraphSnapshot& Source, EVoxelLandM4ControlChannel Channel,
		FVoxelLandGraphSnapshot& OutProjection, FString& OutError) const;
#if WITH_EDITOR
private:
	bool bCookPreparationSucceeded = false;
#endif
};
