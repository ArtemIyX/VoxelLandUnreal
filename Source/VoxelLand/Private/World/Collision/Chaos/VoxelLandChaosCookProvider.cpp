#include "VoxelLandChaosCookProvider.h"

#include "Data/Meshing/VoxelLandChaosMesh.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

void UVoxelLandChaosCookProvider::SetMesh(TSharedRef<const FVoxelLandChaosMesh, ESPMode::ThreadSafe> InMesh)
{
	check(IsInGameThread() && !Mesh);
	Mesh = MoveTemp(InMesh);
}

TSharedPtr<const FVoxelLandChaosMesh, ESPMode::ThreadSafe> UVoxelLandChaosCookProvider::GetMesh() const
{
	return Mesh;
}

bool UVoxelLandChaosCookProvider::GetPhysicsTriMeshData(FTriMeshCollisionData* OutData, bool bInUseAllTriData)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(VoxelLand_ChaosCopyCookInput);
	if (!OutData || !ContainsPhysicsTriMeshData(bInUseAllTriData))
	{
		return false;
	}

	OutData->Vertices = Mesh->Vertices;
	OutData->Indices.Reset(Mesh->Indices.Num() / 3);
	for (int32 Index = 0; Index < Mesh->Indices.Num(); Index += 3)
	{
		FTriIndices Triangle;
		Triangle.v0 = static_cast<int32>(Mesh->Indices[Index]);
		Triangle.v1 = static_cast<int32>(Mesh->Indices[Index + 1]);
		Triangle.v2 = static_cast<int32>(Mesh->Indices[Index + 2]);
		OutData->Indices.Add(Triangle);
	}
	// Surface Nets already winds triangles toward positive density. Preserve the Chaos contact normal.
	OutData->bFlipNormals = false;
	OutData->bDeformableMesh = false;
	OutData->bFastCook = true;
	return true;
}

bool UVoxelLandChaosCookProvider::ContainsPhysicsTriMeshData(bool bInUseAllTriData) const
{
	return Mesh && !Mesh->Vertices.IsEmpty() && !Mesh->Indices.IsEmpty();
}
