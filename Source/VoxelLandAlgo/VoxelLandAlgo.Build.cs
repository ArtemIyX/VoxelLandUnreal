using UnrealBuildTool;

public class VoxelLandAlgo : ModuleRules
{
    public VoxelLandAlgo(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
        FPSemantics = FPSemanticsMode.Precise;
        PublicDependencyModuleNames.AddRange(new string[] { "Core", "VoxelLandData" });
        PrivateDependencyModuleNames.AddRange(new string[] { "Projects", "UnrealFastNoise2" });
    }
}
