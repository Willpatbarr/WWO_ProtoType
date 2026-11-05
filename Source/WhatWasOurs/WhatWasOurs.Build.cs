using UnrealBuildTool;

public class WhatWasOurs : ModuleRules
{
	public WhatWasOurs(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput",
			"UMG", "Slate", "SlateCore", "AIModule", "NavigationSystem",
			"Json", "JsonUtilities", "ApplicationCore"
		});

		PublicIncludePaths.Add(ModuleDirectory);
	}
}
