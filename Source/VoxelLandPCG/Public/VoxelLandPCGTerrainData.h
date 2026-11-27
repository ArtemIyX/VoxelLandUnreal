#pragma once

#include "PCGData.h"
#include "VoxelLandPCGTerrainDataStatus.h"
#include "VoxelLandPCGTerrainDataType.h"
#include "World/Collision/VoxelLandCollisionWorldView.h"

#include "VoxelLandPCGTerrainData.generated.h"

class AVoxelLandWorld;
class FVoxelLandMaskGraphProgram;
class IVoxelLandMaterialSignalSource;
struct FVoxelLandChunkData;
struct FVoxelLandChunkKey;
struct FVoxelLandTerrainPlacementInput;
struct FVoxelLandTerrainPlacementPoint;

UCLASS(Transient)
class VOXELLANDPCG_API UPCGVoxelTerrainData final : public UPCGData
{
	GENERATED_BODY()

public:
	using FChunkPtr = TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe>;

	PCG_ASSIGN_TYPE_INFO(FPCGDataTypeInfoVoxelTerrain);

	virtual bool CanBeSerialized() const override { return false; }
	virtual bool IsCacheable() const override { return false; }

	[[nodiscard]] static EVoxelLandPCGTerrainDataStatus Capture(AVoxelLandWorld& World, const FGuid& LeaseId,
		TConstArrayView<FVoxelLandChunkKey> Keys, const FBox& LogicalOwnershipBounds, UObject* Outer,
		UPCGVoxelTerrainData*& OutData, FString* OutError = nullptr);

	void SetMaskProgram(TSharedPtr<const FVoxelLandMaskGraphProgram, ESPMode::ThreadSafe> InProgram, uint64 InSeed,
		TSharedPtr<const IVoxelLandMaterialSignalSource, ESPMode::ThreadSafe> InTerrainSignals = {});
	const FVoxelLandMaskGraphProgram* GetMaskProgram() const { return MaskProgram.Get(); }
	const IVoxelLandMaterialSignalSource* GetTerrainSignals() const { return TerrainSignals.Get(); }
	uint64 GetMaskSeed() const { return MaskSeed; }

	void Cancel() { bCancelled.Store(true); }
	bool IsCancelled() const { return bCancelled.Load(); }
	FVoxelLandTerrainPlacementInput GetPlacementInput() const;
#if WITH_EDITOR
	void EnableCandidateDebug() { bCaptureCandidates = true; }
	void RecordCandidateDebug(TConstArrayView<FVoxelLandTerrainPlacementPoint> Points) const;
	TArray<FVector> GetDebugCandidates() const;
#endif

	const FBox& GetLogicalBounds() const { return LogicalBounds; }
	const FVoxelLandCollisionWorldView& GetView() const { return View; }
	TConstArrayView<FChunkPtr> GetChunks() const { return Chunks; }

private:
	FBox LogicalBounds = FBox(ForceInit);
	FVoxelLandCollisionWorldView View;
	TArray<FChunkPtr> Chunks;
	TSharedPtr<const FVoxelLandMaskGraphProgram, ESPMode::ThreadSafe> MaskProgram;
	TSharedPtr<const IVoxelLandMaterialSignalSource, ESPMode::ThreadSafe> TerrainSignals;
	uint64 MaskSeed = 0;
	TAtomic<bool> bCancelled{false};
#if WITH_EDITOR
	mutable FCriticalSection DebugCandidateLock;
	mutable TArray<FVector> DebugCandidates;
	bool bCaptureCandidates = false;
#endif
};
