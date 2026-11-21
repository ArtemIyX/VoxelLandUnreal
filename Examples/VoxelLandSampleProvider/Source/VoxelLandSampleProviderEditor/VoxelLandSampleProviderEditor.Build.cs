using UnrealBuildTool;

public class VoxelLandSampleProviderEditor : ModuleRules
{
    public VoxelLandSampleProviderEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PrivateDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "UnrealEd", "VoxelLandAlgo", "VoxelLandData", "VoxelLandEditor", "VoxelLandSampleProvider" });
    }
}
