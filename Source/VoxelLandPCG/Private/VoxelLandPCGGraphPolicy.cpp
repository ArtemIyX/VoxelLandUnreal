#include "VoxelLandPCGGraphPolicy.h"

#include "VoxelLandPCGGetVoxelTerrainSettings.h"
#include "VoxelLandPCGVoxelMaskSampleSettings.h"
#include "VoxelLandPCGVoxelProjectSettings.h"
#include "VoxelLandPCGVoxelSurfaceSamplerSettings.h"
#include "Elements/Metadata/PCGMetadataMathsOpElement.h"
#include "Elements/PCGAttributeFilter.h"
#include "Elements/PCGAttributeNoise.h"
#include "Elements/PCGDensityFilter.h"
#include "Elements/PCGMergeElement.h"
#include "Elements/PCGTransformPoints.h"
#include "PCGGraph.h"
#include "PCGNode.h"

bool FVoxelLandPCGGraphPolicy::Validate(const UPCGGraph& Graph, FString& OutError)
{
	for (const UPCGNode* Node : Graph.GetNodes())
	{
		const UPCGSettings* Settings = Node ? Node->GetSettings() : nullptr;
		if (!Settings || !IsSupported(Settings->GetClass()))
		{
			OutError = FString::Printf(TEXT("PCG graph '%s' uses unsupported node '%s'. Output points and assign the mesh to the VoxelLand layer."),
				*Graph.GetName(), *GetNameSafe(Settings ? Settings->GetClass() : nullptr));
			return false;
		}
	}
	OutError.Reset();
	return true;
}

bool FVoxelLandPCGGraphPolicy::IsSupported(const UClass* SettingsClass)
{
	const UClass* Supported[] = {UVoxelLandPCGGetVoxelTerrainSettings::StaticClass(),
		UVoxelLandPCGVoxelSurfaceSamplerSettings::StaticClass(), UVoxelLandPCGVoxelProjectSettings::StaticClass(),
		UVoxelLandPCGVoxelMaskSampleSettings::StaticClass(), UPCGAttributeFilteringSettings::StaticClass(),
		UPCGAttributeFilteringRangeSettings::StaticClass(), UPCGAttributeNoiseSettings::StaticClass(),
		UPCGDensityFilterSettings::StaticClass(), UPCGMergeSettings::StaticClass(), UPCGMetadataMathsSettings::StaticClass(),
		UPCGTransformPointsSettings::StaticClass()};
	for (const UClass* Candidate : Supported)
	{
		if (Candidate == SettingsClass)
		{
			return true;
		}
	}
	return false;
}
