#pragma once

// The last grid reader may release the lease on any thread. Derived reservation
// destructors must be thread-safe and must not reference destroyed owners.
class FVoxelLandM4GridLease
{
public:
	virtual ~FVoxelLandM4GridLease() = default;
};
