using UnrealBuildTool;

public class BPeek : BPeekBuild
{
    public BPeek(ReadOnlyTargetRules Target) : base(Target)
    {
        PublicIncludePaths.Add(ModuleDirectory);
        PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "UMG", "UMGEditor", "SlateCore", "Slate", "Json", "JsonUtilities", "UnrealEd", "AssetRegistry", "BlueprintGraph", "LevelSequence", "MovieScene", "DataValidation", "DeveloperSettings", "Projects", "BPeekCompat" });
    }
}
