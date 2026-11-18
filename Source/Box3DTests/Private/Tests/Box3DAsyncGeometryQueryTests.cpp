#include "World/Box3DAsyncSubsystem.h"
#include "Box3DTestHelpers.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBox3DAsyncGeometryQueriesTest,
	"Box3D.Async.GeometryQueries", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBox3DAsyncGeometryQueriesTest::RunTest(const FString& Parameters)
{
	Box3DTest::FTestWorld TestWorld(TEXT("AsyncGeometryQueries"));
	UBox3DAsyncSubsystem* Physics = TestWorld.World->GetSubsystem<UBox3DAsyncSubsystem>();
	if (!TestNotNull(TEXT("Async subsystem"), Physics)) return false;
	FBox3DAsyncQueryConsumer Consumer;
	if (!TestTrue(TEXT("Register consumer"), Physics->TryRegisterQueryConsumer(Consumer))) return false;
	FBox3DAsyncBodyId Body = 0;
	if (!TestTrue(TEXT("Create box"), Physics->TryCreateStaticBox(FTransform::Identity, FVector(50), Body))) return false;
	FBox3DAsyncShapeId ShapeId = 0;
	const double CreationDeadline = FPlatformTime::Seconds() + 5.0;
	while (FPlatformTime::Seconds() < CreationDeadline && ShapeId == 0)
	{
		TArray<FBox3DAsyncCreationResult> Results;
		Physics->DrainCreationResults(Results);
		for (const auto& Result : Results)
			if (Result.ResourceId == Body && Result.bSucceeded && Result.ShapeIds.Num() == 1) ShapeId = Result.ShapeIds[0];
		if (ShapeId == 0) FPlatformProcess::Sleep(0.01f);
	}
	if (!TestTrue(TEXT("Box shape acknowledged"), ShapeId != 0)) return false;
	auto Execute = [this, Physics, Consumer](const FBox3DAsyncQueryDesc& Query, FBox3DAsyncQueryResult& Result)
	{
		FBox3DAsyncQueryHandle Request;
		if (!TestTrue(TEXT("Query accepted"), Physics->TrySubmitQuery(Consumer, Query, Request))) return false;
		const double Deadline = FPlatformTime::Seconds() + 5.0;
		while (FPlatformTime::Seconds() < Deadline)
		{
			if (Physics->TryTakeQueryResult(Consumer, Request, Result)) return true;
			FPlatformProcess::Sleep(0.01f);
		}
		AddError(TEXT("Query did not finish"));
		return false;
	};
	FBox3DAsyncQueryDesc Query;
	Query.Kind = EBox3DAsyncQueryKind::Sweep;
	Query.Shape = EBox3DAsyncQueryShape::Sphere;
	Query.Radius = 25;
	Query.Start = FVector(-200, 0, 0);
	Query.End = FVector(200, 0, 0);
	FBox3DAsyncQueryResult Result;
	if (!Execute(Query, Result)) return false;
	TestTrue(TEXT("Sphere sweep hit"), Result.Status == EBox3DAsyncQueryStatus::Hit);
	TestTrue(TEXT("Sample is a real completed step"), Result.Sample.Step > 0 && Result.Sample.Step == Result.SampledStep);
	TestTrue(TEXT("Sample has scheduled monotonic time"), Result.Sample.ScheduledMonotonicSeconds > 0.0);
	if (TestEqual(TEXT("One sweep hit"), Result.Hits.Num(), 1))
	{
		TestEqual(TEXT("Sweep shape ID"), Result.Hits[0].ShapeId, ShapeId);
		TestTrue(TEXT("Sweep fraction near analytic result"), FMath::IsNearlyEqual(Result.Hits[0].Fraction, 0.3125f, 0.01f));
	}
	Query.Kind = EBox3DAsyncQueryKind::Overlap;
	Query.Mode = EBox3DAsyncLineMode::Multi;
	Query.Shape = EBox3DAsyncQueryShape::Box;
	Query.HalfExtents = FVector(10, 30, 10);
	Query.Rotation = FQuat(FVector::UpVector, PI / 4);
	Query.Start = FVector(45, 0, 0);
	if (!Execute(Query, Result)) return false;
	TestTrue(TEXT("Oriented overlap hit"), Result.Status == EBox3DAsyncQueryStatus::Hit);
	if (TestEqual(TEXT("One overlap"), Result.Hits.Num(), 1)) TestEqual(TEXT("Overlap shape ID"), Result.Hits[0].ShapeId, ShapeId);
	Query.Kind = EBox3DAsyncQueryKind::AabbCandidates;
	Query.Start = FVector(-55);
	Query.End = FVector(55);
	if (!Execute(Query, Result)) return false;
	TestTrue(TEXT("AABB candidate hit"), Result.Status == EBox3DAsyncQueryStatus::Hit);
	TestTrue(TEXT("Candidate result kind"), Result.Kind == EBox3DAsyncQueryKind::AabbCandidates);
	Query.Kind = EBox3DAsyncQueryKind::Sweep;
	Query.Shape = EBox3DAsyncQueryShape::Capsule;
	Query.Radius = 20;
	Query.HalfHeight = 10;
	FBox3DAsyncQueryHandle Rejected;
	TestFalse(TEXT("Invalid capsule rejected"), Physics->TrySubmitQuery(Consumer, Query, Rejected));
	Query.Shape = EBox3DAsyncQueryShape::Convex;
	Query.ConvexPoints.SetNum(65);
	TestFalse(TEXT("Oversized convex rejected"), Physics->TrySubmitQuery(Consumer, Query, Rejected));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBox3DAsyncQueryAdmissionTest,
	"Box3D.Async.QueryAdmission", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBox3DAsyncQueryAdmissionTest::RunTest(const FString& Parameters)
{
	Box3DTest::FTestWorld TestWorld(TEXT("AsyncQueryAdmission"));
	UBox3DAsyncSubsystem* Physics = TestWorld.World->GetSubsystem<UBox3DAsyncSubsystem>();
	if (!TestNotNull(TEXT("Async subsystem"), Physics)) return false;
	FBox3DAsyncQueryConsumer First, Second;
	if (!TestTrue(TEXT("First consumer"), Physics->TryRegisterQueryConsumer(First))
		|| !TestTrue(TEXT("Second consumer"), Physics->TryRegisterQueryConsumer(Second))) return false;
	FBox3DAsyncQueryDesc Query;
	Query.Start = FVector(-100, 0, 0);
	Query.End = FVector(100, 0, 0);
	FBox3DAsyncQueryHandle Handle;
	Query.ConvexPoints.SetNum(65);
	TestFalse(TEXT("Unused oversized convex array rejected for line"), Physics->TrySubmitQuery(First, Query, Handle));
	TestEqual(TEXT("Rejected query has no ID"), Handle.Id, uint64(0));
	Query.Kind = EBox3DAsyncQueryKind::AabbCandidates;
	Query.Mode = EBox3DAsyncLineMode::Multi;
	TestFalse(TEXT("Unused oversized convex array rejected for AABB"), Physics->TrySubmitQuery(First, Query, Handle));
	Query.Kind = EBox3DAsyncQueryKind::Line;
	Query.Mode = EBox3DAsyncLineMode::Closest;
	Query.ConvexPoints.Reset();
	TArray<FBox3DAsyncQueryHandle> FirstHandles;
	for (int32 Index = 0; Index < UBox3DAsyncSubsystem::QueryCapacityPerConsumer; ++Index)
	{
		if (!TestTrue(TEXT("Per-consumer slot accepted"), Physics->TrySubmitQuery(First, Query, Handle))) return false;
		FirstHandles.Add(Handle);
	}
	TestFalse(TEXT("First consumer cannot exceed quota"), Physics->TrySubmitQuery(First, Query, Handle));
	TestEqual(TEXT("Quota rejection has no ID"), Handle.Id, uint64(0));
	FBox3DAsyncQueryHandle OtherHandle;
	TestTrue(TEXT("Other consumer retains capacity"), Physics->TrySubmitQuery(Second, Query, OtherHandle));
	FBox3DAsyncQueryResult Result;
	const double Deadline = FPlatformTime::Seconds() + 5.0;
	bool bTaken = false;
	while (FPlatformTime::Seconds() < Deadline && !(bTaken = Physics->TryTakeQueryResult(First, FirstHandles[0], Result)))
		FPlatformProcess::Sleep(0.01f);
	if (!TestTrue(TEXT("One reservation consumed"), bTaken)) return false;
	TestTrue(TEXT("Consumption releases quota"), Physics->TrySubmitQuery(First, Query, Handle));
	Physics->UnregisterQueryConsumer(First);
	Physics->UnregisterQueryConsumer(Second);
	return true;
}
#endif
