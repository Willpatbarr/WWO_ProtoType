using UnrealBuildTool;

public class WhatWasOurs : ModuleRules
{
	public WhatWasOurs(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		bUseUnity = false; // files use same-named helpers in anonymous namespaces

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput",
			"UMG", "Slate", "SlateCore", "AIModule", "NavigationSystem",
			"Json", "JsonUtilities", "ApplicationCore", "ProceduralMeshComponent"
		});

		PublicIncludePaths.Add(ModuleDirectory);

		if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			PublicFrameworks.Add("GameController"); // the DualSense Create button (see UI/WWOMenuCursor)
		}
	}
}
