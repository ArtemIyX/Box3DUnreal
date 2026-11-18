using System.IO;
using UnrealBuildTool;

public class Box3DTests : ModuleRules
{
	public Box3DTests(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"Box3DCore",
			"Box3DAsync",
			"Box3DRuntime",
			"PhysicsCore",
			"ProceduralMeshComponent",
		});
		PrivateIncludePaths.Add(Path.Combine(ModuleDirectory, "../Box3DRuntime/Private"));
		PrivateIncludePaths.Add(Path.Combine(ModuleDirectory, "../Box3DAsync/Private"));
	}
}
