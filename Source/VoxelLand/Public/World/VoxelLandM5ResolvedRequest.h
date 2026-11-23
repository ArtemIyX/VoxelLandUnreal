#pragma once

#include "CoreMinimal.h"

// Finite request key only. This is not a plan or evidence of safe publication.
class VOXELLAND_API FVoxelLandM5ResolvedRequest final
{
public:
	bool IsDisabled() const { return bDisabled; }
	TConstArrayView<uint8> GetIdentity() const { return Identity; }

private:
	friend class FVoxelLandM5RequestResolver;
	bool bDisabled = false;
	TArray<uint8> Identity;
};
