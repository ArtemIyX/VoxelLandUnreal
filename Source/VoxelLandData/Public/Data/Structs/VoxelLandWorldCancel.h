#pragma once

#include "CoreMinimal.h"

namespace VoxelLandWorldPrivate
{
struct FCancel
{
	TAtomic<bool> bCancelled{false};
};
}
