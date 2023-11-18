using UnrealBuildTool;

public class VirtualizationPlus : ModuleRules
{
	public VirtualizationPlus(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDependencyModuleNames.AddRange(new[]
		{
			"Core",
			"Virtualization",
			"HTTP",
			"SSL"
		});

		AddEngineThirdPartyPrivateStaticDependencies(Target, "OpenSSL");
	}
}