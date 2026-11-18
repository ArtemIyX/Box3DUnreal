#include "World/Box3DAsyncSubsystem.h"
#include "World/Box3DSettings.h"
#include "Box3DTestHelpers.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "box3d/box3d.h"
#include "box3d/collision.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBox3DAsyncNativeImpactThreshold,
	"Box3D.Async.NativeImpactThreshold", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBox3DAsyncNativeImpactThreshold::RunTest(const FString& Parameters)
{
	auto HitAtSpeed = [](float Speed)
	{
		b3WorldDef WorldDef = b3DefaultWorldDef();
		WorldDef.gravity = b3Vec3{0, 0, 0};
		WorldDef.hitEventThreshold = 1.0f;
		WorldDef.workerCount = 1;
		const b3WorldId World = b3CreateWorld(&WorldDef);
		b3BodyDef BodyDef = b3DefaultBodyDef();
		const b3BodyId Wall = b3CreateBody(World, &BodyDef);
		b3ShapeDef ShapeDef = b3DefaultShapeDef();
		const b3BoxHull WallHull = b3MakeBoxHull(0.1f, 2.0f, 2.0f);
		b3CreateHullShape(Wall, &ShapeDef, &WallHull.base);
		BodyDef.type = b3_dynamicBody;
		BodyDef.position = b3Pos{-1.0f, 0, 0};
		BodyDef.linearVelocity = b3Vec3{Speed, 0, 0};
		const b3BodyId Moving = b3CreateBody(World, &BodyDef);
		ShapeDef.enableHitEvents = true;
		const b3Sphere Sphere{{0, 0, 0}, 0.1f};
		b3CreateSphereShape(Moving, &ShapeDef, &Sphere);
		bool bHit = false;
		for (int32 Step = 0; Step < 180; ++Step)
		{
			b3World_Step(World, 1.0f / 60.0f, 1);
			bHit |= b3World_GetContactEvents(World).hitCount > 0;
		}
		b3DestroyWorld(World);
		return bHit;
	};
	TestFalse(TEXT("Below threshold is silent"), HitAtSpeed(0.5f));
	TestFalse(TEXT("At threshold is silent"), HitAtSpeed(1.0f));
	TestTrue(TEXT("Above threshold emits an impact"), HitAtSpeed(2.0f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBox3DAsyncImpactMetadata,
	"Box3D.Async.ImpactMetadata", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBox3DAsyncImpactMetadata::RunTest(const FString& Parameters)
{
	UBox3DSettings* Settings = GetMutableDefault<UBox3DSettings>();
	const FVector OldGravity = Settings->Gravity;
	const float OldThreshold = Settings->HitEventThreshold;
	Settings->Gravity = FVector::ZeroVector;
	Settings->HitEventThreshold = 100.0f;
	ON_SCOPE_EXIT { Settings->Gravity = OldGravity; Settings->HitEventThreshold = OldThreshold; };
	Box3DTest::FTestWorld World(TEXT("AsyncImpactMetadata"));
	UBox3DAsyncSubsystem* Physics = World.World->GetSubsystem<UBox3DAsyncSubsystem>();
	if (!TestNotNull(TEXT("Async world"), Physics)) return false;
	FBox3DAsyncShapeOptions WallOptions;
	WallOptions.MaterialId = 0x1234;
	FBox3DAsyncShapeOptions VisitorOptions;
	VisitorOptions.bImpactEvents = true;
	VisitorOptions.MaterialId = 0x5678;
	FBox3DAsyncBodyHandle Wall, Visitor;
	if (!TestTrue(TEXT("Wall accepted"), Physics->TryCreateStaticBox(FTransform::Identity, FVector(10, 100, 100), WallOptions, Wall))
		|| !TestTrue(TEXT("Visitor accepted"), Physics->TryCreateDynamicSphere(FTransform(FVector(-100, 0, 0)), 10,
			FVector(400, 0, 0), VisitorOptions, Visitor))) return false;
	bool bSeen = false, bGap = false;
	const double Deadline = FPlatformTime::Seconds() + 5.0;
	while (!bSeen && !bGap && FPlatformTime::Seconds() < Deadline)
	{
		TArray<FBox3DAsyncEventBatch> Batches;
		Physics->DrainRawEvents(Batches, bGap);
		for (const auto& Batch : Batches) for (const auto& Event : Batch.Events)
		{
			if (Event.Kind != EBox3DAsyncEventKind::Impact) continue;
			bSeen = true;
			TestTrue(TEXT("Impact endpoints canonical"), Event.ShapeA < Event.ShapeB);
			TestEqual(TEXT("Wall is canonical endpoint A"), Event.BodyA, Wall.Id);
			TestEqual(TEXT("Visitor is canonical endpoint B"), Event.BodyB, Visitor.Id);
			TestTrue(TEXT("Normal points from wall to visitor"), Event.Normal.X < -0.9);
			TestTrue(TEXT("Approach speed uses cm/s"), Event.ApproachSpeed > 350 && Event.ApproachSpeed < 450);
			TestEqual(TEXT("Wall material follows endpoint A"), Event.MaterialA, uint64(0x1234));
			TestEqual(TEXT("Visitor material follows endpoint B"), Event.MaterialB, uint64(0x5678));
		}
		if (!bSeen) FPlatformProcess::Sleep(0.01f);
	}
	TestFalse(TEXT("No event gap"), bGap);
	TestTrue(TEXT("Impact delivered"), bSeen);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBox3DAsyncSimultaneousRemoval,
	"Box3D.Async.SimultaneousRemoval", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBox3DAsyncSimultaneousRemoval::RunTest(const FString& Parameters)
{
	Box3DTest::FTestWorld World(TEXT("AsyncSimultaneousRemoval"));
	UBox3DAsyncSubsystem* Physics = World.World->GetSubsystem<UBox3DAsyncSubsystem>();
	if (!TestNotNull(TEXT("Async world"), Physics)) return false;
	FBox3DAsyncShapeOptions SensorOptions; SensorOptions.bSensor = true;
	FBox3DAsyncShapeOptions VisitorOptions; VisitorOptions.bDetectableBySensors = true;
	FBox3DAsyncBodyHandle Sensor, Visitors[2];
	if (!Physics->TryCreateStaticBox(FTransform::Identity, FVector(5000), SensorOptions, Sensor)
		|| !Physics->TryCreateDynamicSphere(FTransform(FVector(-100, 0, 0)), 20, FVector::ZeroVector, VisitorOptions, Visitors[0])
		|| !Physics->TryCreateDynamicSphere(FTransform(FVector(100, 0, 0)), 20, FVector::ZeroVector, VisitorOptions, Visitors[1])) return false;
	FBox3DAsyncShapeId OldShapeIds[2] = {0, 0};
	bool bEntered[2] = {false, false}, bGap = false;
	const double EnterDeadline = FPlatformTime::Seconds() + 5.0;
	while ((!bEntered[0] || !bEntered[1]) && !bGap && FPlatformTime::Seconds() < EnterDeadline)
	{
		TArray<FBox3DAsyncEventBatch> Batches;
		Physics->DrainRawEvents(Batches, bGap);
		for (const auto& Batch : Batches) for (const auto& Event : Batch.Events)
			for (int32 I = 0; I < 2; ++I)
				if (Event.Kind == EBox3DAsyncEventKind::SensorEnter && Event.BodyA == Sensor.Id && Event.BodyB == Visitors[I].Id)
				{ bEntered[I] = true; OldShapeIds[I] = Event.ShapeB; }
		if (!bEntered[0] || !bEntered[1]) FPlatformProcess::Sleep(0.01f);
	}
	if (!TestTrue(TEXT("Both overlaps observed"), bEntered[0] && bEntered[1]) || !TestFalse(TEXT("No initial gap"), bGap)) return false;
	if (!TestTrue(TEXT("Remove first"), Physics->TryRemoveBody(Visitors[0]))
		|| !TestTrue(TEXT("Remove second"), Physics->TryRemoveBody(Visitors[1]))) return false;
	int32 Exits[2] = {0, 0};
	const uint64 InitialStep = Physics->GetCompletedStepCount();
	const double ExitDeadline = FPlatformTime::Seconds() + 5.0;
	while ((Exits[0] == 0 || Exits[1] == 0 || Physics->GetCompletedStepCount() < InitialStep + 4)
		&& !bGap && FPlatformTime::Seconds() < ExitDeadline)
	{
		TArray<FBox3DAsyncEventBatch> Batches;
		Physics->DrainRawEvents(Batches, bGap);
		for (const auto& Batch : Batches) for (const auto& Event : Batch.Events)
			for (int32 I = 0; I < 2; ++I)
				if (Event.Kind == EBox3DAsyncEventKind::SensorExit && Event.BodyA == Sensor.Id && Event.BodyB == Visitors[I].Id)
				{ ++Exits[I]; TestTrue(TEXT("Removal cause"), Event.EndCause == EBox3DAsyncEventEndCause::Removed); }
		FPlatformProcess::Sleep(0.01f);
	}
	TestFalse(TEXT("No removal gap"), bGap);
	TestEqual(TEXT("First exits exactly once"), Exits[0], 1);
	TestEqual(TEXT("Second exits exactly once"), Exits[1], 1);
	FBox3DAsyncBodyHandle Replacement;
	if (!TestTrue(TEXT("Replacement accepted"), Physics->TryCreateDynamicSphere(FTransform::Identity, 20, FVector::ZeroVector, VisitorOptions, Replacement))) return false;
	bool bReplacement = false;
	const double ReplacementDeadline = FPlatformTime::Seconds() + 5.0;
	while (!bReplacement && !bGap && FPlatformTime::Seconds() < ReplacementDeadline)
	{
		TArray<FBox3DAsyncEventBatch> Batches;
		Physics->DrainRawEvents(Batches, bGap);
		for (const auto& Batch : Batches) for (const auto& Event : Batch.Events)
			if (Event.Kind == EBox3DAsyncEventKind::SensorEnter && Event.BodyA == Sensor.Id && Event.BodyB == Replacement.Id)
			{
				bReplacement = true;
				TestTrue(TEXT("Replacement identity is new"), Event.ShapeB != OldShapeIds[0] && Event.ShapeB != OldShapeIds[1]);
			}
		if (!bReplacement) FPlatformProcess::Sleep(0.01f);
	}
	TestTrue(TEXT("Replacement enter arrives"), bReplacement);
	TestFalse(TEXT("No gap after replacement"), bGap);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBox3DAsyncEventOverflowRecovery,
	"Box3D.Async.EventOverflowRecovery", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBox3DAsyncEventOverflowRecovery::RunTest(const FString& Parameters)
{
	UBox3DSettings* Settings = GetMutableDefault<UBox3DSettings>();
	const FVector OldGravity = Settings->Gravity;
	const float OldTimeStep = Settings->FixedTimeStep;
	Settings->Gravity = FVector::ZeroVector;
	Settings->FixedTimeStep = 0.25f;
	ON_SCOPE_EXIT { Settings->Gravity = OldGravity; Settings->FixedTimeStep = OldTimeStep; };
	Box3DTest::FTestWorld World(TEXT("AsyncEventOverflow"));
	UBox3DAsyncSubsystem* Physics = World.World->GetSubsystem<UBox3DAsyncSubsystem>();
	if (!TestNotNull(TEXT("Async world"), Physics)) return false;
	FBox3DAsyncEventFilter Filter;
	Filter.bContacts = false;
	Filter.bImpacts = false;
	FBox3DAsyncEventSubscription Subscriber;
	if (!TestTrue(TEXT("Subscription accepted"), Physics->SubscribeEvents(Filter, Subscriber))) return false;
	FBox3DAsyncEventDelivery Delivery;
	const double InitialDeadline = FPlatformTime::Seconds() + 5.0;
	while (FPlatformTime::Seconds() < InitialDeadline)
	{
		if (!Physics->PollEvents(Subscriber, Delivery)) return false;
		if (Delivery.Status == EBox3DAsyncEventPollStatus::Baseline) break;
		FPlatformProcess::Sleep(0.005f);
	}
	if (!TestTrue(TEXT("Initial empty baseline"), Delivery.Status == EBox3DAsyncEventPollStatus::Baseline && Delivery.Batch.Events.IsEmpty())) return false;
	// The worker's publication cap is 1024 per step. Restrict bilateral masks
	// to sensor/visitor pairs so 33 sensors x 32 visitors produce 1056 enters.
	FBox3DAsyncShapeOptions SensorOptions;
	SensorOptions.bSensor = true;
	SensorOptions.CategoryBits = 1;
	SensorOptions.MaskBits = 2;
	FBox3DAsyncShapeOptions VisitorOptions;
	VisitorOptions.bDetectableBySensors = true;
	VisitorOptions.CategoryBits = 2;
	VisitorOptions.MaskBits = 1;
	for (int32 I = 0; I < 33; ++I)
	{
		FBox3DAsyncBodyHandle Sensor;
		if (!TestTrue(TEXT("Sensor admitted"), Physics->TryCreateStaticBox(FTransform::Identity, FVector(500), SensorOptions, Sensor))) return false;
	}
	for (int32 I = 0; I < 32; ++I)
	{
		FBox3DAsyncBodyHandle Visitor;
		if (!TestTrue(TEXT("Visitor admitted"), Physics->TryCreateDynamicSphere(FTransform(FVector(I * 2, 0, 0)), 1,
			FVector::ZeroVector, VisitorOptions, Visitor))) return false;
	}
	bool bGap = false, bRecovered = false;
	const double RecoveryDeadline = FPlatformTime::Seconds() + 8.0;
	while (!bRecovered && FPlatformTime::Seconds() < RecoveryDeadline)
	{
		if (!Physics->PollEvents(Subscriber, Delivery)) return false;
		if (Delivery.Status == EBox3DAsyncEventPollStatus::NeedsResync) bGap = true;
		if (bGap && Delivery.Status == EBox3DAsyncEventPollStatus::Baseline)
		{
			bRecovered = true;
			TestEqual(TEXT("Recovered baseline contains complete sensor membership"), Delivery.Batch.Events.Num(), 1056);
		}
		if (!bRecovered) FPlatformProcess::Sleep(0.01f);
	}
	TestTrue(TEXT("Overflow is reported as a gap"), bGap);
	TestTrue(TEXT("Complete baseline follows gap"), bRecovered);
	const FBox3DAsyncDiagnostics Diagnostics = Physics->GetDiagnostics();
	TestTrue(TEXT("Lost batch accounted"), Diagnostics.DroppedEventBatches > 0);
	return true;
}
#endif
