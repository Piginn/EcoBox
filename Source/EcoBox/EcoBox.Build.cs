// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class EcoBox : ModuleRules
{
	public EcoBox(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
	
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput" });

		// DeveloperSettings：让 UEcoBoxSettings 能出现在 项目设置 → Game 里
		// ApplicationCore：FDisplayMetrics（查询显示器原生分辨率，做分辨率自适应）
		PrivateDependencyModuleNames.AddRange(new string[] { "UMG", "Slate", "SlateCore", "DeveloperSettings", "ApplicationCore" });

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });
		
		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
