#pragma once

#include "CoreMinimal.h"

class UPCGGraph;
class UClass;

class FVoxelLandPCGGraphPolicy
{
public:
	[[nodiscard]] static bool Validate(const UPCGGraph& Graph, FString& OutError);

private:
	static bool IsSupported(const UClass* SettingsClass);
};
