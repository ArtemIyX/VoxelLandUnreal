using UnrealBuildTool;

public class VoxelLandPCG : ModuleRules
{
    public VoxelLandPCG(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "PCG", "VoxelLand", "VoxelLandData", "VoxelLandAlgo" });
    }
}
