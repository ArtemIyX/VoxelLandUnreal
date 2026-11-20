using UnrealBuildTool;

public class VoxelLandUncooked : ModuleRules
{
    public VoxelLandUncooked(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
            }
        );

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "CoreUObject",
                "Engine",
                "VoxelLandData",
                "VoxelLandAlgo",
                "VoxelLandRender",
                "Slate",
                "SlateCore"
            }
        );
    }
}
