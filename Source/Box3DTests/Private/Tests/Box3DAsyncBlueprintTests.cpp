#include "Box3DAsyncBlueprintTestReceiver.h"
#include "Box3DTestHelpers.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBox3DAsyncBlueprintAdapterTest,
	"Box3D.Async.BlueprintAdapters", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBox3DAsyncBlueprintAdapterTest::RunTest(const FString& Parameters)
{
	auto TestWorld = MakeUnique<Box3DTest::FTestWorld>(TEXT("BlueprintAsyncAdapters"));
	UBox3DAsyncSubsystem* Physics = TestWorld->World->GetSubsystem<UBox3DAsyncSubsystem>();
	if (!TestNotNull(TEXT("Async subsystem"), Physics)) return false;
	TStrongObjectPtr<UBox3DAsyncBlueprintTestReceiver> Receiver(NewObject<UBox3DAsyncBlueprintTestReceiver>());
	TestNull(TEXT("No context means no event subscription"), UBox3DBPAsyncLibrary::SubscribeBox3DAsyncEvents(nullptr, Receiver.Get(), {}));
	FBox3DBPQuery Query;
	Query.UserTag = 42;
	TStrongObjectPtr<UBox3DBPAsyncQuery> Invalid(UBox3DBPAsyncQuery::QueryBox3DAsync(TestWorld->World, Receiver.Get(), Query));
	Invalid->Completed.AddDynamic(Receiver.Get(), &UBox3DAsyncBlueprintTestReceiver::OnQuery);
	Invalid->Activate();
	TestEqual(TEXT("Invalid query completes once"), Receiver->QueryCount, 1);
	TestTrue(TEXT("Invalid query reports failure"), Receiver->LastQuery.Status == EBox3DBPQueryStatus::Failed);
	TestTrue(TEXT("Invalid query reports admission reason"), Receiver->LastQuery.Admission == EBox3DBPQueryAdmission::InvalidInput);
	TestEqual(TEXT("Failure retains correlation tag"), Receiver->LastQuery.UserTag, int64(42));

	FBox3DBPEventFilter Filter;
	TStrongObjectPtr<UBox3DBPEventSubscription> Subscription(
		UBox3DBPAsyncLibrary::SubscribeBox3DAsyncEvents(TestWorld->World, Receiver.Get(), Filter));
	if (!TestNotNull(TEXT("Event subscription"), Subscription.Get())) return false;
	Subscription->Delivered.AddDynamic(Receiver.Get(), &UBox3DAsyncBlueprintTestReceiver::OnEvent);
	const double Deadline = FPlatformTime::Seconds() + 5.0;
	while (Receiver->LastEvent.Status != EBox3DBPEventStatus::Baseline && FPlatformTime::Seconds() < Deadline)
	{
		Physics->Tick(0.0f);
		FPlatformProcess::Sleep(0.005f);
	}
	TestTrue(TEXT("Subscription delivers baseline"), Receiver->EventCount > 0
		&& Receiver->LastEvent.Status == EBox3DBPEventStatus::Baseline);
	Subscription->Stop();
	const int32 EventsAfterStop = Receiver->EventCount;
	Physics->Tick(0.0f);
	TestEqual(TEXT("Stopped subscription stays silent"), Receiver->EventCount, EventsAfterStop);

	Query.Start = FVector(-100, 0, 0); Query.End = FVector(100, 0, 0);
	TStrongObjectPtr<UBox3DBPAsyncQuery> Pending(UBox3DBPAsyncQuery::QueryBox3DAsync(TestWorld->World, Receiver.Get(), Query));
	Pending->Completed.AddDynamic(Receiver.Get(), &UBox3DAsyncBlueprintTestReceiver::OnQuery);
	Pending->Activate();
	Pending->Cancel();
	const double QueryDeadline = FPlatformTime::Seconds() + 5.0;
	while (Receiver->QueryCount < 2 && FPlatformTime::Seconds() < QueryDeadline)
	{
		Physics->PumpQueryCallbacks();
		FPlatformProcess::Sleep(0.005f);
	}
	TestEqual(TEXT("Cancelled query completes once"), Receiver->QueryCount, 2);
	TestTrue(TEXT("Cancelled status delivered"), Receiver->LastQuery.Status == EBox3DBPQueryStatus::Cancelled);
	Physics->PumpQueryCallbacks();
	TestEqual(TEXT("No duplicate query callback"), Receiver->QueryCount, 2);
	TStrongObjectPtr<UBox3DBPEventSubscription> Closing(
		UBox3DBPAsyncLibrary::SubscribeBox3DAsyncEvents(TestWorld->World, Receiver.Get(), Filter));
	if (!TestNotNull(TEXT("Closing subscription"), Closing.Get())) return false;
	Closing->Delivered.AddDynamic(Receiver.Get(), &UBox3DAsyncBlueprintTestReceiver::OnEvent);
	TStrongObjectPtr<UBox3DBPAsyncQuery> Ending(UBox3DBPAsyncQuery::QueryBox3DAsync(TestWorld->World, Receiver.Get(), Query));
	Ending->Completed.AddDynamic(Receiver.Get(), &UBox3DAsyncBlueprintTestReceiver::OnQuery);
	Ending->Activate();
	TestWorld.Reset();
	TestEqual(TEXT("Pending query closes once"), Receiver->QueryCount, 3);
	TestTrue(TEXT("Pending query reports world ended"), Receiver->LastQuery.Status == EBox3DBPQueryStatus::WorldEnded);
	TestTrue(TEXT("Subscription reports closure"), Receiver->LastEvent.Status == EBox3DBPEventStatus::Closed);
	Closing->Stop();
	return true;
}
#endif
