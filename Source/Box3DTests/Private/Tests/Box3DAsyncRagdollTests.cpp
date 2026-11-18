#include "World/Box3DAsyncRagdollSubsystem.h"
#include "Box3DTestHelpers.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBox3DAsyncRagdollGroupTest,
	"Box3DUnreal.Async.RagdollGroup", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBox3DAsyncRagdollGroupTest::RunTest(const FString& Parameters)
{
	Box3DTest::FTestWorld World(TEXT("AsyncRagdollGroup"));
	UBox3DAsyncRagdollSubsystem* Ragdolls = World.World->GetSubsystem<UBox3DAsyncRagdollSubsystem>();
	if (!TestNotNull(TEXT("Ragdoll subsystem"), Ragdolls)) return false;
	FBox3DAsyncRagdollDesc Desc;
	for (int32 Index = 0; Index < 2; ++Index)
	{
		FBox3DAsyncRagdollBoneDesc& Bone = Desc.Bones.AddDefaulted_GetRef();
		Bone.BoneName = Index == 0 ? TEXT("Root") : TEXT("Limb");
		Bone.Body.Motion = EBox3DAsyncBodyMotion::Dynamic;
		Bone.Body.GravityScale = 0.0f;
		Bone.Body.InitialVelocity = FVector(150, 0, 0);
		Bone.Body.Transform.SetLocation(FVector(Index * 100.0, 0, 0));
		Bone.Body.Shapes.AddDefaulted();
	}
	FBox3DAsyncRagdollLinkDesc& Link = Desc.Links.AddDefaulted_GetRef();
	Link.Parent = 0; Link.Child = 1;
	Link.Joint.Type = EBox3DAsyncJointType::Spherical;
	Link.Joint.LocalFrameA.SetLocation(FVector(50, 0, 0));
	Link.Joint.LocalFrameB.SetLocation(FVector(-50, 0, 0));
	FBox3DAsyncRagdollHandle Group;
	if (!TestTrue(TEXT("Group admitted"), Ragdolls->TryCreateRagdoll(Desc, Group))) return false;
	const double Deadline = FPlatformTime::Seconds() + 8.0;
	while (FPlatformTime::Seconds() < Deadline)
	{
		Ragdolls->Tick(0.0f);
		if (Ragdolls->GetRagdollStatus(Group) == EBox3DAsyncRagdollStatus::Active) break;
		FPlatformProcess::Sleep(0.001f);
	}
	if (!TestTrue(TEXT("Group created"), Ragdolls->GetRagdollStatus(Group) == EBox3DAsyncRagdollStatus::Active)) return false;
	FBox3DAsyncRagdollPose Pose;
	bool bReady = false;
	while (FPlatformTime::Seconds() < Deadline && !(bReady = Ragdolls->TryGetLatestRagdollPose(Group, Pose)))
		FPlatformProcess::Sleep(0.001f);
	if (TestTrue(TEXT("Coherent group pose"), bReady))
	{
		TestEqual(TEXT("All bones present"), Pose.WorldTransforms.Num(), 2);
		TestEqual(TEXT("Names retained"), Pose.BoneNames.Num(), 2);
		TestTrue(TEXT("Sample stamped"), Pose.CaptureSerial > 0 && Pose.World == Group.World);
	}
	FBox3DAsyncBodyState BoneState;
	bool bBoneStateReady = false;
	while (FPlatformTime::Seconds() < Deadline && !(bBoneStateReady = Ragdolls->TryGetBoneState(Group, TEXT("Limb"), BoneState)
		&& BoneState.bEnabled && BoneState.LinearVelocity.X > 50.0))
		FPlatformProcess::Sleep(0.001f);
	TestTrue(TEXT("Ragdoll activation preserves initial velocity and publishes state"), bBoneStateReady && BoneState.World == Group.World && BoneState.Mass > 0.0f);
	TestFalse(TEXT("Missing bone state is rejected"), Ragdolls->TryGetBoneState(Group, TEXT("Missing"), BoneState));
	FBox3DAsyncBodyCommand Impulse;
	Impulse.Type = EBox3DAsyncBodyCommandType::ApplyImpulse;
	Impulse.Value = FVector(100, 0, 0);
	TestTrue(TEXT("Bone impulse admitted"), Ragdolls->TrySubmitBoneCommand(Group, TEXT("Limb"), Impulse));
	TestTrue(TEXT("Group impulse admitted"), Ragdolls->TryApplyRagdollImpulse(Group, FVector(100, 0, 0)));
	TestFalse(TEXT("Unknown bone rejected"), Ragdolls->TrySubmitBoneCommand(Group, TEXT("Missing"), Impulse));
	TestTrue(TEXT("Group removal admitted"), Ragdolls->TryRemoveRagdoll(Group));
	const double RemoveDeadline = FPlatformTime::Seconds() + 8.0;
	while (FPlatformTime::Seconds() < RemoveDeadline && Ragdolls->GetRagdollStatus(Group) == EBox3DAsyncRagdollStatus::Removing)
	{
		Ragdolls->Tick(0.0f);
		FPlatformProcess::Sleep(0.001f);
	}
	TestTrue(TEXT("Group removed"), Ragdolls->GetRagdollStatus(Group) == EBox3DAsyncRagdollStatus::Removed);
	TestTrue(TEXT("Release group identity"), Ragdolls->ForgetRagdoll(Group));
	FBox3DAsyncRagdollDesc Broken = Desc;
	Broken.Bones[1].Body.Shapes.Reset();
	FBox3DAsyncBodyShapeDesc& Flat = Broken.Bones[1].Body.Shapes.AddDefaulted_GetRef();
	Flat.Kind = EBox3DAsyncBodyShapeKind::Convex;
	Flat.ConvexPoints = {FVector(-20, -20, 0), FVector(20, -20, 0), FVector(20, 20, 0), FVector(-20, 20, 0)};
	FBox3DAsyncRagdollHandle Failed;
	if (!TestTrue(TEXT("Degenerate group admitted for worker validation"), Ragdolls->TryCreateRagdoll(Broken, Failed))) return false;
	const double FailureDeadline = FPlatformTime::Seconds() + 8.0;
	while (FPlatformTime::Seconds() < FailureDeadline && Ragdolls->GetRagdollStatus(Failed) != EBox3DAsyncRagdollStatus::Failed)
	{
		Ragdolls->Tick(0.0f);
		FPlatformProcess::Sleep(0.001f);
	}
	TestTrue(TEXT("Failed group rolls back before reporting failure"), Ragdolls->GetRagdollStatus(Failed) == EBox3DAsyncRagdollStatus::Failed);
	TestTrue(TEXT("Release failed identity"), Ragdolls->ForgetRagdoll(Failed));
	return true;
}
#endif
