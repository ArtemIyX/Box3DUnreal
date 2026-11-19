#include "World/Box3DAsyncSubsystem.h"
#include "Box3DTestHelpers.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBox3DAsyncFieldTest,
	"Box3DUnreal.Async.PersistentField", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBox3DAsyncFieldTest::RunTest(const FString& Parameters)
{
	Box3DTest::FTestWorld World(TEXT("AsyncField"));
	UBox3DAsyncSubsystem* Physics = World.World->GetSubsystem<UBox3DAsyncSubsystem>();
	if (!TestNotNull(TEXT("Physics"), Physics)) return false;
	FBox3DAsyncCreationConsumer Consumer;
	if (!TestTrue(TEXT("Consumer"), Physics->TryRegisterCreationConsumer(Consumer))) return false;
	FBox3DAsyncBodyDesc Desc;
	Desc.Motion = EBox3DAsyncBodyMotion::Dynamic;
	Desc.GravityScale = 0.0f;
	Desc.Shapes.AddDefaulted();
	FBox3DAsyncBodyHandle Body;
	if (!TestTrue(TEXT("Body admitted"), Physics->TryCreateBody(Consumer, Desc, Body))) return false;
	FBox3DAsyncCreationResult Creation;
	bool bCreated = false;
	const double Deadline = FPlatformTime::Seconds() + 5.0;
	while (FPlatformTime::Seconds() < Deadline && !(bCreated = Physics->TryTakeCreationResult(Consumer, Body, Creation)))
		FPlatformProcess::Sleep(0.001f);
	if (!TestTrue(TEXT("Body created"), bCreated && Creation.bSucceeded)) return false;
	FBox3DAsyncFieldDesc FieldDesc;
	FieldDesc.Mode = EBox3DAsyncFieldMode::Acceleration;
	FieldDesc.Radius = 1000.0f;
	FieldDesc.Vector = FVector(100, 0, 0);
	FBox3DAsyncFieldHandle Field;
	if (!TestTrue(TEXT("Field admitted"), Physics->TryCreateField(FieldDesc, Field))) return false;
	FBox3DAsyncFieldHandle Foreign = Field;
	Foreign.World = FGuid::NewGuid();
	TestFalse(TEXT("Foreign field rejected"), Physics->TryRemoveField(Foreign));
	bool bAccelerated = false;
	FBox3DAsyncBodyState State;
	const double MotionDeadline = FPlatformTime::Seconds() + 3.0;
	while (FPlatformTime::Seconds() < MotionDeadline && !bAccelerated)
	{
		bAccelerated = Physics->TryGetLatestBodyState(Body, State) && State.LinearVelocity.X > 10.0;
		if (!bAccelerated) FPlatformProcess::Sleep(0.001f);
	}
	TestTrue(TEXT("Field accelerates body over fixed steps"), bAccelerated);
	TestTrue(TEXT("Remove field"), Physics->TryRemoveField(Field));
	const uint64 RemovalStep = State.SimulationStep;
	const double SettleDeadline = FPlatformTime::Seconds() + 3.0;
	while (FPlatformTime::Seconds() < SettleDeadline
		&& (!Physics->TryGetLatestBodyState(Body, State) || State.SimulationStep < RemovalStep + 2))
		FPlatformProcess::Sleep(0.001f);
	const double SettledVelocity = State.LinearVelocity.X;
	const uint64 SettledStep = State.SimulationStep;
	while (FPlatformTime::Seconds() < SettleDeadline
		&& (!Physics->TryGetLatestBodyState(Body, State) || State.SimulationStep < SettledStep + 3))
		FPlatformProcess::Sleep(0.001f);
	TestTrue(TEXT("Simulation advanced after field removal"), State.SimulationStep >= SettledStep + 3);
	TestTrue(TEXT("Removal stops persistent acceleration"), FMath::IsNearlyEqual(State.LinearVelocity.X, SettledVelocity, 0.1));
	TestFalse(TEXT("Removed field cannot be updated"), Physics->TryUpdateField(Field, FieldDesc));
	TestTrue(TEXT("Remove body"), Physics->TryRemoveBody(Body));
	Physics->UnregisterCreationConsumer(Consumer);
	return true;
}
#endif
