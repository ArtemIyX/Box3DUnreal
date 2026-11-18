#include "Box3DIrisReplication.h"
#include "Box3DAsyncPresentationActor.h"
#include "HAL/UnrealMemory.h"
#include "Misc/AutomationTest.h"
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBox3DIrisCentimeterQuantizationTest,
	"BoxPhysicsExample.Iris.CentimeterQuantization", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBox3DIrisCentimeterQuantizationTest::RunTest(const FString& Parameters)
{
	FVector Quantized = FVector::ZeroVector;
	TestTrue(TEXT("Finite positions are quantized"), Box3DIrisReplication::TryQuantizePositionToCentimeter(
		FVector(-12.5, 0.49, 99.5), Quantized));
	TestEqual(TEXT("X preserves symmetric rounding"), Quantized.X, -13.0);
	TestEqual(TEXT("Y rounds below the grid to zero"), Quantized.Y, 0.0);
	TestEqual(TEXT("Z rounds to whole centimeters"), Quantized.Z, 100.0);
	TestTrue(TEXT("Finite positions beyond 32-bit centimeters remain representable"),
		Box3DIrisReplication::TryQuantizePositionToCentimeter(FVector(2147483648.0, -9000000000000.0, 0.0), Quantized));
	TestEqual(TEXT("Large positive position is not clipped"), Quantized.X, 2147483648.0);
	TestEqual(TEXT("Large negative position is not clipped"), Quantized.Y, -9000000000000.0);
	TestFalse(TEXT("Non finite values are rejected"),
		Box3DIrisReplication::TryQuantizePositionToCentimeter(
			FVector(std::numeric_limits<double>::quiet_NaN(), 0.0, 0.0), Quantized));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBox3DIrisCoordinateAndCollisionPolicyTest,
	"BoxPhysicsExample.Iris.CoordinateAndCollisionPolicy", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBox3DIrisCoordinateAndCollisionPolicyTest::RunTest(const FString& Parameters)
{
	const FVector ServerLocal(1200.0, -35.0, 900.0);
	const FVector ServerOrigin(1000000000000.0, -2000000000000.0, 500000000000.0);
	const FVector WireOrigin(900000000000.0, -2100000000000.0, 400000000000.0);
	const FVector Absolute = Box3DIrisReplication::ToAbsoluteWorldPosition(ServerLocal, ServerOrigin);
	FVector QuantizedOffset;
	TestTrue(TEXT("Large signed map-relative offset quantizes"),
		Box3DIrisReplication::TryQuantizePositionToCentimeter(Absolute - WireOrigin, QuantizedOffset));
	const FVector_NetQuantize WirePosition(QuantizedOffset);
	const FVector ReconstructedAbsolute = Box3DIrisReplication::ToAbsoluteWorldPosition(FVector(WirePosition), WireOrigin);
	TestTrue(TEXT("Position reconstructs in the absolute map frame"), ReconstructedAbsolute.Equals(Absolute, 0.5));
	const FVector ClientOrigin(-300000000000.0, 400000000000.0, -100000000000.0);
	const FVector ClientLocal = Box3DIrisReplication::ToLocalWorldPosition(ReconstructedAbsolute, ClientOrigin);
	TestTrue(TEXT("Client presentation converts absolute position using its own origin"),
		Box3DIrisReplication::ToAbsoluteWorldPosition(ClientLocal, ClientOrigin).Equals(Absolute, 0.5));
	TestEqual(TEXT("Cursor resumes at the first skipped due candidate"),
		Box3DIrisReplication::ComputeNextCandidateCursor(0, 200, 300, 128), 128);
	TestEqual(TEXT("Repeated budget exhaustion continues with the next deferred body"),
		Box3DIrisReplication::ComputeNextCandidateCursor(128, 200, 300, 128), 256);
	TestEqual(TEXT("Cursor advances past the bounded scan when nothing was deferred"),
		Box3DIrisReplication::ComputeNextCandidateCursor(240, 60, 300, INDEX_NONE), 0);

	using namespace Box3DAsyncPresentationCollision;
	const FFilter SharedDynamic = GetFilter(EBox3DReplicationPolicy::Shared,
		EBox3DAsyncPresentationMode::AuthoredDynamicCollision);
	const FFilter LocalDynamic = GetFilter(EBox3DReplicationPolicy::LocalOnly,
		EBox3DAsyncPresentationMode::AuthoredDynamicCollision);
	const FFilter StaticWorld = GetFilter(EBox3DReplicationPolicy::LocalOnly,
		EBox3DAsyncPresentationMode::AuthoredStaticCollision);
	TestTrue(TEXT("Shared dynamics collide with static scenery"), CanCollide(SharedDynamic, StaticWorld));
	TestTrue(TEXT("Local dynamics collide with static scenery"), CanCollide(LocalDynamic, StaticWorld));
	TestFalse(TEXT("Shared and local dynamic bodies remain isolated"), CanCollide(SharedDynamic, LocalDynamic));
	TestFalse(TEXT("Shared static actors are not snapshot replicated"), IsSharedDynamic(EBox3DReplicationPolicy::Shared,
		EBox3DAsyncPresentationMode::AuthoredStaticCollision));
	const FFilter SharedStatic = GetFilter(EBox3DReplicationPolicy::Shared,
		EBox3DAsyncPresentationMode::AuthoredStaticCollision);
	TestEqual(TEXT("Shared static actors keep the local static filter"), SharedStatic.CategoryBits, StaticWorld.CategoryBits);
	TestEqual(TEXT("Shared static actors keep the local static mask"), SharedStatic.MaskBits, StaticWorld.MaskBits);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBox3DIrisConnectionArrayIsolationTest,
	"BoxPhysicsExample.Iris.ConnectionArrayIsolation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBox3DIrisConnectionArrayIsolationTest::RunTest(const FString& Parameters)
{
	UBox3DIrisReplicationRoot* RootA = NewObject<UBox3DIrisReplicationRoot>(GetTransientPackage());
	UBox3DIrisReplicationRoot* RootB = NewObject<UBox3DIrisReplicationRoot>(GetTransientPackage());
	if (!TestNotNull(TEXT("Root A"), RootA) || !TestNotNull(TEXT("Root B"), RootB)) return false;
	FBox3DIrisBodyItem& ItemA = RootA->GetBodyArray().Items.AddDefaulted_GetRef();
	ItemA.BodyId = 1;
	ItemA.BodyKey = FGuid(1, 2, 3, 4);
	ItemA.Position = FVector_NetQuantize(100.0, -50.0, 25.0);
	RootA->GetBodyArray().MarkItemDirty(ItemA);
	FBox3DIrisBodyItem& ItemB = RootB->GetBodyArray().Items.AddDefaulted_GetRef();
	ItemB.BodyId = 1;
	ItemB.BodyKey = FGuid(1, 2, 3, 4);
	ItemB.Position = FVector_NetQuantize(100.0, -50.0, 25.0);
	RootB->GetBodyArray().MarkItemDirty(ItemB);
	ItemA.Position = FVector_NetQuantize(200.0, -50.0, 25.0);
	RootA->GetBodyArray().MarkItemDirty(ItemA);
	TestTrue(TEXT("Updating one connection copy leaves the other copy unchanged"),
		FVector(ItemB.Position).Equals(FVector(100.0, -50.0, 25.0)));
	RootA->GetBodyArray().Items.RemoveAt(0);
	RootA->GetBodyArray().MarkArrayDirty();
	TestEqual(TEXT("Removal affects only root A"), RootA->GetBodyArray().Items.Num(), 0);
	TestEqual(TEXT("Root B retains its independent row"), RootB->GetBodyArray().Items.Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBox3DIrisSchedulerAndPresentationInvariantTest,
	"BoxPhysicsExample.Iris.SchedulerAndPresentationInvariants",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBox3DIrisSchedulerAndPresentationInvariantTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Near tier schedules at 20 Hz"), Box3DIrisReplication::ComputeTierInterval(1000.0), 0.05);
	TestEqual(TEXT("Near tier hysteresis retains 20 Hz at threshold plus margin"),
		Box3DIrisReplication::ComputeTierInterval(1550.0, 0.05), 0.05);
	TestEqual(TEXT("Mid tier schedules at 10 Hz"), Box3DIrisReplication::ComputeTierInterval(2500.0), 0.1);
	TestEqual(TEXT("Far tier schedules at 5 Hz"), Box3DIrisReplication::ComputeTierInterval(5000.0), 0.2);
	TestTrue(TEXT("Deadline is not due early"), !Box3DIrisReplication::IsDeadlineDue(1.99, 2.0));
	TestTrue(TEXT("Deadline becomes due at boundary"), Box3DIrisReplication::IsDeadlineDue(2.0, 2.0));
	TestTrue(TEXT("Late scheduling advances deadline past now"), FMath::IsNearlyEqual(
		Box3DIrisReplication::AdvanceDeadline(2.16, 0.05, 2.0), 2.2, 1.e-9));
	TestTrue(TEXT("Sequence wraps monotonically"), Box3DIrisReplication::IsCaptureSequenceNewer(1, MAX_uint32));
	TestFalse(TEXT("Stale sequence is ignored"), Box3DIrisReplication::IsCaptureSequenceNewer(10, 11));
	TestTrue(TEXT("Body recreation resets presentation identity"), Box3DIrisReplication::ShouldResetPresentation(
		2, 1, 0, 1, 1, 0, true));
	TestTrue(TEXT("Discontinuity resets presentation history"), Box3DIrisReplication::ShouldResetPresentation(
		1, 1, 2, 1, 1, 1, true));
	TestEqual(TEXT("Interpolation clamps to oldest sample"),
		Box3DIrisReplication::ComputePlaybackAlpha(-1.0, 0.0, 1.0), 0.0f);
	TestEqual(TEXT("Interpolation clamps to newest sample"),
		Box3DIrisReplication::ComputePlaybackAlpha(2.0, 0.0, 1.0), 1.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBox3DIrisAuthorityOriginAndCapacityPolicyTest,
	"BoxPhysicsExample.Iris.AuthorityOriginAndCapacityPolicy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBox3DIrisAuthorityOriginAndCapacityPolicyTest::RunTest(const FString& Parameters)
{
	const FVector FrozenOrigin(1000000.0, -250000.0, 0.0);
	TestFalse(TEXT("Unchanged authority origin keeps replication active"),
		Box3DIrisReplication::ShouldSuspendForAuthorityOriginShift(FrozenOrigin, FrozenOrigin, false));
	TestTrue(TEXT("Live authority origin shift suspends replication"),
		Box3DIrisReplication::ShouldSuspendForAuthorityOriginShift(FrozenOrigin, FrozenOrigin + FVector(1.0, 0.0, 0.0), false));
	TestTrue(TEXT("Origin suspension stays sticky after coordinates return"),
		Box3DIrisReplication::ShouldSuspendForAuthorityOriginShift(FrozenOrigin, FrozenOrigin, true));
	TestTrue(TEXT("Closer distance wins capacity admission"),
		Box3DIrisReplication::IsAdmissionPreferred(100.0, 25, 101.0, 3));
	TestTrue(TEXT("Body ID breaks equal-distance admission ties deterministically"),
		Box3DIrisReplication::IsAdmissionPreferred(100.0, 3, 100.0, 4));
	TestFalse(TEXT("Farther equal-distance tie cannot replace a lower ID"),
		Box3DIrisReplication::IsAdmissionPreferred(100.0, 5, 100.0, 4));
	const Box3DIrisReplication::FStageBudget Saturated =
		Box3DIrisReplication::ComputeStageBudget(200, 200, 128, 96);
	TestEqual(TEXT("Lifecycle staging has a fixed bound under pressure"), Saturated.Lifecycle, 96);
	TestEqual(TEXT("Routine service is reserved while lifecycle work remains saturated"), Saturated.Routine, 32);
	const Box3DIrisReplication::FStageBudget LightLifecycle =
		Box3DIrisReplication::ComputeStageBudget(12, 200, 128, 96);
	TestEqual(TEXT("Unused lifecycle budget returns to routine service"), LightLifecycle.Routine, 116);
	return true;
}
#endif
