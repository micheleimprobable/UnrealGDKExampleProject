// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

using UnrealBuildTool;

public class GDKShooter : ModuleRules
{
	public GDKShooter(ReadOnlyTargetRules Target) : base(Target)
	{
		//PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PCHUsage = PCHUsageMode.NoSharedPCHs;
		PrivatePCHHeaderFile = "my_pch.h";
		CppStandard = CppStandardVersion.Cpp17;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"InputCore",
				"Sockets",
				"OnlineSubsystemUtils",
				"PhysXVehicles",
				"UMG",
				"Slate",
				"SlateCore",
				"SpatialGDK",
                "Json",
                "HTTP",
                "AIModule"
			});
	}
}
