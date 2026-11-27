#pragma once

#include "CoreMinimal.h"
#include "Interfaces/Interface_CollisionDataProvider.h"
#include "VoxelLandChaosCookProvider.generated.h"

struct FVoxelLandChaosMesh;

UCLASS(Transient)
class UVoxelLandChaosCookProvider final
	: public UObject
	, public IInterface_CollisionDataProvider
{
	GENERATED_BODY()

public:
	void SetMesh(TSharedRef<const FVoxelLandChaosMesh, ESPMode::ThreadSafe> InMesh);
	TSharedPtr<const FVoxelLandChaosMesh, ESPMode::ThreadSafe> GetMesh() const;

	virtual bool GetPhysicsTriMeshData(FTriMeshCollisionData* OutData, bool bInUseAllTriData) override;
	virtual bool ContainsPhysicsTriMeshData(bool bInUseAllTriData) const override;

private:
	TSharedPtr<const FVoxelLandChaosMesh, ESPMode::ThreadSafe> Mesh;
};
