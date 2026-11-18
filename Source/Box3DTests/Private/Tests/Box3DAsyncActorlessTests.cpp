#include "World/Box3DAsyncSubsystem.h"
#include "Box3DTestHelpers.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBox3DAsyncActorlessCompoundTest,
	"Box3D.Async.ActorlessCompound", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBox3DAsyncActorlessCompoundTest::RunTest(const FString& Parameters)
{
	Box3DTest::FTestWorld World(TEXT("AsyncActorlessCompound"));
	UBox3DAsyncSubsystem* Physics = World.World->GetSubsystem<UBox3DAsyncSubsystem>();
	if (!TestNotNull(TEXT("Async subsystem"), Physics)) return false;
	FBox3DAsyncCreationConsumer A, B;
	if (!TestTrue(TEXT("Register A"), Physics->TryRegisterCreationConsumer(A))
		|| !TestTrue(TEXT("Register B"), Physics->TryRegisterCreationConsumer(B))) return false;
	FBox3DAsyncBodyDesc Desc;
	FBox3DAsyncBodyShapeDesc Sphere;
	Sphere.Kind = EBox3DAsyncBodyShapeKind::Sphere;
	Sphere.Radius = 30;
	Sphere.LocalTransform.SetLocation(FVector(-80, 0, 0));
	Sphere.Options.CategoryBits = 1ull << 40;
	Sphere.Options.MaskBits = MAX_uint64;
	Sphere.SubshapeKey = 101;
	FBox3DAsyncBodyShapeDesc Box;
	Box.Kind = EBox3DAsyncBodyShapeKind::Box;
	Box.HalfExtents = FVector(20, 30, 40);
	Box.LocalTransform.SetLocation(FVector(80, 0, 0));
	Box.Options.CategoryBits = 1ull << 41;
	Box.SubshapeKey = 202;
	Desc.Shapes = {Sphere, Box};
	FBox3DAsyncBodyHandle Body;
	if (!TestTrue(TEXT("Compound accepted"), Physics->TryCreateBody(A, Desc, Body))) return false;
	FBox3DAsyncCreationResult Result;
	TestFalse(TEXT("Wrong owner cannot consume"), Physics->TryTakeCreationResult(B, Body, Result));
	const double Deadline = FPlatformTime::Seconds() + 5.0;
	bool bReady = false;
	while (FPlatformTime::Seconds() < Deadline && !(bReady = Physics->TryTakeCreationResult(A, Body, Result)))
		FPlatformProcess::Sleep(0.01f);
	if (!TestTrue(TEXT("Owned acknowledgment arrives"), bReady)) return false;
	if (!TestTrue(TEXT("Compound created"), Result.bSucceeded)) return false;
	if (!TestEqual(TEXT("Two identities"), Result.ShapeHandles.Num(), 2)
		|| !TestEqual(TEXT("Two authored keys"), Result.SubshapeKeys.Num(), 2)) return false;
	TestTrue(TEXT("Distinct shape IDs"), Result.ShapeHandles[0].Id != Result.ShapeHandles[1].Id);
	TestTrue(TEXT("Both keys preserved"), Result.SubshapeKeys.Contains(101) && Result.SubshapeKeys.Contains(202));
	TestTrue(TEXT("Shape identities have originating world"), Result.ShapeHandles[0].World == Body.World && Result.ShapeHandles[1].World == Body.World);
	TArray<FBox3DAsyncCreationResult> Legacy;
	Physics->DrainCreationResults(Legacy);
	TestTrue(TEXT("Owned acknowledgment not in legacy drain"), Legacy.IsEmpty());

	FBox3DAsyncQueryConsumer QueryOwner;
	if (!TestTrue(TEXT("Register query owner"), Physics->TryRegisterQueryConsumer(QueryOwner))) return false;
	FBox3DAsyncQueryDesc Query;
	Query.Start = FVector(-200, 0, 0);
	Query.End = FVector(200, 0, 0);
	Query.Mode = EBox3DAsyncLineMode::Multi;
	Query.MaxHits = 4;
	Query.MaskBits = 1ull << 41;
	FBox3DAsyncQueryHandle Request;
	if (!TestTrue(TEXT("Filtered query accepted"), Physics->TrySubmitQuery(QueryOwner, Query, Request))) return false;
	FBox3DAsyncQueryResult QueryResult;
	const double QueryDeadline = FPlatformTime::Seconds() + 5.0;
	bool bQueryReady = false;
	while (FPlatformTime::Seconds() < QueryDeadline && !(bQueryReady = Physics->TryTakeQueryResult(QueryOwner, Request, QueryResult)))
		FPlatformProcess::Sleep(0.01f);
	if (!TestTrue(TEXT("Query completes"), bQueryReady)) return false;
	if (TestEqual(TEXT("Only box passes category filter"), QueryResult.Hits.Num(), 1))
	{
		const int32 BoxIndex = Result.SubshapeKeys.IndexOfByKey(uint64(202));
		TestTrue(TEXT("Query resolves box shape"), BoxIndex != INDEX_NONE && QueryResult.Hits[0].ShapeId == Result.ShapeHandles[BoxIndex].Id);
	}

	// The first primitive is created before the coplanar hull fails. The body and
	// all of its shapes must be rolled back without publishing partial identity.
	FBox3DAsyncBodyDesc Broken;
	Broken.Transform.SetLocation(FVector(1000, 0, 0));
	FBox3DAsyncBodyShapeDesc Flat;
	Flat.Kind = EBox3DAsyncBodyShapeKind::Convex;
	Flat.ConvexPoints = {FVector(-20, -20, 0), FVector(20, -20, 0), FVector(20, 20, 0), FVector(-20, 20, 0)};
	Broken.Shapes = {Sphere, Flat};
	FBox3DAsyncBodyHandle BrokenBody;
	if (!TestTrue(TEXT("Degenerate hull submitted for native cook"), Physics->TryCreateBody(B, Broken, BrokenBody))) return false;
	FBox3DAsyncCreationResult BrokenResult;
	const double BrokenDeadline = FPlatformTime::Seconds() + 5.0;
	bool bBrokenReady = false;
	while (FPlatformTime::Seconds() < BrokenDeadline && !(bBrokenReady = Physics->TryTakeCreationResult(B, BrokenBody, BrokenResult)))
		FPlatformProcess::Sleep(0.01f);
	if (TestTrue(TEXT("Failed compound completes"), bBrokenReady))
	{
		TestFalse(TEXT("Failed compound is not published"), BrokenResult.bSucceeded);
		TestTrue(TEXT("No partial shape identities"), BrokenResult.ShapeHandles.IsEmpty() && BrokenResult.SubshapeKeys.IsEmpty());
		TestFalse(TEXT("Failed body cannot be removed"), Physics->TryRemoveBody(BrokenBody.Id));
	}
	return true;
}
#endif
