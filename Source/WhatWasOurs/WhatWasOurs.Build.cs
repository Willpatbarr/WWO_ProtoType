using UnrealBuildTool;

public class WhatWasOurs : ModuleRules
{
	public WhatWasOurs(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// Files the game reads at runtime from Config/ (not assets), staged into packaged builds.
		// ShippedSettings.json is a copy of your saved Settings values (Tools/package_mac.sh makes it),
		// the defaults a packaged build starts from; ShippedKeybinds.json the same for key and button bindings.
		foreach (string Name in new string[] { "WeaponModels.json", "WeaponMods.json", "CharacterModels.json", "DefaultLoadout.json", "Sounds.json", "ShippedSettings.json", "ShippedKeybinds.json" })
		{
			RuntimeDependencies.Add("$(ProjectDir)/Config/" + Name, StagedFileType.UFS);
		}
		// Windows: MSVC's shadow checks (errors under these build settings) also count a local that
		// has the name of a class member, which the UI code does all over (a "Slot" local in a
		// widget, where UWidget has a Slot member). Clang's check doesn't, and stays on for Mac builds.
		if (Target.Platform.IsInGroup(UnrealPlatformGroup.Windows))
		{
			CppCompileWarningSettings.ShadowVariableWarningLevel = WarningLevel.Off;
		}
		bUseUnity = false; // files use same-named helpers in anonymous namespaces

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core", "CoreUObject", "Engine", "RHI", "RenderCore", "InputCore", "EnhancedInput",
			"UMG", "Slate", "SlateCore", "AIModule", "NavigationSystem",
			"Json", "JsonUtilities", "ApplicationCore", "ProceduralMeshComponent", "IKRig",
			"NetCore", "OnlineSubsystem", "OnlineSubsystemUtils", "Sockets"
		});

		PublicIncludePaths.Add(ModuleDirectory);

		if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			PublicFrameworks.Add("GameController"); // the DualSense Create button (see UI/WWOMenuCursor)
		}
	}
}
