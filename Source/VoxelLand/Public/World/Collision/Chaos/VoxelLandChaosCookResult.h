#pragma once

#include "CoreMinimal.h"
#include "UObject/StrongObjectPtr.h"

class UBodySetup;
class UVoxelLandChaosCookProvider;
struct FVoxelLandChaosMesh;

// Owns both UObjects through game-thread completion, including after a world detaches its request.
class VOXELLAND_API FVoxelLandChaosCookResult final
{
public:
	FVoxelLandChaosCookResult(UVoxelLandChaosCookProvider& InProvider, UBodySetup& InBodySetup);
	~FVoxelLandChaosCookResult();

	UVoxelLandChaosCookProvider* GetProvider() const;
	UBodySetup* GetBodySetup() const;
	TSharedPtr<const FVoxelLandChaosMesh, ESPMode::ThreadSafe> GetMesh() const;

private:
	TStrongObjectPtr<UVoxelLandChaosCookProvider> Provider;
	TStrongObjectPtr<UBodySetup> BodySetup;
};

using FVoxelLandChaosCookResultPtr = TSharedPtr<const FVoxelLandChaosCookResult, ESPMode::ThreadSafe>;
