#pragma once

#include "PCGElement.h"

class FVoxelLandPCGGetVoxelTerrainElement final : public IPCGElement
{
public:
	virtual bool IsCacheable(const UPCGSettings*) const override { return false; }
	virtual bool CanExecuteOnlyOnMainThread(FPCGContext*) const override { return true; }

protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
};
