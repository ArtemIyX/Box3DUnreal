using UnrealBuildTool;

public class Box3DAsync : ModuleRules
{
    public Box3DAsync(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
                "CoreUObject",
                "Engine",
            }
        );

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "Box3DCore",
                "Box3DRuntime",
                "PhysicsCore",
            }
        );
    }
}
