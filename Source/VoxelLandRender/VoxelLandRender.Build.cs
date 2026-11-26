using UnrealBuildTool;
public class VoxelLandRender : ModuleRules
{
    public VoxelLandRender(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "VoxelLandData", "VoxelLandAlgo" });
        PrivateDependencyModuleNames.AddRange(new[] { "RenderCore", "RHI", "Projects" });
    }
}
