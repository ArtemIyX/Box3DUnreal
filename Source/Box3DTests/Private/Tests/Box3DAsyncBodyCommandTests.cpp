#include "World/Box3DAsyncSubsystem.h"
#include "Box3DTestHelpers.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBox3DAsyncBodyCommandTest,
	"Box3DUnreal.Async.BodyCommands", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBox3DAsyncBodyCommandTest::RunTest(const FString& Parameters)
{
	Box3DTest::FTestWorld World(TEXT("AsyncBodyCommands"));
	UBox3DAsyncSubsystem* Physics = World.World->GetSubsystem<UBox3DAsyncSubsystem>();
	if (!TestNotNull(TEXT("Async subsystem"), Physics)) return false;
	FBox3DAsyncCreationConsumer Owner;
	if (!TestTrue(TEXT("Creation consumer"), Physics->TryRegisterCreationConsumer(Owner))) return false;
	FBox3DAsyncBodyDesc Desc;
	Desc.Motion = EBox3DAsyncBodyMotion::Dynamic;
	Desc.GravityScale = 0.0f;
	Desc.Shapes.AddDefaulted();
	FBox3DAsyncBodyHandle Body;
	if (!TestTrue(TEXT("Body accepted"), Physics->TryCreateBody(Owner, Desc, Body))) return false;
	FBox3DAsyncCreationResult Result;
	const double Deadline = FPlatformTime::Seconds() + 5.0;
	bool bCreated = false;
	while (FPlatformTime::Seconds() < Deadline && !(bCreated = Physics->TryTakeCreationResult(Owner, Body, Result)))
		FPlatformProcess::Sleep(0.001f);
	if (!TestTrue(TEXT("Body creation completes"), bCreated && Result.bSucceeded)) return false;

	FBox3DAsyncBodyCommand Command;
	Command.Type = EBox3DAsyncBodyCommandType::SetLinearVelocity;
	Command.Value = FVector(100.0, 0.0, 0.0);
	FBox3DAsyncBodyHandle Foreign = Body;
	Foreign.World = FGuid::NewGuid();
	TestFalse(TEXT("Foreign world is rejected"), Physics->TrySubmitBodyCommand(Foreign, Command));
	FBox3DAsyncBodyCommand Invalid = Command;
	Invalid.Value.X = std::numeric_limits<double>::infinity();
	TestFalse(TEXT("Non-finite input is rejected"), Physics->TrySubmitBodyCommand(Body, Invalid));
	if (!TestTrue(TEXT("Velocity command accepted"), Physics->TrySubmitBodyCommand(Body, Command))) return false;
	Command.Type = EBox3DAsyncBodyCommandType::AddLinearVelocity;
	Command.Value = FVector(100.0, 0.0, 0.0);
	if (!TestTrue(TEXT("Add velocity accepted"), Physics->TrySubmitBodyCommand(Body, Command))) return false;

	bool bMoved = false;
	bool bObservedVelocity = false;
	FBox3DAsyncBodyState State;
	TestFalse(TEXT("Foreign state is rejected"), Physics->TryGetLatestBodyState(Foreign, State));
	const double MotionDeadline = FPlatformTime::Seconds() + 3.0;
	while (FPlatformTime::Seconds() < MotionDeadline && (!bMoved || !bObservedVelocity))
	{
		FBox3DAsyncPoseFrame Frame;
		if (Physics->TryGetLatestPoseFrame(Frame))
			for (const FBox3DAsyncPose& Pose : Frame.Poses)
				if (Pose.BodyId == Body.Id && Pose.Transform.GetLocation().X > 10.0) bMoved = true;
		if (Physics->TryGetLatestBodyState(Body, State) && FMath::IsNearlyEqual(State.LinearVelocity.X, 200.0, 0.1))
			bObservedVelocity = State.World == Body.World && State.BodyId == Body.Id && State.CaptureSerial > 0 && State.Mass > 0.0f;
		if (!bMoved || !bObservedVelocity) FPlatformProcess::Sleep(0.001f);
	}
	TestTrue(TEXT("Worker applies submitted velocity to body"), bMoved);
	TestTrue(TEXT("Published body state has velocity, mass and identity"), bObservedVelocity);
	TestTrue(TEXT("Body removal accepted"), Physics->TryRemoveBody(Body));
	TestFalse(TEXT("Commands after removal are rejected"), Physics->TrySubmitBodyCommand(Body, Command));
	TestFalse(TEXT("Removed state is rejected"), Physics->TryGetLatestBodyState(Body, State));
	Physics->UnregisterCreationConsumer(Owner);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBox3DAsyncKinematicBodyTest,
	"Box3DUnreal.Async.KinematicBody", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBox3DAsyncKinematicBodyTest::RunTest(const FString& Parameters)
{
	Box3DTest::FTestWorld World(TEXT("AsyncKinematicBody"));
	UBox3DAsyncSubsystem* Physics = World.World->GetSubsystem<UBox3DAsyncSubsystem>();
	if (!TestNotNull(TEXT("Async subsystem"), Physics)) return false;
	FBox3DAsyncCreationConsumer Owner;
	if (!TestTrue(TEXT("Creation consumer"), Physics->TryRegisterCreationConsumer(Owner))) return false;
	FBox3DAsyncBodyDesc Desc;
	Desc.Motion = EBox3DAsyncBodyMotion::Kinematic;
	Desc.Shapes.AddDefaulted();
	FBox3DAsyncBodyHandle Body;
	if (!TestTrue(TEXT("Kinematic body admitted"), Physics->TryCreateBody(Owner, Desc, Body))) return false;
	FBox3DAsyncCreationResult Result;
	bool bCreated = false;
	const double Deadline = FPlatformTime::Seconds() + 5.0;
	while (FPlatformTime::Seconds() < Deadline && !(bCreated = Physics->TryTakeCreationResult(Owner, Body, Result)))
		FPlatformProcess::Sleep(0.001f);
	if (!TestTrue(TEXT("Kinematic body created"), bCreated && Result.bSucceeded)) return false;
	FBox3DAsyncBodyCommand Target;
	Target.Type = EBox3DAsyncBodyCommandType::SetKinematicTarget;
	Target.Target.SetLocation(FVector(100, 0, 0));
	TestFalse(TEXT("Zero duration is rejected"), Physics->TrySubmitBodyCommand(Body, Target));
	Target.TargetDuration = 1.0f;
	if (!TestTrue(TEXT("Kinematic target accepted"), Physics->TrySubmitBodyCommand(Body, Target))) return false;
	bool bMoved = false;
	const double MotionDeadline = FPlatformTime::Seconds() + 3.0;
	while (FPlatformTime::Seconds() < MotionDeadline && !bMoved)
	{
		FBox3DAsyncPoseFrame Frame;
		if (Physics->TryGetLatestPoseFrame(Frame))
			for (const FBox3DAsyncPose& Pose : Frame.Poses)
				if (Pose.BodyId == Body.Id && Pose.Transform.GetLocation().X > 10.0) bMoved = true;
		if (!bMoved) FPlatformProcess::Sleep(0.001f);
	}
	TestTrue(TEXT("Kinematic pose is published and moves"), bMoved);
	TestTrue(TEXT("Remove kinematic body"), Physics->TryRemoveBody(Body));
	Physics->UnregisterCreationConsumer(Owner);
	return true;
}
#endif
