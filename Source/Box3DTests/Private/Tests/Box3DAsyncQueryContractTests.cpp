#include "World/Box3DAsyncSubsystem.h"
#include "Box3DTestHelpers.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBox3DAsyncQueryContractTest,
    "Box3D.Async.QueryContract", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBox3DAsyncQueryContractTest::RunTest(const FString& Parameters)
{
    Box3DTest::FTestWorld World(TEXT("AsyncQueryContract"));
    Box3DTest::FTestWorld OtherWorld(TEXT("AsyncQueryOther"));
    auto* Physics = World.World->GetSubsystem<UBox3DAsyncSubsystem>();
    auto* Other = OtherWorld.World->GetSubsystem<UBox3DAsyncSubsystem>();
    if (!TestNotNull(TEXT("Async subsystem"), Physics) || !TestNotNull(TEXT("Other world"), Other)) return false;
    FBox3DAsyncQueryConsumer Consumer, Foreign;
    if (!Physics->TryRegisterQueryConsumer(Consumer) || !Other->TryRegisterQueryConsumer(Foreign)) return false;
    FBox3DAsyncQueryDesc Query;
    Query.Start = FVector(-400, 0, 0); Query.End = FVector(400, 0, 0);
    FBox3DAsyncQueryHandle Handle;
    TestTrue(TEXT("Foreign world reported"), Physics->SubmitQuery(Foreign, Query, Handle) == EBox3DAsyncQueryAdmission::WrongWorld);
    TestEqual(TEXT("Foreign request has no handle"), Handle.Id, uint64(0));
    FBox3DAsyncQueryConsumer Invalid = Consumer; Invalid.Id = MAX_uint64;
    TestTrue(TEXT("Invalid consumer reported"), Physics->SubmitQuery(Invalid, Query, Handle) == EBox3DAsyncQueryAdmission::InvalidConsumer);
    Query.Kind = EBox3DAsyncQueryKind::Overlap;
    TestTrue(TEXT("Closest overlap unsupported"), Physics->SubmitQuery(Consumer, Query, Handle) == EBox3DAsyncQueryAdmission::Unsupported);
    Query.Kind = EBox3DAsyncQueryKind::Line;
    Query.End = Query.Start;
    TestTrue(TEXT("Zero cast is invalid"), Physics->SubmitQuery(Consumer, Query, Handle) == EBox3DAsyncQueryAdmission::InvalidInput);

    constexpr uint64 High = uint64(1) << 63;
    constexpr uint64 QueryBit = uint64(1) << 31;
    FBox3DAsyncShapeOptions Options;
    Options.CategoryBits = High; Options.MaskBits = QueryBit;
    FBox3DAsyncBodyId Near = 0, Middle = 0, Far = 0;
    // Reverse creation order so traversal/order assumptions are visible.
    if (!Physics->TryCreateStaticBox(FTransform(FVector(200, 0, 0)), FVector(25), Options, Far)
        || !Physics->TryCreateStaticBox(FTransform(FVector(0, 0, 0)), FVector(25), Options, Middle)
        || !Physics->TryCreateStaticBox(FTransform(FVector(-200, 0, 0)), FVector(25), Options, Near)) return false;
    int32 Created = 0;
    const double CreationDeadline = FPlatformTime::Seconds() + 5.0;
    while (FPlatformTime::Seconds() < CreationDeadline && Created < 3)
    {
        TArray<FBox3DAsyncCreationResult> Results; Physics->DrainCreationResults(Results);
        for (const auto& Result : Results) if (Result.bSucceeded && (Result.ResourceId == Near || Result.ResourceId == Middle || Result.ResourceId == Far)) ++Created;
        if (Created < 3) FPlatformProcess::Sleep(0.01f);
    }
    if (!TestEqual(TEXT("Fixtures created"), Created, 3)) return false;
    auto Execute = [this, Physics, Consumer](const FBox3DAsyncQueryDesc& Input, FBox3DAsyncQueryResult& Result)
    {
        FBox3DAsyncQueryHandle Request;
        if (!TestTrue(TEXT("Query accepted"), Physics->TrySubmitQuery(Consumer, Input, Request))) return false;
        const double Deadline = FPlatformTime::Seconds() + 5.0;
        while (FPlatformTime::Seconds() < Deadline)
        {
            if (Physics->TryTakeQueryResult(Consumer, Request, Result)) return true;
            FPlatformProcess::Sleep(0.005f);
        }
        AddError(TEXT("Timed out waiting for query")); return false;
    };
    Query.Start = FVector(-400, 0, 0); Query.End = FVector(400, 0, 0);
    Query.CategoryBits = QueryBit; Query.MaskBits = High; Query.Mode = EBox3DAsyncLineMode::Multi; Query.MaxHits = 2;
    FBox3DAsyncQueryResult Result;
    if (!Execute(Query, Result)) return false;
    TestTrue(TEXT("More distinct hits report truncation"), Result.Status == EBox3DAsyncQueryStatus::Truncated);
    if (TestEqual(TEXT("Best two hits"), Result.Hits.Num(), 2))
    {
        TestEqual(TEXT("Nearest first"), Result.Hits[0].BodyId, Near);
        TestEqual(TEXT("Second nearest"), Result.Hits[1].BodyId, Middle);
        TestTrue(TEXT("Fraction order"), Result.Hits[0].Fraction < Result.Hits[1].Fraction);
    }
    Query.MaskBits = uint64(1) << 62;
    if (!Execute(Query, Result)) return false;
    TestTrue(TEXT("Target mask independently rejects"), Result.Status == EBox3DAsyncQueryStatus::Miss);
    Query.MaskBits = High; Query.CategoryBits = uint64(1) << 30;
    if (!Execute(Query, Result)) return false;
    TestTrue(TEXT("Shape mask independently rejects"), Result.Status == EBox3DAsyncQueryStatus::Miss);

    Query.CategoryBits = QueryBit; Query.Mode = EBox3DAsyncLineMode::Closest; Query.MaxHits = 1;
    Query.Kind = EBox3DAsyncQueryKind::Sweep; Query.Shape = EBox3DAsyncQueryShape::Capsule;
    Query.Radius = 10; Query.HalfHeight = 40; Query.Rotation = FQuat(FVector::ForwardVector, PI / 4);
    if (!Execute(Query, Result)) return false;
    TestTrue(TEXT("Tilted capsule sweep hits"), Result.Status == EBox3DAsyncQueryStatus::Hit && Result.Hits.Num() == 1 && Result.Hits[0].BodyId == Near);
    Query.Kind = EBox3DAsyncQueryKind::Overlap; Query.Mode = EBox3DAsyncLineMode::Multi;
    Query.Shape = EBox3DAsyncQueryShape::Convex; Query.Start = FVector(-200, 0, 0);
    Query.ConvexPoints = {FVector(-10,-10,-10), FVector(10,-10,-10), FVector(0,10,-10), FVector(0,0,10)};
    if (!Execute(Query, Result)) return false;
    TestTrue(TEXT("Rotated convex overlap hits"), Result.Status == EBox3DAsyncQueryStatus::Hit && Result.Hits.Num() == 1 && Result.Hits[0].BodyId == Near);
    FBox3DAsyncBodyId Distant = 0;
    const FVector DistantCenter(10000000, 0, 0);
    if (!TestTrue(TEXT("Large-coordinate box accepted"), Physics->TryCreateStaticBox(FTransform(DistantCenter), FVector(100), Options, Distant))) return false;
    bool bDistantCreated = false;
    const double DistantDeadline = FPlatformTime::Seconds() + 5.0;
    while (!bDistantCreated && FPlatformTime::Seconds() < DistantDeadline)
    {
        TArray<FBox3DAsyncCreationResult> Results; Physics->DrainCreationResults(Results);
        for (const auto& Creation : Results) if (Creation.ResourceId == Distant) bDistantCreated = Creation.bSucceeded;
        if (!bDistantCreated) FPlatformProcess::Sleep(0.005f);
    }
    if (!TestTrue(TEXT("Large-coordinate fixture created"), bDistantCreated)) return false;
    Query.Kind = EBox3DAsyncQueryKind::Line; Query.Mode = EBox3DAsyncLineMode::Closest;
    Query.Start = DistantCenter - FVector(400, 0, 0); Query.End = DistantCenter + FVector(400, 0, 0);
    if (!Execute(Query, Result)) return false;
    TestTrue(TEXT("Large-coordinate hit and fraction"), Result.Status == EBox3DAsyncQueryStatus::Hit
        && Result.Hits.Num() == 1 && Result.Hits[0].BodyId == Distant
        && FMath::IsNearlyEqual(Result.Hits[0].Fraction, 0.375f, 0.01f));
    Physics->UnregisterQueryConsumer(Consumer); Other->UnregisterQueryConsumer(Foreign);
    return true;
}
#endif
