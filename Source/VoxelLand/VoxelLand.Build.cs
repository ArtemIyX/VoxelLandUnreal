using UnrealBuildTool;
public class VoxelLand : ModuleRules
{
    public VoxelLand(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "VoxelLandData", "VoxelLandAlgo" });
        PrivateDependencyModuleNames.Add("InputCore");
        if (Target.Type != TargetType.Server)
        {
            PrivateDependencyModuleNames.Add("VoxelLandRender");
        }
    }
}
