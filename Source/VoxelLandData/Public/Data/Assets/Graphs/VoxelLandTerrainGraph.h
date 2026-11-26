#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Data/Graph/Sources/VoxelLandGraphLinkRecord.h"
#include "Data/Graph/Schema/VoxelLandGraphDiagnostic.h"
#include "Data/Graph/Sources/VoxelLandGraphNodeRecord.h"
#include "Data/Graph/Sources/VoxelLandGraphParameter.h"
#include "Data/Graph/Sources/VoxelLandGraphSnapshot.h"
#include "Data/Program/Serialization/VoxelLandSerializedProgram.h"
#include "VoxelLandTerrainGraph.generated.h"

class UEdGraph;

// Source and last compiled payload are stored separately; callers must reject stale payloads.
UCLASS(BlueprintType, HideDropdown)
class VOXELLANDDATA_API UVoxelLandTerrainGraph : public UDataAsset
{
	GENERATED_BODY()

public:
	// Checks record integrity only; this does not establish that the graph can compile.
	bool ValidateStructure(FString& OutError) const;
	bool ValidateStructure(FVoxelLandGraphDiagnostic& OutDiagnostic) const;

	// Copies validated records on the game thread for subsequent worker use.
	bool CaptureSnapshot(FVoxelLandGraphSnapshot& OutSnapshot, FString& OutError) const;
	bool CaptureSnapshot(FVoxelLandGraphSnapshot& OutSnapshot, FVoxelLandGraphDiagnostic& OutDiagnostic) const;

	UPROPERTY(VisibleAnywhere, Category = "VoxelLand")
	int32 SourceVersion = 1;

	UPROPERTY(EditAnywhere, Category = "VoxelLand", meta = (TitleProperty = "Name"))
	TArray<FVoxelLandGraphParameter> Parameters;

	UPROPERTY()
	TArray<FVoxelLandGraphNodeRecord> Nodes;

	UPROPERTY()
	TArray<FVoxelLandGraphLinkRecord> Links;

	UPROPERTY()
	FVoxelLandSerializedProgram CompiledProgram;

#if WITH_EDITORONLY_DATA
	// Editor mirror and its layout are stripped from cooked runtime data.
	UPROPERTY()
	TObjectPtr<UEdGraph> EditorGraph;
#endif
};
