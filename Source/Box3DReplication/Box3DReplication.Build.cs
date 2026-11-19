using UnrealBuildTool;

public class Box3DReplication : ModuleRules
{
    public Box3DReplication(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "Box3DAsync", "NetCore", "IrisCore" });
        PublicDependencyModuleNames.Add("DeveloperSettings");
        SetupIrisSupport(Target);
    }
}
