#include "World/Box3DAsyncSubsystem.h"
#include "Box3DTestHelpers.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBox3DAsyncQualifiedIdentityTest,
    "Box3D.Async.QualifiedIdentity", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBox3DAsyncQualifiedIdentityTest::RunTest(const FString& Parameters)
{
    Box3DTest::FTestWorld First(TEXT("QualifiedFirst"));
    Box3DTest::FTestWorld Second(TEXT("QualifiedSecond"));
    auto* A = First.World->GetSubsystem<UBox3DAsyncSubsystem>();
    auto* B = Second.World->GetSubsystem<UBox3DAsyncSubsystem>();
    if (!TestNotNull(TEXT("First subsystem"), A) || !TestNotNull(TEXT("Second subsystem"), B)) return false;
    FBox3DAsyncBodyHandle Body;
    FBox3DAsyncShapeOptions Options;
    if (!TestTrue(TEXT("Create qualified box"), A->TryCreateStaticBox(FTransform::Identity, FVector(50), Options, Body))) return false;
    FBox3DAsyncShapeHandle Shape;
    const double Deadline = FPlatformTime::Seconds() + 5;
    while (Shape.Id == 0 && FPlatformTime::Seconds() < Deadline)
    {
        TArray<FBox3DAsyncCreationResult> Results;
        A->DrainCreationResults(Results);
        for (const auto& Result : Results)
            if (Result.ResourceId == Body.Id && Result.bSucceeded && Result.ShapeHandles.Num() == 1)
            {
                TestTrue(TEXT("Creation body qualified"), Result.BodyHandle.World == Body.World && Result.BodyHandle.Id == Body.Id);
                Shape = Result.ShapeHandles[0];
            }
        if (!Shape.Id) FPlatformProcess::Sleep(0.01f);
    }
    if (!TestTrue(TEXT("Shape identity acknowledged"), Shape.Id != 0 && Shape.World == Body.World)) return false;
    TestFalse(TEXT("Foreign remove rejected"), B->TryRemoveBody(Body));
    FBox3DAsyncEventFilter Filter;
    Filter.ShapeHandle = Shape;
    FBox3DAsyncEventSubscription Subscription;
    TestFalse(TEXT("Foreign event filter rejected"), B->SubscribeEvents(Filter, Subscription));
    TestTrue(TEXT("Local event filter accepted"), A->SubscribeEvents(Filter, Subscription));
    FBox3DAsyncQueryConsumer Consumer;
    if (!TestTrue(TEXT("Register consumer"), B->TryRegisterQueryConsumer(Consumer))) return false;
    FBox3DAsyncLineQuery Query;
    Query.Start = FVector(-200, 0, 0); Query.End = FVector(200, 0, 0);
    Query.IgnoredBodyHandles.Add(Body);
    FBox3DAsyncQueryHandle Request;
    TestFalse(TEXT("Foreign ignored body rejected"), B->TrySubmitQuery(Consumer, Query, Request));
    if (!TestTrue(TEXT("Local consumer"), A->TryRegisterQueryConsumer(Consumer))) return false;
    TestTrue(TEXT("Local ignored body accepted"), A->TrySubmitQuery(Consumer, Query, Request));
    FBox3DAsyncQueryResult Result;
    bool bReady = false;
    while (FPlatformTime::Seconds() < Deadline && !(bReady = A->TryTakeQueryResult(Consumer, Request, Result))) FPlatformProcess::Sleep(0.01f);
    TestTrue(TEXT("Qualified ignored body excluded"), bReady && Result.Status == EBox3DAsyncQueryStatus::Miss);
    Query.IgnoredBodyHandles.Reset();
    if (!TestTrue(TEXT("Local hit query accepted"), A->TrySubmitQuery(Consumer, Query, Request))) return false;
    bReady = false;
    while (FPlatformTime::Seconds() < Deadline && !(bReady = A->TryTakeQueryResult(Consumer, Request, Result))) FPlatformProcess::Sleep(0.01f);
    if (TestTrue(TEXT("Local hit received"), bReady && Result.Hits.Num() == 1))
    {
        TestTrue(TEXT("Hit body handle qualified"), Result.Hits[0].BodyHandle.World == Body.World && Result.Hits[0].BodyHandle.Id == Body.Id);
        TestTrue(TEXT("Hit shape handle qualified"), Result.Hits[0].ShapeHandle.World == Shape.World && Result.Hits[0].ShapeHandle.Id == Shape.Id);
    }
    return true;
}
#endif
