#include "World/Box3DAsyncSubsystem.h"
#include "Box3DTestHelpers.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBox3DAsyncEventDispatchFairness,
	"Box3D.Async.EventDispatchFairness", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBox3DAsyncEventDispatchFairness::RunTest(const FString& Parameters)
{
	Box3DTest::FTestWorld TestWorld(TEXT("AsyncEventDispatchFairness"));
	UBox3DAsyncSubsystem* Physics = TestWorld.World->GetSubsystem<UBox3DAsyncSubsystem>();
	if (!TestNotNull(TEXT("Async subsystem"), Physics)) return false;
	const double Deadline = FPlatformTime::Seconds() + 5.0;
	while (Physics->GetCompletedStepCount() == 0 && FPlatformTime::Seconds() < Deadline)
		FPlatformProcess::Sleep(0.005f);
	if (!TestTrue(TEXT("A real step has completed"), Physics->GetCompletedStepCount() > 0)) return false;
	FBox3DAsyncEventSubscription Probe;
	FBox3DAsyncEventFilter Filter;
	if (!Physics->SubscribeEvents(Filter, Probe)) return false;
	FBox3DAsyncEventDelivery Ready;
	do
	{
		if (!Physics->PollEvents(Probe, Ready)) return false;
		if (Ready.Status != EBox3DAsyncEventPollStatus::Baseline) FPlatformProcess::Sleep(0.005f);
	} while (Ready.Status != EBox3DAsyncEventPollStatus::Baseline && FPlatformTime::Seconds() < Deadline);
	Physics->UnsubscribeEvents(Probe);
	if (!TestTrue(TEXT("Baseline published"), Ready.Status == EBox3DAsyncEventPollStatus::Baseline)) return false;
	int32 Received[12] = {};
	TArray<FBox3DAsyncEventSubscription> Handles;
	for (int32 Index = 0; Index < 12; ++Index)
	{
		FBox3DAsyncEventSubscription Handle;
		if (!TestTrue(TEXT("Subscribe callback"), Physics->SubscribeEvents(Filter, TestWorld.World,
			[&Received, Index](const FBox3DAsyncEventDelivery& Delivery)
			{
				if (Delivery.Status == EBox3DAsyncEventPollStatus::Baseline) ++Received[Index];
			}, false, Handle))) return false;
		Handles.Add(Handle);
	}
	Physics->Tick(0.0f);
	int32 FirstPass = 0;
	for (int32 Count : Received) FirstPass += Count;
	TestEqual(TEXT("First pump has bounded callback work"), FirstPass, 8);
	Physics->Tick(0.0f);
	int32 SecondPass = 0;
	for (int32 Count : Received) SecondPass += Count;
	TestEqual(TEXT("Round robin reaches all subscribers"), SecondPass, 12);
	for (FBox3DAsyncEventSubscription Handle : Handles) Physics->UnsubscribeEvents(Handle);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBox3DAsyncSustainedEventPressure,
	"Box3D.Async.SustainedEventPressure", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBox3DAsyncSustainedEventPressure::RunTest(const FString& Parameters)
{
	Box3DTest::FTestWorld TestWorld(TEXT("AsyncSustainedEventPressure"));
	UBox3DAsyncSubsystem* Physics = TestWorld.World->GetSubsystem<UBox3DAsyncSubsystem>();
	if (!TestNotNull(TEXT("Async subsystem"), Physics)) return false;
	FBox3DAsyncShapeOptions SensorOptions, VisitorOptions;
	SensorOptions.bSensor = true;
	VisitorOptions.bDetectableBySensors = true;
	FBox3DAsyncBodyId Sensor = 0, Visitor = 0;
	if (!TestTrue(TEXT("Create sensor"), Physics->TryCreateStaticBox(FTransform::Identity, FVector(5000), SensorOptions, Sensor))
		|| !TestTrue(TEXT("Create visitor"), Physics->TryCreateDynamicSphere(FTransform::Identity, 25, FVector::ZeroVector, VisitorOptions, Visitor))) return false;
	FBox3DAsyncEventFilter Filter;
	Filter.bContacts = false;
	Filter.bImpacts = false;
	FBox3DAsyncEventSubscription Handle;
	if (!TestTrue(TEXT("Subscribe"), Physics->SubscribeEvents(Filter, Handle))) return false;
	FBox3DAsyncEventDelivery Delivery;
	const double Deadline = FPlatformTime::Seconds() + 5.0;
	bool bActiveBaseline = false;
	while (FPlatformTime::Seconds() < Deadline && !bActiveBaseline)
	{
		if (!Physics->PollEvents(Handle, Delivery)) return false;
		bActiveBaseline = Delivery.Status == EBox3DAsyncEventPollStatus::Baseline && Delivery.Batch.Events.ContainsByPredicate(
			[Sensor, Visitor](const FBox3DAsyncEvent& Event) { return Event.BodyA == Sensor && Event.BodyB == Visitor; });
		if (!bActiveBaseline)
		{
			// Subscribe after the pair has entered if the initial baseline was empty.
			if (Delivery.Status == EBox3DAsyncEventPollStatus::Events)
			{
				Physics->UnsubscribeEvents(Handle);
				if (!Physics->SubscribeEvents(Filter, Handle)) return false;
			}
			FPlatformProcess::Sleep(0.005f);
		}
	}
	if (!TestTrue(TEXT("Initial active relationship baseline"), bActiveBaseline)) return false;
	const uint64 BeforeDrops = Physics->GetDiagnostics().DroppedEventBatches;
	Physics->SetEventRecordLimitForTest(-1);
	while (Physics->GetDiagnostics().DroppedEventBatches < BeforeDrops + 3 && FPlatformTime::Seconds() < Deadline)
		FPlatformProcess::Sleep(0.005f);
	if (!TestTrue(TEXT("Consecutive steps reported loss"), Physics->GetDiagnostics().DroppedEventBatches >= BeforeDrops + 3)) return false;
	if (!TestTrue(TEXT("Poll loss"), Physics->PollEvents(Handle, Delivery))) return false;
	TestTrue(TEXT("Gap reported"), Delivery.Status == EBox3DAsyncEventPollStatus::NeedsResync);
	if (!TestTrue(TEXT("Poll recovery"), Physics->PollEvents(Handle, Delivery))) return false;
	TestTrue(TEXT("Complete baseline remains available during ongoing overflow"), Delivery.Status == EBox3DAsyncEventPollStatus::Baseline);
	TestTrue(TEXT("Recovered baseline retains native sensor relationship"), Delivery.Batch.Events.ContainsByPredicate(
		[Sensor, Visitor](const FBox3DAsyncEvent& Event) { return Event.BodyA == Sensor && Event.BodyB == Visitor; }));
	Physics->SetEventRecordLimitForTest(1024);
	Physics->UnsubscribeEvents(Handle);
	return true;
}
#endif
