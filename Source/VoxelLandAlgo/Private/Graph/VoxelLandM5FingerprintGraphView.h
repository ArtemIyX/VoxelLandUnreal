#pragma once

#include "Containers/Array.h"

struct FVoxelLandGraphFunctionSnapshot;
struct FVoxelLandGraphLinkRecord;
struct FVoxelLandGraphNodeRecord;
struct FVoxelLandGraphParameter;

struct FVoxelLandM5FingerprintGraphView
{
	int32 SourceVersion;
	const TArray<FVoxelLandGraphParameter>& Parameters;
	const TArray<FVoxelLandGraphNodeRecord>& Nodes;
	const TArray<FVoxelLandGraphLinkRecord>& Links;
	const TArray<FVoxelLandGraphFunctionSnapshot>* Functions = nullptr;
};
