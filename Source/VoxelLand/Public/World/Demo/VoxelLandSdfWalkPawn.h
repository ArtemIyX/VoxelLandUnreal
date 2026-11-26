#pragma once

#include "GameFramework/Pawn.h"
#include "World/Collision/VoxelLandWalkSettings.h"
#include "World/Collision/VoxelLandWalkState.h"

#include "VoxelLandSdfWalkPawn.generated.h"

class APlayerController;
class AVoxelLandWorld;
class UCameraComponent;
class UCapsuleComponent;

UCLASS(Placeable, meta = (DisplayName = "VoxelLand SDF Walk Pawn"))
class VOXELLAND_API AVoxelLandSdfWalkPawn final : public APawn
{
	GENERATED_BODY()

public:
	AVoxelLandSdfWalkPawn();

	// Assign a terrain world that initializes at startup.
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "VoxelLand|Demo")
	TObjectPtr<AVoxelLandWorld> TerrainWorld;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "VoxelLand|Demo")
	bool bIsMotionBlocked = false;

	UFUNCTION(BlueprintPure, Category = "VoxelLand|Demo")
	bool IsGrounded() const { return WalkState.bIsGrounded; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void UnPossessed() override;

private:
	static constexpr int32 MaxStepsPerTick = 4;

	FVector2d ReadMovementInput(const APlayerController& Player) const;
	FBox MakeDemandBounds(const FVector& LocalCenter) const;
	bool SimulateStep(AVoxelLandWorld& Terrain, const FVector2d& Input);
	void ClearCollisionDemand();

	UPROPERTY(VisibleAnywhere, Category = "VoxelLand|Demo")
	TObjectPtr<UCapsuleComponent> Capsule;

	UPROPERTY(VisibleAnywhere, Category = "VoxelLand|Demo")
	TObjectPtr<UCameraComponent> Camera;

	const FVoxelLandWalkSettings WalkSettings;
	FVoxelLandWalkState WalkState;
	TWeakObjectPtr<AVoxelLandWorld> ActiveTerrain;
	TOptional<uint64> ActiveEpoch;
	FGuid DemandOwner;
	double AccumulatedSeconds = 0.0;
};
