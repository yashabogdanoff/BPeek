using UnrealBuildTool;

public class BPeekEditor : BPeekBuild
{
    public BPeekEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "Slate", "SlateCore", "EditorStyle", "UnrealEd", "LevelEditor", "ContentBrowser", "ToolMenus", "Projects", "Settings", "BPeekCompat" });
        PrivateDependencyModuleNames.AddRange(new string[] { "BPeek" });
    }
}