#include "VoxelLandRender.h"

#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "ShaderCore.h"

#define LOCTEXT_NAMESPACE "FVoxelLandRenderModule"

void FVoxelLandRenderModule::StartupModule()
{
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("VoxelLand"));
	if (Plugin.IsValid())
	{
		const FString Directory = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Shaders"));
		if (FPaths::DirectoryExists(Directory))
		{
			AddShaderSourceDirectoryMapping(TEXT("/Plugin/VoxelLand"), Directory);
		}
	}
}

void FVoxelLandRenderModule::ShutdownModule() {}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FVoxelLandRenderModule, VoxelLandRender)