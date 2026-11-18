#include "World/Box3DAsyncSubsystem.h"
#include "World/Box3DAsyncEventOrdering.h"
#include "Box3DTestHelpers.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBox3DAsyncIndependentEventSubscriptions,
	"Box3D.Async.IndependentEventSubscriptions", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBox3DAsyncIndependentEventSubscriptions::RunTest(const FString& Parameters)
{
	Box3DTest::FTestWorld World(TEXT("AsyncEventSubscribers"));
	UBox3DAsyncSubsystem* Physics = World.World->GetSubsystem<UBox3DAsyncSubsystem>();
	if (!TestNotNull(TEXT("Async world"), Physics)) return false;
	FBox3DAsyncShapeOptions SensorOptions;
	SensorOptions.bSensor = true;
	FBox3DAsyncShapeOptions VisitorOptions;
	VisitorOptions.bDetectableBySensors = true;
	FBox3DAsyncBodyId Sensor = 0, Sensor2 = 0, Visitor = 0;
	if (!TestTrue(TEXT("Create sensor"), Physics->TryCreateStaticBox(FTransform::Identity, FVector(5000), SensorOptions, Sensor))
		|| !TestTrue(TEXT("Create second sensor"), Physics->TryCreateStaticBox(FTransform::Identity, FVector(5000), SensorOptions, Sensor2))
		|| !TestTrue(TEXT("Create visitor"), Physics->TryCreateDynamicSphere(FTransform::Identity, 25, FVector::ZeroVector, VisitorOptions, Visitor))) return false;
	bool bEntered = false, bGap = false;
	const double StartDeadline = FPlatformTime::Seconds() + 5.0;
	while (!bEntered && !bGap && FPlatformTime::Seconds() < StartDeadline)
	{
		TArray<FBox3DAsyncEventBatch> Legacy;
		Physics->DrainRawEvents(Legacy, bGap);
		for (const auto& Batch : Legacy) for (const auto& Event : Batch.Events)
			if (Event.Kind == EBox3DAsyncEventKind::SensorEnter && Event.BodyA == Sensor && Event.BodyB == Visitor) bEntered = true;
		if (!bEntered) FPlatformProcess::Sleep(0.01f);
	}
	if (!TestTrue(TEXT("Pair active before subscription"), bEntered) || !TestFalse(TEXT("No legacy gap"), bGap)) return false;
	FBox3DAsyncEventFilter Filter;
	Filter.bContacts = false;
	Filter.bImpacts = false;
	FBox3DAsyncEventSubscription First, Second;
	if (!TestTrue(TEXT("First subscription"), Physics->SubscribeEvents(Filter, First))
		|| !TestTrue(TEXT("Second subscription"), Physics->SubscribeEvents(Filter, Second))) return false;
	for (FBox3DAsyncEventSubscription Handle : {First, Second})
	{
		FBox3DAsyncEventDelivery Delivery;
		if (!TestTrue(TEXT("Poll baseline"), Physics->PollEvents(Handle, Delivery))) return false;
		TestTrue(TEXT("Baseline delivered"), Delivery.Status == EBox3DAsyncEventPollStatus::Baseline);
		TestTrue(TEXT("Baseline includes active pair"), Delivery.Batch.Events.ContainsByPredicate([&](const FBox3DAsyncEvent& Event)
			{ return Event.Kind == EBox3DAsyncEventKind::SensorEnter && Event.BodyA == Sensor && Event.BodyB == Visitor; }));
		TArray<FBox3DAsyncEvent> VisitorPairs;
		for (const FBox3DAsyncEvent& Event : Delivery.Batch.Events)
			if (Event.BodyB == Visitor) VisitorPairs.Add(Event);
		if (TestEqual(TEXT("Both sensors detect visitor"), VisitorPairs.Num(), 2))
		{
			TestEqual(TEXT("Baseline first pair ordered"), VisitorPairs[0].BodyA, Sensor);
			TestEqual(TEXT("Baseline second pair ordered"), VisitorPairs[1].BodyA, Sensor2);
		}
		TestTrue(TEXT("First sensor also detects second sensor"), Delivery.Batch.Events.ContainsByPredicate([&](const FBox3DAsyncEvent& Event)
			{ return Event.Kind == EBox3DAsyncEventKind::SensorEnter && Event.BodyA == Sensor && Event.BodyB == Sensor2; }));
		TestTrue(TEXT("Second sensor independently detects first"), Delivery.Batch.Events.ContainsByPredicate([&](const FBox3DAsyncEvent& Event)
			{ return Event.Kind == EBox3DAsyncEventKind::SensorEnter && Event.BodyA == Sensor2 && Event.BodyB == Sensor; }));
	}
	if (!TestTrue(TEXT("Remove visitor"), Physics->TryRemoveBody(Visitor))) return false;
	bool bFirstExit = false, bSecondExit = false;
	const double EndDeadline = FPlatformTime::Seconds() + 5.0;
	while ((!bFirstExit || !bSecondExit) && FPlatformTime::Seconds() < EndDeadline)
	{
		for (int32 Index = 0; Index != 2; ++Index)
		{
			FBox3DAsyncEventDelivery Delivery;
			if (!Physics->PollEvents(Index ? Second : First, Delivery)) continue;
			for (const auto& Event : Delivery.Batch.Events)
				if (Event.Kind == EBox3DAsyncEventKind::SensorExit && Event.BodyA == Sensor && Event.BodyB == Visitor)
					(Index ? bSecondExit : bFirstExit) = true;
		}
		if (!bFirstExit || !bSecondExit) FPlatformProcess::Sleep(0.01f);
	}
	TestTrue(TEXT("First receives exit"), bFirstExit);
	TestTrue(TEXT("Second independently receives exit"), bSecondExit);
	Physics->UnsubscribeEvents(First);
	FBox3DAsyncEventDelivery Ignored;
	TestFalse(TEXT("Unsubscribed handle rejected"), Physics->PollEvents(First, Ignored));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBox3DAsyncEventLagRecovery,
	"Box3D.Async.EventLagRecovery", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBox3DAsyncEventLagRecovery::RunTest(const FString& Parameters)
{
	Box3DTest::FTestWorld World(TEXT("AsyncEventLag"));
	UBox3DAsyncSubsystem* Physics = World.World->GetSubsystem<UBox3DAsyncSubsystem>();
	if (!TestNotNull(TEXT("Async world"), Physics)) return false;
	FBox3DAsyncShapeOptions SensorOptions;
	SensorOptions.bSensor = true;
	FBox3DAsyncShapeOptions VisitorOptions;
	VisitorOptions.bDetectableBySensors = true;
	FBox3DAsyncBodyId Sensor = 0;
	if (!TestTrue(TEXT("Create sensor"), Physics->TryCreateStaticBox(FTransform::Identity, FVector(5000), SensorOptions, Sensor))) return false;
	FBox3DAsyncEventFilter Filter;
	Filter.bContacts = false;
	Filter.bImpacts = false;
	FBox3DAsyncEventSubscription Fast, Slow;
	if (!TestTrue(TEXT("Fast subscriber"), Physics->SubscribeEvents(Filter, Fast))
		|| !TestTrue(TEXT("Slow subscriber"), Physics->SubscribeEvents(Filter, Slow))) return false;
	const double BaselineDeadline = FPlatformTime::Seconds() + 5.0;
	for (FBox3DAsyncEventSubscription Handle : {Fast, Slow})
	{
		FBox3DAsyncEventDelivery Delivery;
		do
		{
			if (!Physics->PollEvents(Handle, Delivery)) return false;
			if (Delivery.Status != EBox3DAsyncEventPollStatus::Baseline) FPlatformProcess::Sleep(0.01f);
		} while (Delivery.Status != EBox3DAsyncEventPollStatus::Baseline && FPlatformTime::Seconds() < BaselineDeadline);
		if (!TestTrue(TEXT("Initial baseline"), Delivery.Status == EBox3DAsyncEventPollStatus::Baseline)) return false;
	}
	// Retention is 128 batches. Repeated real transitions exceed it while the
	// fast subscriber advances; the slow subscriber must recover independently.
	for (int32 Cycle = 0; Cycle < 66; ++Cycle)
	{
		FBox3DAsyncBodyId Visitor = 0;
		if (!Physics->TryCreateDynamicSphere(FTransform::Identity, 25, FVector::ZeroVector, VisitorOptions, Visitor))
		{
			AddError(TEXT("Visitor admission failed"));
			return false;
		}
		for (int32 Phase = 0; Phase < 2; ++Phase)
		{
			if (Phase == 1 && !Physics->TryRemoveBody(Visitor)) return false;
			const EBox3DAsyncEventKind Expected = Phase ? EBox3DAsyncEventKind::SensorExit : EBox3DAsyncEventKind::SensorEnter;
			bool bObserved = false;
			const double Deadline = FPlatformTime::Seconds() + 3.0;
			while (!bObserved && FPlatformTime::Seconds() < Deadline)
			{
				FBox3DAsyncEventDelivery Delivery;
				if (!Physics->PollEvents(Fast, Delivery)) return false;
				if (Delivery.Status == EBox3DAsyncEventPollStatus::NeedsResync)
				{
					AddError(TEXT("Fast subscriber unexpectedly lost history"));
					return false;
				}
				for (const auto& Event : Delivery.Batch.Events)
					if (Event.Kind == Expected && Event.BodyA == Sensor && Event.BodyB == Visitor) bObserved = true;
				TArray<FBox3DAsyncCreationResult> Creations;
				Physics->DrainCreationResults(Creations);
				if (!bObserved) FPlatformProcess::Sleep(0.005f);
			}
			if (!TestTrue(TEXT("Expected transition arrived"), bObserved)) return false;
		}
	}
	FBox3DAsyncEventDelivery Delivery;
	if (!TestTrue(TEXT("Poll lagging subscriber"), Physics->PollEvents(Slow, Delivery))) return false;
	TestTrue(TEXT("Lag is reported"), Delivery.Status == EBox3DAsyncEventPollStatus::NeedsResync);
	TestTrue(TEXT("Missing range recorded"), Delivery.FirstMissedSequence > 0 && Delivery.LastMissedSequence >= Delivery.FirstMissedSequence);
	if (!TestTrue(TEXT("Poll replacement baseline"), Physics->PollEvents(Slow, Delivery))) return false;
	TestTrue(TEXT("Fresh baseline replaces stale state"), Delivery.Status == EBox3DAsyncEventPollStatus::Baseline);
	TestTrue(TEXT("No active visitor in recovered baseline"), Delivery.Batch.Events.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBox3DAsyncEventBodyPairHandoff,
	"Box3D.Async.EventBodyPairHandoff", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBox3DAsyncEventBodyPairHandoff::RunTest(const FString& Parameters)
{
	auto SensorPair = [](uint64 BodyA, uint64 BodyB, uint64 ShapeA, uint64 ShapeB)
	{
		FBox3DAsyncEvent Event;
		Event.Kind = EBox3DAsyncEventKind::SensorEnter;
		Event.BodyA = BodyA; Event.BodyB = BodyB; Event.ShapeA = ShapeA; Event.ShapeB = ShapeB;
		return Event;
	};
	const FBox3DAsyncEvent One = SensorPair(10, 20, 1, 2);
	const FBox3DAsyncEvent Two = SensorPair(10, 20, 1, 3);
	const FBox3DAsyncEvent Three = SensorPair(10, 20, 1, 4);
	const FBox3DAsyncEvent Other = SensorPair(5, 30, 5, 6);
	TArray<FBox3DAsyncEvent> Active;
	FBox3DAsyncEventBatch Baseline;
	Baseline.Events = {Two, One, Other};
	Box3DAsyncPrivate::AggregateBodiesForTest(Active, Baseline, true);
	if (!TestEqual(TEXT("One baseline per body pair"), Baseline.Events.Num(), 2)) return false;
	TestEqual(TEXT("Body baseline sorted independent of shape input order"), Baseline.Events[0].BodyA, uint64(5));
	TestEqual(TEXT("Second body pair sorted"), Baseline.Events[1].BodyA, uint64(10));
	FBox3DAsyncEvent ExitOne = One;
	ExitOne.Kind = EBox3DAsyncEventKind::SensorExit;
	FBox3DAsyncEventBatch Handoff;
	Handoff.Events = {Three, ExitOne};
	Box3DAsyncPrivate::AggregateBodiesForTest(Active, Handoff, false);
	TestTrue(TEXT("Same-step handoff keeps body relation active"), Handoff.Events.IsEmpty());
	FBox3DAsyncEvent ExitTwo = Two;
	ExitTwo.Kind = EBox3DAsyncEventKind::SensorExit;
	FBox3DAsyncEvent ExitThree = Three;
	ExitThree.Kind = EBox3DAsyncEventKind::SensorExit;
	ExitThree.EndCause = EBox3DAsyncEventEndCause::Removed;
	FBox3DAsyncEventBatch Removed;
	Removed.Events = {ExitThree, ExitTwo};
	Box3DAsyncPrivate::AggregateBodiesForTest(Active, Removed, false);
	if (!TestEqual(TEXT("Last two shape exits become one body exit"), Removed.Events.Num(), 1)) return false;
	TestTrue(TEXT("Body exit preserves removal cause"), Removed.Events[0].EndCause == EBox3DAsyncEventEndCause::Removed);
	TestEqual(TEXT("Unrelated body pair remains active"), Active.Num(), 1);
	return true;
}
#endif
