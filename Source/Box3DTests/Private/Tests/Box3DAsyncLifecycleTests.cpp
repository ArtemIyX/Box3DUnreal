#include "World/Box3DAsyncSubsystem.h"
#include "Box3DTestHelpers.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBox3DAsyncQueryLifecycleTest,
	"Box3D.Async.QueryLifecycle", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBox3DAsyncQueryLifecycleTest::RunTest(const FString& Parameters)
{
	auto TestWorld = MakeUnique<Box3DTest::FTestWorld>(TEXT("AsyncQueryLifecycle"));
	UBox3DAsyncSubsystem* Physics = TestWorld->World->GetSubsystem<UBox3DAsyncSubsystem>();
	if (!TestNotNull(TEXT("Async subsystem"), Physics)) return false;
	FBox3DAsyncQueryConsumer Consumer;
	if (!TestTrue(TEXT("Consumer registered"), Physics->TryRegisterQueryConsumer(Consumer))) return false;
	FBox3DAsyncLineQuery Query;
	Query.Start = FVector(-100, 0, 0);
	Query.End = FVector(100, 0, 0);
	FBox3DAsyncQueryHandle Cancelled;
	if (!TestTrue(TEXT("Request accepted"), Physics->TrySubmitQuery(Consumer, Query, Cancelled))) return false;
	int32 CallbackCount = 0;
	EBox3DAsyncQueryStatus CallbackStatus = EBox3DAsyncQueryStatus::Miss;
	TestTrue(TEXT("Callback attached"), Physics->TrySetQueryCallback(Consumer, Cancelled, GetTransientPackage(),
		[&](const FBox3DAsyncQueryResult& Result) { ++CallbackCount; CallbackStatus = Result.Status; }));
	TestTrue(TEXT("Cancellation claims pending request"), Physics->TryCancelQuery(Consumer, Cancelled));
	const double Deadline = FPlatformTime::Seconds() + 5.0;
	while (CallbackCount == 0 && FPlatformTime::Seconds() < Deadline)
	{
		Physics->PumpQueryCallbacks();
		FPlatformProcess::Sleep(0.005f);
	}
	TestEqual(TEXT("Callback invoked exactly once"), CallbackCount, 1);
	TestTrue(TEXT("Callback sees cancellation"), CallbackStatus == EBox3DAsyncQueryStatus::Cancelled);
	TestFalse(TEXT("Consumed cancellation cannot be cancelled again"), Physics->TryCancelQuery(Consumer, Cancelled));

	FBox3DAsyncQueryHandle Ending;
	if (!TestTrue(TEXT("Shutdown request accepted"), Physics->TrySubmitQuery(Consumer, Query, Ending))) return false;
	bool bNotified = false;
	bool bTookResult = false;
	EBox3DAsyncQueryStatus ShutdownStatus = EBox3DAsyncQueryStatus::Miss;
	TestTrue(TEXT("Shutdown notification registered"), Physics->SetQueryShutdownCallback(Consumer, GetTransientPackage(), [&]()
	{
		bNotified = true;
		FBox3DAsyncQueryResult Result;
		bTookResult = Physics->TryTakeQueryResult(Consumer, Ending, Result);
		if (bTookResult) ShutdownStatus = Result.Status;
	}));
	TestWorld.Reset();
	TestTrue(TEXT("Polling owner notified during shutdown"), bNotified);
	TestTrue(TEXT("Polling owner can take terminal result"), bTookResult);
	TestTrue(TEXT("Uncommitted request ends with world"), ShutdownStatus == EBox3DAsyncQueryStatus::WorldEnded);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBox3DAsyncQueryReentrantShutdownTest,
	"Box3D.Async.QueryReentrantShutdown", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBox3DAsyncQueryReentrantShutdownTest::RunTest(const FString& Parameters)
{
	auto TestWorld = MakeUnique<Box3DTest::FTestWorld>(TEXT("AsyncReentrantShutdown"));
	UBox3DAsyncSubsystem* Physics = TestWorld->World->GetSubsystem<UBox3DAsyncSubsystem>();
	if (!TestNotNull(TEXT("Async subsystem"), Physics)) return false;
	FBox3DAsyncQueryConsumer Consumer;
	if (!TestTrue(TEXT("Consumer registered"), Physics->TryRegisterQueryConsumer(Consumer))) return false;
	FBox3DAsyncLineQuery Query;
	Query.Start = FVector(-100, 0, 0);
	Query.End = FVector(100, 0, 0);
	FBox3DAsyncQueryHandle First, Second, Third;
	if (!TestTrue(TEXT("First request accepted"), Physics->TrySubmitQuery(Consumer, Query, First))
		|| !TestTrue(TEXT("Second request accepted"), Physics->TrySubmitQuery(Consumer, Query, Second))) return false;
	int32 FirstCount = 0, SecondCount = 0, ThirdCount = 0;
	EBox3DAsyncQueryStatus SecondStatus = EBox3DAsyncQueryStatus::Miss;
	EBox3DAsyncQueryStatus ThirdStatus = EBox3DAsyncQueryStatus::Miss;
	TestTrue(TEXT("First callback attached"), Physics->TrySetQueryCallback(Consumer, First, GetTransientPackage(),
		[&](const FBox3DAsyncQueryResult&)
		{
			++FirstCount;
			TestTrue(TEXT("Callback submits third request"), Physics->TrySubmitQuery(Consumer, Query, Third));
			TestTrue(TEXT("Third callback attached"), Physics->TrySetQueryCallback(Consumer, Third, GetTransientPackage(),
				[&](const FBox3DAsyncQueryResult& Result) { ++ThirdCount; ThirdStatus = Result.Status; }));
			TestWorld.Reset();
		}));
	TestTrue(TEXT("Second callback attached"), Physics->TrySetQueryCallback(Consumer, Second, GetTransientPackage(),
		[&](const FBox3DAsyncQueryResult& Result) { ++SecondCount; SecondStatus = Result.Status; }));
	TestTrue(TEXT("First cancellation committed"), Physics->TryCancelQuery(Consumer, First));
	TestTrue(TEXT("Second cancellation committed"), Physics->TryCancelQuery(Consumer, Second));
	const double Deadline = FPlatformTime::Seconds() + 5.0;
	while (FirstCount == 0 && FPlatformTime::Seconds() < Deadline)
	{
		Physics->PumpQueryCallbacks();
		if (FirstCount == 0) FPlatformProcess::Sleep(0.005f);
	}
	TestEqual(TEXT("First callback delivered once"), FirstCount, 1);
	TestEqual(TEXT("Second callback delivered during shutdown once"), SecondCount, 1);
	TestEqual(TEXT("Third callback delivered during shutdown once"), ThirdCount, 1);
	TestTrue(TEXT("Committed cancellation survives shutdown"), SecondStatus == EBox3DAsyncQueryStatus::Cancelled);
	TestTrue(TEXT("Uncommitted request reports world ended"), ThirdStatus == EBox3DAsyncQueryStatus::WorldEnded);
	return true;
}
#endif
