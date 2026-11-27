#pragma once

#include "Data/Collision/Chaos/VoxelLandChaosCollisionSettings.h"
#include "VoxelLandChaosPhase.h"
#include "Data/World/Streaming/VoxelLandWorldCancel.h"
#include "World/Collision/Chaos/VoxelLandChaosCook.h"

struct FVoxelLandChaosTile
{
	int32 Owners = 0;
	int32 Attempts = 0;
	int32 InstalledLod = INDEX_NONE;
	uint64 Ticket = 0;
	uint64 SourceRevision = 0;
	double RetirementTime = 0.0;
	double RetryTime = 0.0;
	double ObservedVisualLodTime = 0.0;
	int32 StableVisualLod = INDEX_NONE;
	int32 ObservedVisualLod = INDEX_NONE;
	bool bHasVisualLod = false;
	EVoxelLandChaosCollisionDetail Detail = EVoxelLandChaosCollisionDetail::Disabled;
	EVoxelLandChaosPhase Phase = EVoxelLandChaosPhase::Field;
	TSharedPtr<const FVoxelLandChaosMesh, ESPMode::ThreadSafe> Mesh;
	TSharedPtr<VoxelLandWorldPrivate::FCancel, ESPMode::ThreadSafe> MeshCancellation;
	FVoxelLandChaosCookResultPtr Cooked;
	TWeakObjectPtr<UVoxelLandChaosTerrainComponent> Body;
	int64 ResidentBytes = 0;
	bool bHasCoverage = false;
	bool bRetireStaleBody = false;
	bool bInstallOverBudget = false;
};
