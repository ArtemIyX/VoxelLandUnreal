#pragma once

#include "Graph/VoxelLandGraphNodeOrigin.h"

#include "CoreMinimal.h"

struct FVoxelLandGraphSnapshot;
struct FVoxelLandGraphDiagnostic;

// Pure expansion of captured function records. Failure publishes no partial graph.
class VOXELLANDALGO_API FVoxelLandGraphFunctionFlatten
{
public:
    static bool ValidateSource(const FVoxelLandGraphSnapshot& Source, FVoxelLandGraphDiagnostic& Diagnostic);
    static bool Expand(const FVoxelLandGraphSnapshot& Source, FVoxelLandGraphSnapshot& OutGraph,
        FVoxelLandGraphDiagnostic& Diagnostic);
    static bool Expand(const FVoxelLandGraphSnapshot& Source, FVoxelLandGraphSnapshot& OutGraph,
        FVoxelLandGraphDiagnostic& Diagnostic, TMap<FGuid, FGuid>& OriginCalls, TMap<FGuid, FString>& OriginDescriptions,
        TMap<FGuid, FVoxelLandGraphNodeOrigin>* NodeOrigins = nullptr);
    static void TranslateDiagnostic(FVoxelLandGraphDiagnostic& Diagnostic, const TMap<FGuid, FGuid>& OriginCalls,
        const TMap<FGuid, FString>& OriginDescriptions);
};
