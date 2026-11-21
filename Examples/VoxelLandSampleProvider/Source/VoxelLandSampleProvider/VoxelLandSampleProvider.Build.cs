using UnrealBuildTool;

public class VoxelLandSampleProvider : ModuleRules
{
    public VoxelLandSampleProvider(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core" });
        PrivateDependencyModuleNames.AddRange(new[] { "VoxelLandAlgo", "VoxelLandData" });
    }
}
