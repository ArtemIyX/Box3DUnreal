#include "Misc/AutomationTest.h"
#include "Box3DTestHelpers.h"
#include "World/Box3DAsyncSubsystem.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBox3DAsyncQueryDiagnosticsTest,
	"Box3D.Async.QueryDiagnostics", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBox3DAsyncQueryDiagnosticsTest::RunTest(const FString& Parameters)
{
	Box3DTest::FTestWorld World(TEXT("AsyncQueryDiagnostics"));
	UBox3DAsyncSubsystem* Physics = World.World->GetSubsystem<UBox3DAsyncSubsystem>();
	if (!TestNotNull(TEXT("Async subsystem"), Physics)) return false;
	FBox3DAsyncQueryConsumer Consumer;
	if (!TestTrue(TEXT("Register consumer"), Physics->TryRegisterQueryConsumer(Consumer))) return false;
	FBox3DAsyncQueryDesc Query;
	Query.Start = FVector(-100, 0, 0);
	Query.End = FVector(100, 0, 0);
	FBox3DAsyncQueryHandle Request;
	Query.ConvexPoints.SetNum(65);
	TestFalse(TEXT("Oversized unused data rejected"), Physics->TrySubmitQuery(Consumer, Query, Request));
	Query.ConvexPoints.Reset();
	if (!TestTrue(TEXT("Valid query accepted"), Physics->TrySubmitQuery(Consumer, Query, Request))) return false;
	FBox3DAsyncDiagnostics Pending = Physics->GetDiagnostics();
	TestEqual(TEXT("One acceptance"), Pending.AcceptedQueries, uint64(1));
	TestEqual(TEXT("One rejection"), Pending.RejectedQueries, uint64(1));
	TestEqual(TEXT("One outstanding query"), Pending.OutstandingQueries, 1);
	TestTrue(TEXT("Reservation is charged"), Pending.QueryReservedBytes > 0);
	FBox3DAsyncQueryResult Result;
	const double Deadline = FPlatformTime::Seconds() + 5.0;
	bool bTaken = false;
	while (FPlatformTime::Seconds() < Deadline && !(bTaken = Physics->TryTakeQueryResult(Consumer, Request, Result)))
		FPlatformProcess::Sleep(0.005f);
	if (!TestTrue(TEXT("Result completed"), bTaken)) return false;
	const FBox3DAsyncDiagnostics Done = Physics->GetDiagnostics();
	TestEqual(TEXT("One native query executed"), Done.ExecutedQueries, uint64(1));
	TestEqual(TEXT("One result delivered"), Done.DeliveredQueryResults, uint64(1));
	TestEqual(TEXT("Reservation released"), Done.QueryReservedBytes, uint64(0));
	TestEqual(TEXT("No outstanding queries"), Done.OutstandingQueries, 0);
	TestTrue(TEXT("High-water marks retained"), Done.PeakOutstandingQueries >= 1 && Done.PeakQueryReservedBytes >= Pending.QueryReservedBytes);
	TestTrue(TEXT("Timing recorded"), Done.TotalQueryExecutionSeconds >= Done.MaxQueryExecutionSeconds
		&& Done.MaxQueryExecutionSeconds >= 0.0 && Done.TotalQueryLatencySeconds >= Done.MaxQueryLatencySeconds
		&& Done.MaxQueryLatencySeconds > 0.0);
	return true;
}
#endif
