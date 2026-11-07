using UnrealBuildTool;

public class WhatWasOurs : ModuleRules
{
	public WhatWasOurs(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// Files the game reads at runtime from Config/ (not assets), staged into packaged builds.
		// ShippedSettings.json is a copy of your saved Settings values (Tools/package_mac.sh makes it),
		// the defaults a packaged build starts from.
		foreach (string Name in new string[] { "WeaponModels.json", "WeaponMods.json", "CharacterModels.json", "DefaultLoadout.json", "ShippedSettings.json" })
		{
			RuntimeDependencies.Add("$(ProjectDir)/Config/" + Name, StagedFileType.UFS);
		}
		bUseUnity = false; // files use same-named helpers in anonymous namespaces

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core", "CoreUObject", "Engine", "RHI", "RenderCore", "InputCore", "EnhancedInput",
			"UMG", "Slate", "SlateCore", "AIModule", "NavigationSystem",
			"Json", "JsonUtilities", "ApplicationCore", "ProceduralMeshComponent", "IKRig",
			"NetCore", "OnlineSubsystem", "OnlineSubsystemUtils"
		});

		PublicIncludePaths.Add(ModuleDirectory);

		if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			PublicFrameworks.Add("GameController"); // the DualSense Create button (see UI/WWOMenuCursor)
		}
	}
}
