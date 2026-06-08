using UnrealBuildTool;

public class CenterpieceFlasher : ModuleRules
{
    public CenterpieceFlasher(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "InputCore",
            "DeveloperSettings"
        });

        PrivateDependencyModuleNames.AddRange(new[]
        {
            "Slate",
            "SlateCore",
            "EditorStyle",
            "UnrealEd",
            "LevelEditor",
            "DesktopPlatform",
            "Projects",
            "MainFrame"
        });

        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            // Win32 HID stack: SetupAPI for enumeration, hid.lib for HidD_*/HidP_* helpers.
            PublicSystemLibraries.AddRange(new[] { "setupapi.lib", "hid.lib" });
        }
    }
}
