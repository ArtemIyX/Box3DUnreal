#include "World/Box3DAsyncSubsystem.h"
#include "Box3DTestHelpers.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBox3DAsyncJointLifecycleTest,
	"Box3DUnreal.Async.JointLifecycle", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBox3DAsyncJointLifecycleTest::RunTest(const FString& Parameters)
{
	Box3DTest::FTestWorld World(TEXT("AsyncJointLifecycle"));
	UBox3DAsyncSubsystem* Physics = World.World->GetSubsystem<UBox3DAsyncSubsystem>();
	if (!TestNotNull(TEXT("Physics"), Physics)) return false;
	FBox3DAsyncCreationConsumer Consumer;
	if (!TestTrue(TEXT("Consumer"), Physics->TryRegisterCreationConsumer(Consumer))) return false;
	FBox3DAsyncBodyDesc Desc;
	Desc.Shapes.AddDefaulted();
	FBox3DAsyncBodyHandle BodyA, BodyB;
	if (!TestTrue(TEXT("Static body admitted"), Physics->TryCreateBody(Consumer, Desc, BodyA))) return false;
	Desc.Motion = EBox3DAsyncBodyMotion::Dynamic;
	Desc.Transform.SetLocation(FVector(200, 0, 0));
	if (!TestTrue(TEXT("Dynamic body admitted"), Physics->TryCreateBody(Consumer, Desc, BodyB))) return false;
	FBox3DAsyncJointDesc JointDesc;
	JointDesc.BodyA = BodyA;
	JointDesc.BodyB = BodyB;
	JointDesc.DistanceLength = 200.0f;
	FBox3DAsyncJointDesc Invalid = JointDesc;
	Invalid.BodyB.World = FGuid::NewGuid();
	FBox3DAsyncJointHandle Joint;
	TestFalse(TEXT("Wrong-world body rejected"), Physics->TryCreateJoint(Invalid, Joint));
	for (uint8 Type = 0; Type <= uint8(EBox3DAsyncJointType::Wheel); ++Type)
	{
		JointDesc.Type = static_cast<EBox3DAsyncJointType>(Type);
		if (!TestTrue(TEXT("Joint admitted"), Physics->TryCreateJoint(JointDesc, Joint))) return false;
		const double Deadline = FPlatformTime::Seconds() + 5.0;
		while (FPlatformTime::Seconds() < Deadline && Physics->GetJointStatus(Joint) == EBox3DAsyncJointStatus::Pending)
			FPlatformProcess::Sleep(0.001f);
		TestTrue(TEXT("Joint created on worker"), Physics->GetJointStatus(Joint) == EBox3DAsyncJointStatus::Active);
		TestTrue(TEXT("Joint settings update admitted"), Physics->TryUpdateJoint(Joint, JointDesc));
		if (JointDesc.Type == EBox3DAsyncJointType::Spherical)
		{
			FBox3DAsyncJointDesc WideCone = JointDesc;
			WideCone.bEnableConeLimit = true; WideCone.ConeAngle = 2.0f;
			TestFalse(TEXT("Native runtime cone limit above 90 degrees rejected"), Physics->TryUpdateJoint(Joint, WideCone));
		}
		if (JointDesc.Type == EBox3DAsyncJointType::Revolute)
		{
			FBox3DAsyncJointDesc BadTarget = JointDesc;
			BadTarget.Target = 4.0f;
			TestFalse(TEXT("Native revolute target outside pi rejected"), Physics->TryUpdateJoint(Joint, BadTarget));
		}
		FBox3DAsyncJointDesc WrongType = JointDesc;
		WrongType.Type = JointDesc.Type == EBox3DAsyncJointType::Weld ? EBox3DAsyncJointType::Distance : EBox3DAsyncJointType::Weld;
		TestFalse(TEXT("Changing joint type rejected"), Physics->TryUpdateJoint(Joint, WrongType));
		FBox3DAsyncJointState State;
		bool bStateReady = false;
		while (FPlatformTime::Seconds() < Deadline && !(bStateReady = Physics->TryGetLatestJointState(Joint, State)))
			FPlatformProcess::Sleep(0.001f);
		TestTrue(TEXT("Completed joint state is published"), bStateReady && State.World == Joint.World && State.JointId == Joint.Id && State.CaptureSerial > 0);
		FBox3DAsyncJointCommand Control;
		Control.Type = EBox3DAsyncJointCommandType::SetMotorSpeed;
		Control.Scalar = 1.0f;
		const bool bSupportsSpeed = JointDesc.Type == EBox3DAsyncJointType::Distance || JointDesc.Type == EBox3DAsyncJointType::Revolute
			|| JointDesc.Type == EBox3DAsyncJointType::Prismatic || JointDesc.Type == EBox3DAsyncJointType::Wheel;
		TestEqual(TEXT("Motor control rejects incompatible types"), Physics->TrySubmitJointCommand(Joint, Control), bSupportsSpeed);
		if (!TestTrue(TEXT("Joint removal admitted"), Physics->TryRemoveJoint(Joint))) return false;
		while (FPlatformTime::Seconds() < Deadline && Physics->GetJointStatus(Joint) == EBox3DAsyncJointStatus::RemovalQueued)
			FPlatformProcess::Sleep(0.001f);
		TestTrue(TEXT("Joint removed"), Physics->GetJointStatus(Joint) == EBox3DAsyncJointStatus::Unavailable);
	}
	JointDesc.Type = EBox3DAsyncJointType::Weld;
	if (!TestTrue(TEXT("Joint before body removal admitted"), Physics->TryCreateJoint(JointDesc, Joint))) return false;
	if (!TestTrue(TEXT("Body removal admitted"), Physics->TryRemoveBody(BodyB))) return false;
	const double RemovalDeadline = FPlatformTime::Seconds() + 5.0;
	while (FPlatformTime::Seconds() < RemovalDeadline && Physics->GetJointStatus(Joint) == EBox3DAsyncJointStatus::Pending)
		FPlatformProcess::Sleep(0.001f);
	while (FPlatformTime::Seconds() < RemovalDeadline && Physics->GetJointStatus(Joint) == EBox3DAsyncJointStatus::Active)
		FPlatformProcess::Sleep(0.001f);
	TestTrue(TEXT("Removing endpoint retires joint"), Physics->GetJointStatus(Joint) == EBox3DAsyncJointStatus::Failed);
	TestTrue(TEXT("Retired joint identity can be released"), Physics->TryRemoveJoint(Joint));
	TestTrue(TEXT("Remove remaining body"), Physics->TryRemoveBody(BodyA));
	Physics->UnregisterCreationConsumer(Consumer);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBox3DAsyncJointBreakTest,
	"Box3DUnreal.Async.JointBreak", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBox3DAsyncJointBreakTest::RunTest(const FString& Parameters)
{
	Box3DTest::FTestWorld World(TEXT("AsyncJointBreak"));
	UBox3DAsyncSubsystem* Physics = World.World->GetSubsystem<UBox3DAsyncSubsystem>();
	if (!TestNotNull(TEXT("Physics"), Physics)) return false;
	FBox3DAsyncCreationConsumer Consumer;
	if (!TestTrue(TEXT("Consumer"), Physics->TryRegisterCreationConsumer(Consumer))) return false;
	FBox3DAsyncBodyDesc BodyDesc;
	BodyDesc.Shapes.AddDefaulted();
	FBox3DAsyncBodyHandle Anchor, Body;
	if (!Physics->TryCreateBody(Consumer, BodyDesc, Anchor)) return false;
	BodyDesc.Motion = EBox3DAsyncBodyMotion::Dynamic;
	BodyDesc.Transform.SetLocation(FVector(200, 0, 0));
	if (!Physics->TryCreateBody(Consumer, BodyDesc, Body)) return false;
	FBox3DAsyncJointDesc Desc;
	Desc.BodyA = Anchor; Desc.BodyB = Body; Desc.Type = EBox3DAsyncJointType::Weld; Desc.BreakForce = 0.01f;
	FBox3DAsyncJointHandle Joint;
	if (!TestTrue(TEXT("Breakable joint admitted"), Physics->TryCreateJoint(Desc, Joint))) return false;
	const double Deadline = FPlatformTime::Seconds() + 5.0;
	while (FPlatformTime::Seconds() < Deadline)
	{
		const EBox3DAsyncJointStatus Status = Physics->GetJointStatus(Joint);
		if (Status == EBox3DAsyncJointStatus::Broken || Status == EBox3DAsyncJointStatus::Failed) break;
		FPlatformProcess::Sleep(0.001f);
	}
	TestTrue(TEXT("Threshold changes joint status"), Physics->GetJointStatus(Joint) == EBox3DAsyncJointStatus::Broken);
	TArray<FBox3DAsyncJointBreakEvent> Events;
	bool bGap = true;
	Physics->DrainJointBreakEvents(Events, bGap);
	TestFalse(TEXT("Break event stream has no gap"), bGap);
	TestTrue(TEXT("Break event carries identity and step"), Events.ContainsByPredicate([Joint](const FBox3DAsyncJointBreakEvent& E)
		{ return E.Joint.World == Joint.World && E.Joint.Id == Joint.Id && E.SimulationStep > 0; }));
	TestTrue(TEXT("Release broken joint"), Physics->TryRemoveJoint(Joint));
	TestTrue(TEXT("Remove body"), Physics->TryRemoveBody(Body));
	TestTrue(TEXT("Remove anchor"), Physics->TryRemoveBody(Anchor));
	Physics->UnregisterCreationConsumer(Consumer);
	return true;
}
#endif
