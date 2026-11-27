#pragma once

#include "CoreMinimal.h"
#include "VoxelLandTerrainPlacementInput.h"
#include "VoxelLandTerrainPlacementMesh.h"
#include "VoxelLandTerrainPlacementPoint.h"

class VOXELLANDALGO_API FVoxelLandTerrainPlacement final
{
public:
	static constexpr int32 MaxPoints = 8192;
	static constexpr double MinimumSpacingCm = 10.0;
	static constexpr double MaximumSpacingCm = 100000.0;

	[[nodiscard]] static bool BuildMeshes(const FVoxelLandTerrainPlacementInput& Input,
		TFunctionRef<bool()> IsCancelled, TArray<FVoxelLandTerrainPlacementMesh>& Out);
	[[nodiscard]] static bool Sample(const FVoxelLandTerrainPlacementInput& Input, TFunctionRef<bool()> IsCancelled,
		double SpacingCm, int32 Seed, double MaximumSlopeDegrees, TArray<FVoxelLandTerrainPlacementPoint>& Out);
	[[nodiscard]] static bool Project(TConstArrayView<FVoxelLandTerrainPlacementMesh> Meshes,
		const FVector& LogicalStart, const FVector& Direction, double DistanceCm, bool bGroundOnly,
		FVoxelLandTerrainPlacementPoint& Out);
	[[nodiscard]] static bool CanProject(const FVoxelLandCollisionField& Field, const FVector& LogicalStart,
		const FVector& Direction, double DistanceCm);

private:
	static uint64 Mix(uint64 Value);
	static FVector2d GridPosition(const FInt64Point& Key, double CellWidth, uint64 Seed);
	static bool KeyLess(const FInt64Point& A, const FInt64Point& B);
	static FBox BoundsFor(const FVoxelLandTerrainPlacementMesh& Mesh);
	static FVector InterpolateNormal(const FVoxelLandMeshData& Mesh, int32 Index, double U, double V);
	static void AccumulateMesh(const FVoxelLandTerrainPlacementMesh& Mesh, const FBox& Bounds, double CellWidth,
		uint64 Seed, double MinimumNormalZ, TMap<FInt64Point, FVoxelLandTerrainPlacementPoint>& Points);
	static void AccumulateTriangle(const FVoxelLandTerrainPlacementMesh& Mesh, int32 Triangle, const FBox& Bounds,
		double CellWidth, uint64 Seed, double MinimumNormalZ,
		TMap<FInt64Point, FVoxelLandTerrainPlacementPoint>& Points);
};
