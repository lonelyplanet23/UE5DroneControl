// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class UE5DroneControl : ModuleRules
{
	public UE5DroneControl(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"AIModule",
			"NavigationSystem",
			"StateTreeModule",
			"GameplayStateTreeModule",
			"Niagara",
			"UMG",
			"Sockets",
            "Networking",
			"Slate"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });

		PublicIncludePaths.AddRange(new string[] {
			"UE5DroneControl",
			"UE5DroneControl/Variant_Strategy",
			"UE5DroneControl/Variant_Strategy/UI",
			"UE5DroneControl/Variant_TwinStick",
			"UE5DroneControl/Variant_TwinStick/AI",
			"UE5DroneControl/Variant_TwinStick/Gameplay",
			"UE5DroneControl/Variant_TwinStick/UI"
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
