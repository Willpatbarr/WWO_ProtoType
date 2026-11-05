using UnrealBuildTool;

public class WhatWasOursTarget : TargetRules
{
	public WhatWasOursTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("WhatWasOurs");
	}
}
