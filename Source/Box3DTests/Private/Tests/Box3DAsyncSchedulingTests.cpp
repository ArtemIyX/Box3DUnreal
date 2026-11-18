#include "World/Box3DAsyncSubsystem.h"
#include "Box3DTestHelpers.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
	template <typename T> bool WaitForAsyncCondition(T Predicate, double Seconds = 5.0)
	{
		const double Deadline = FPlatformTime::Seconds() + Seconds;
		do
		{
			if (Predicate()) return true;
			FPlatformProcess::Sleep(0.001f);
		} while (FPlatformTime::Seconds() < Deadline);
		return false;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBox3DAsyncCancellationPhasesTest,
	"Box3D.Async.CancellationPhases", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBox3DAsyncCancellationPhasesTest::RunTest(const FString& Parameters)
{
	Box3DTest::FTestWorld World(TEXT("AsyncCancellationPhases"));
	UBox3DAsyncSubsystem* Physics = World.World->GetSubsystem<UBox3DAsyncSubsystem>();
	if (!TestNotNull(TEXT("Async subsystem"), Physics)) return false;
	FBox3DAsyncQueryConsumer Consumer;
	if (!TestTrue(TEXT("Consumer registered"), Physics->TryRegisterQueryConsumer(Consumer))) return false;
	if (!TestTrue(TEXT("Initial step"), WaitForAsyncCondition([&] { return Physics->GetCompletedStepCount() > 0; }))) return false;
	FBox3DAsyncLineQuery Query;
	Query.Start = FVector(-100, 0, 0); Query.End = FVector(100, 0, 0);
	FBox3DAsyncQueryHandle Request;
	FBox3DAsyncQueryResult Result;

	Physics->SetQueryTestGate(1);
	if (!TestTrue(TEXT("Worker paused before selection"), WaitForAsyncCondition([&] { return Physics->GetQueryTestGateReached() == 1; })))
	{ Physics->SetQueryTestGate(0); return false; }
	TestTrue(TEXT("Queued request submitted"), Physics->TrySubmitQuery(Consumer, Query, Request));
	TestEqual(TEXT("Queued phase"), Physics->GetQueryTestPhase(Request), 0);
	TestTrue(TEXT("Queued cancellation wins"), Physics->TryCancelQuery(Consumer, Request));
	TestTrue(TEXT("Queued cancellation is available"), Physics->TryTakeQueryResult(Consumer, Request, Result));
	TestTrue(TEXT("Queued terminal cancelled"), Result.Status == EBox3DAsyncQueryStatus::Cancelled);
	Physics->SetQueryTestGate(0);
	WaitForAsyncCondition([&] { return Physics->GetQueryTestGateReached() == 0; });

	Physics->SetQueryTestGate(2);
	TestTrue(TEXT("Executing request submitted"), Physics->TrySubmitQuery(Consumer, Query, Request));
	if (!TestTrue(TEXT("Worker paused after selection"), WaitForAsyncCondition([&] { return Physics->GetQueryTestGateReached() == 2; })))
	{ Physics->SetQueryTestGate(0); return false; }
	TestEqual(TEXT("Executing phase"), Physics->GetQueryTestPhase(Request), 1);
	TestTrue(TEXT("Executing cancellation wins"), Physics->TryCancelQuery(Consumer, Request));
	TestFalse(TEXT("In-flight storage remains until worker releases it"), Physics->TryTakeQueryResult(Consumer, Request, Result));
	Physics->SetQueryTestGate(0);
	TestTrue(TEXT("Executing cancellation becomes ready"), WaitForAsyncCondition([&] { return Physics->GetQueryTestPhase(Request) == 2; }));
	TestTrue(TEXT("Take executing cancellation"), Physics->TryTakeQueryResult(Consumer, Request, Result));
	TestTrue(TEXT("Executing terminal cancelled"), Result.Status == EBox3DAsyncQueryStatus::Cancelled);

	TestTrue(TEXT("Ready request submitted"), Physics->TrySubmitQuery(Consumer, Query, Request));
	TestTrue(TEXT("Worker result ready before dispatch"), WaitForAsyncCondition([&] { return Physics->GetQueryTestPhase(Request) == 2; }));
	TestTrue(TEXT("Ready but uncommitted cancellation wins"), Physics->TryCancelQuery(Consumer, Request));
	TestTrue(TEXT("Take ready cancellation"), Physics->TryTakeQueryResult(Consumer, Request, Result));
	TestTrue(TEXT("Ready terminal cancelled"), Result.Status == EBox3DAsyncQueryStatus::Cancelled);

	TestTrue(TEXT("Committed request submitted"), Physics->TrySubmitQuery(Consumer, Query, Request));
	TestTrue(TEXT("Result ready"), WaitForAsyncCondition([&] { return Physics->GetQueryTestPhase(Request) == 2; }));
	Physics->PumpQueryCallbacks();
	TestFalse(TEXT("Cancellation loses after dispatch commits"), Physics->TryCancelQuery(Consumer, Request));
	TestTrue(TEXT("Committed result remains readable"), Physics->TryTakeQueryResult(Consumer, Request, Result));
	TestTrue(TEXT("Committed query reports miss"), Result.Status == EBox3DAsyncQueryStatus::Miss);
	TestFalse(TEXT("Terminal consumed once"), Physics->TryTakeQueryResult(Consumer, Request, Result));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBox3DAsyncRebaseSampleTest,
	"Box3D.Async.RebaseSample", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBox3DAsyncRebaseSampleTest::RunTest(const FString& Parameters)
{
	Box3DTest::FTestWorld World(TEXT("AsyncRebaseSample"));
	UBox3DAsyncSubsystem* Physics = World.World->GetSubsystem<UBox3DAsyncSubsystem>();
	if (!TestNotNull(TEXT("Async subsystem"), Physics)) return false;
	FBox3DAsyncQueryConsumer Consumer;
	if (!TestTrue(TEXT("Consumer registered"), Physics->TryRegisterQueryConsumer(Consumer))) return false;
	if (!TestTrue(TEXT("Initial step"), WaitForAsyncCondition([&] { return Physics->GetCompletedStepCount() > 0; }))) return false;
	FBox3DAsyncLineQuery Query;
	Query.Start = FVector(-100, 0, 0); Query.End = FVector(100, 0, 0);
	Physics->SetQueryTestGate(2);
	FBox3DAsyncQueryHandle Before;
	TestTrue(TEXT("Query accepted"), Physics->TrySubmitQuery(Consumer, Query, Before));
	if (!TestTrue(TEXT("Query selected"), WaitForAsyncCondition([&] { return Physics->GetQueryTestGateReached() == 2; })))
	{ Physics->SetQueryTestGate(0); return false; }
	const uint64 StepAtSelection = Physics->GetCompletedStepCount();
	// Keep the worker parked longer than its four-step catch-up window.
	FPlatformProcess::Sleep(0.20f);
	Physics->SetQueryTestGate(0);
	FBox3DAsyncQueryResult BeforeResult;
	TestTrue(TEXT("Selected query completes"), WaitForAsyncCondition([&] { return Physics->TryTakeQueryResult(Consumer, Before, BeforeResult); }));
	TestEqual(TEXT("Result reports selected real step"), BeforeResult.Sample.Step, StepAtSelection);
	TestTrue(TEXT("A rebase occurs"), WaitForAsyncCondition([&] { return Physics->GetDiagnostics().ClockRebases > 0; }));
	FBox3DAsyncQueryResult AfterResult;
	bool bObservedNewEpoch = false;
	const double Deadline = FPlatformTime::Seconds() + 5.0;
	while (FPlatformTime::Seconds() < Deadline && !bObservedNewEpoch)
	{
		FBox3DAsyncQueryHandle After;
		if (Physics->TrySubmitQuery(Consumer, Query, After)
			&& WaitForAsyncCondition([&] { return Physics->TryTakeQueryResult(Consumer, After, AfterResult); }))
			bObservedNewEpoch = AfterResult.Sample.ClockEpoch > BeforeResult.Sample.ClockEpoch;
	}
	TestTrue(TEXT("Subsequent real step carries new epoch"), bObservedNewEpoch);
	TestTrue(TEXT("Real step advanced"), AfterResult.Sample.Step > BeforeResult.Sample.Step);
	TestTrue(TEXT("Scheduled monotonic time advanced"), AfterResult.Sample.ScheduledMonotonicSeconds > BeforeResult.Sample.ScheduledMonotonicSeconds);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBox3DAsyncStaleWorldTest,
	"Box3D.Async.StaleWorldHandles", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBox3DAsyncStaleWorldTest::RunTest(const FString& Parameters)
{
	auto First = MakeUnique<Box3DTest::FTestWorld>(TEXT("AsyncStaleFirst"));
	UBox3DAsyncSubsystem* Old = First->World->GetSubsystem<UBox3DAsyncSubsystem>();
	if (!TestNotNull(TEXT("First async world"), Old)) return false;
	FBox3DAsyncQueryConsumer Previous;
	if (!TestTrue(TEXT("First consumer"), Old->TryRegisterQueryConsumer(Previous))) return false;
	FBox3DAsyncLineQuery Query;
	Query.Start = FVector(-100, 0, 0); Query.End = FVector(100, 0, 0);
	FBox3DAsyncQueryHandle OldRequest;
	if (!TestTrue(TEXT("First request"), Old->TrySubmitQuery(Previous, Query, OldRequest))) return false;
	First.Reset();
	Box3DTest::FTestWorld Second(TEXT("AsyncStaleSecond"));
	UBox3DAsyncSubsystem* Current = Second.World->GetSubsystem<UBox3DAsyncSubsystem>();
	if (!TestNotNull(TEXT("Second async world"), Current)) return false;
	FBox3DAsyncQueryConsumer Consumer;
	TestTrue(TEXT("Second consumer"), Current->TryRegisterQueryConsumer(Consumer));
	FBox3DAsyncQueryHandle NewRequest;
	FBox3DAsyncQueryResult Result;
	TestFalse(TEXT("Stale consumer rejected"), Current->TrySubmitQuery(Previous, Query, NewRequest));
	TestFalse(TEXT("Stale request cannot cancel"), Current->TryCancelQuery(Consumer, OldRequest));
	TestFalse(TEXT("Stale request cannot take"), Current->TryTakeQueryResult(Consumer, OldRequest, Result));
	TestTrue(TEXT("Fresh consumer works"), Current->TrySubmitQuery(Consumer, Query, NewRequest));
	return true;
}
#endif
