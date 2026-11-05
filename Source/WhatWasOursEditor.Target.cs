using UnrealBuildTool;

public class WhatWasOursEditorTarget : TargetRules
{
	public WhatWasOursEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("WhatWasOurs");
	}
}
