#pragma once

#include "CoreMinimal.h"
#include "Data/World/Streaming/VoxelLandWorldCancel.h"
#include "Data/Field/Chunks/VoxelLandChunkData.h"

namespace VoxelLandWorldPrivate
{
struct FRecord
{
	uint64 Ticket = 0;
	bool bExplicit = false, bDemand = false, bQueued = false, bRunning = false, bFailed = false;
	// A collision-only completion may still need the ordinary LOD 0 render job when it enters camera demand.
	bool bNeedsRender = false;
	uint8 CollisionOwners = 0;
	uint8 CollisionRetries = 0;
	double CollisionRetryAfter = 0.0;

	static constexpr uint8 MaxCollisionRetries = 2;
	static constexpr double InitialCollisionRetryDelay = 0.25;

	void DeferCollisionRetry(double Now)
	{
		CollisionRetryAfter = Now + InitialCollisionRetryDelay * double(1 << CollisionRetries);
	}

	void QueueDueCollisionRetry(double Now)
	{
		if (CollisionOwners == 0 || Data || !bFailed || bRunning || CollisionRetries >= MaxCollisionRetries
			|| Now < CollisionRetryAfter)
		{
			return;
		}
		++CollisionRetries;
		bFailed = false;
		bQueued = true;
		Error.Reset();
	}

	bool HasPinnedDemand() const { return bExplicit || CollisionOwners != 0; }
	bool HasDemand() const { return bDemand || HasPinnedDemand(); }
	int64 MeshBytes = 0;
	int32 Vertices = 0, Triangles = 0;
	// Present only for a failed asynchronous request; retained while the record is resident.
	FString Error;
	TSharedPtr<FCancel, ESPMode::ThreadSafe> Cancel;
	TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe> Data;
};
}
