using UnrealBuildTool;

public class VoxelLandSampleProviderEditor : ModuleRules
{
    public VoxelLandSampleProviderEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PrivateDependencyModuleNames.AddRange(new[] { "Core", "VoxelLandData", "VoxelLandEditor", "VoxelLandSampleProvider" });
    }
}
