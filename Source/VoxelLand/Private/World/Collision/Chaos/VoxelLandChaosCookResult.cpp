#include "World/Collision/Chaos/VoxelLandChaosCookResult.h"

#include "VoxelLandChaosCookProvider.h"
#include "PhysicsEngine/BodySetup.h"

FVoxelLandChaosCookResult::FVoxelLandChaosCookResult(UVoxelLandChaosCookProvider& InProvider, UBodySetup& InBodySetup)
	: Provider(&InProvider)
	, BodySetup(&InBodySetup)
{
}

FVoxelLandChaosCookResult::~FVoxelLandChaosCookResult() = default;

UVoxelLandChaosCookProvider* FVoxelLandChaosCookResult::GetProvider() const
{
	return Provider.Get();
}

UBodySetup* FVoxelLandChaosCookResult::GetBodySetup() const
{
	return BodySetup.Get();
}

TSharedPtr<const FVoxelLandChaosMesh, ESPMode::ThreadSafe> FVoxelLandChaosCookResult::GetMesh() const
{
	return Provider->GetMesh();
}
