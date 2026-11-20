using UnrealBuildTool;

public class VoxelLandEditor : ModuleRules
{
    public VoxelLandEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
                "CoreUObject",
                "VoxelLandData",
            }
        );

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "Engine",
                "RenderCore",
                "Slate",
                "SlateCore",
                "InputCore",
                "VoxelLandAlgo",
                "VoxelLand",
                "VoxelLandRender",
                "UnrealEd",
                "AssetTools",
                "AssetRegistry",
                "GraphEditor",
                "PropertyEditor",
                "ToolMenus",
                "ApplicationCore",
                "EditorFramework",
                "AdvancedPreviewScene"
            }
        );
    }
}
