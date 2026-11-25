#pragma once

#include "Templates/SharedPointer.h"

class FVoxelLandM5Budget;
template <typename T> using TVoxelLandM5ConstPtr = TSharedPtr<const T, ESPMode::ThreadSafe>;
template <typename T> using TVoxelLandM5ConstRef = TSharedRef<const T, ESPMode::ThreadSafe>;
using FVoxelLandM5BudgetRef = TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe>;
