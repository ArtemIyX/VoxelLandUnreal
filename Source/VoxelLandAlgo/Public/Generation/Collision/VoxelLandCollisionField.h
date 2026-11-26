#pragma once

#include "CoreMinimal.h"
#include "Data/Field/Chunks/VoxelLandChunkKey.h"
#include "Data/World/Configuration/VoxelLandDensityBounds.h"
#include "Generation/Collision/VoxelLandCollisionFieldStatus.h"

struct FVoxelLandChunkData;

// Owns pinned LOD 0 references for one caller-controlled generation and lifetime.
class VOXELLANDALGO_API FVoxelLandCollisionField final
{
public:
	using FChunkPtr = TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe>;
	using FChunkMap = TMap<FVoxelLandChunkKey, FChunkPtr>;

	FVoxelLandCollisionField(
		double InVoxelSizeCm, FChunkMap InChunks, TOptional<FVoxelLandDensityBounds> InFiniteDirectDensityBounds = {});

	[[nodiscard]] double GetVoxelSizeCm() const { return VoxelSizeCm; }
	[[nodiscard]] bool IsValid() const { return bIsValid; }
	[[nodiscard]] bool TryGetCell(const FVector3d& LogicalCm, FInt64Vector& OutCell) const;
	[[nodiscard]] static bool TryGetCellOwner(const FInt64Vector& Cell, FVoxelLandChunkKey& OutOwner);

	// A successful empty dependency means this cell is wholly outside known solid bounds.
	[[nodiscard]] bool TryGetCellDependency(const FInt64Vector& Cell, TOptional<FVoxelLandChunkKey>& OutOwner) const;

	// Corners are indexed [Z][Y][X]. A non-available result leaves the density output intact.
	[[nodiscard]] EVoxelLandCollisionFieldStatus ReadCell(
		const FInt64Vector& Cell, double (&OutDensity)[2][2][2], FVoxelLandChunkKey* OutMissingKey = nullptr) const;

	// Density is in voxel units. The optional gradient is in density units per lattice unit.
	[[nodiscard]] EVoxelLandCollisionFieldStatus Sample(
		const FVector3d& LogicalCm, double& OutDensity, FVector3d* OutGradient = nullptr) const;

	[[nodiscard]] static double EvaluateCell(
		const double (&Density)[2][2][2], const FVector3d& Fraction, FVector3d* OutGradient = nullptr);

private:
	[[nodiscard]] bool TryLocatePoint(const FVector3d& LogicalCm, FVector3d& OutLattice, FInt64Vector& OutCell) const;

	double VoxelSizeCm;
	FChunkMap Chunks;
	TOptional<FVoxelLandDensityBounds> FiniteDirectDensityBounds;
	bool bIsValid;
};
