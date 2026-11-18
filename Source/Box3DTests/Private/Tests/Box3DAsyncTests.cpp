#include "World/Box3DAsyncSubsystemImpl.h"
#include "World/Box3DAsyncStats.h"
#include "Misc/AutomationTest.h"
#include "PhysicsEngine/BodySetup.h"
#include "Box3DTestHelpers.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Misc/ScopeExit.h"
#include "box3d/collision.h"
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBox3DAsyncStatsAggregationTest,
	"Box3D.Async.StatsAggregation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBox3DAsyncStatsAggregationTest::RunTest(const FString& Parameters)
{
	const Box3DAsyncStats::FAggregate Baseline = Box3DAsyncStats::GetAggregate();
	auto First = MakeUnique<Box3DTest::FTestWorld>(TEXT("AsyncStatsFirst"));
	auto Second = MakeUnique<Box3DTest::FTestWorld>(TEXT("AsyncStatsSecond"));
	UBox3DAsyncSubsystem* A = First->World->GetSubsystem<UBox3DAsyncSubsystem>();
	UBox3DAsyncSubsystem* B = Second->World->GetSubsystem<UBox3DAsyncSubsystem>();
	if (!TestNotNull(TEXT("First async world"), A) || !TestNotNull(TEXT("Second async world"), B)) return false;
	FBox3DAsyncBodyId AId = 0, BId = 0;
	if (!TestTrue(TEXT("First box queued"), A->TryCreateStaticBox(FTransform::Identity, FVector(50), AId))
		|| !TestTrue(TEXT("Second box queued"), B->TryCreateStaticBox(FTransform::Identity, FVector(50), BId))) return false;
	const double Deadline = FPlatformTime::Seconds() + 5.0;
	bool bPublished = false;
	while (FPlatformTime::Seconds() < Deadline)
	{
		const Box3DAsyncStats::FAggregate Stats = Box3DAsyncStats::GetAggregate();
		if (Stats.WorldCount == Baseline.WorldCount + 2 && Stats.Bodies >= Baseline.Bodies + 2
			&& Stats.Shapes >= Baseline.Shapes + 2 && A->GetCompletedStepCount() > 0 && B->GetCompletedStepCount() > 0)
		{
			bPublished = true;
			break;
		}
		FPlatformProcess::Sleep(0.01f);
	}
	TestTrue(TEXT("Both worker snapshots contribute"), bPublished);
	First.Reset();
	TestEqual(TEXT("First world contribution removed"), Box3DAsyncStats::GetAggregate().WorldCount, Baseline.WorldCount + 1);
	Second.Reset();
	TestEqual(TEXT("Both world contributions removed"), Box3DAsyncStats::GetAggregate().WorldCount, Baseline.WorldCount);
	return true;
}
#endif

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBox3DAsyncDynamicAuthoredBoxTest,
	"Box3D.Async.DynamicAuthoredBox",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBox3DAsyncDynamicAuthoredBoxTest::RunTest(const FString& Parameters)
{
	UBox3DSettings* Settings = GetMutableDefault<UBox3DSettings>();
	const FVector PreviousGravity = Settings->Gravity;
	Settings->Gravity = FVector(0, 0, -980);
	ON_SCOPE_EXIT { Settings->Gravity = PreviousGravity; };
	Box3DTest::FTestWorld TestWorld(TEXT("Box3DAsyncDynamicBoxTest"));
	UBox3DAsyncSubsystem* Physics = TestWorld.World->GetSubsystem<UBox3DAsyncSubsystem>();
	if (!TestNotNull(TEXT("Async subsystem exists"), Physics)) return false;

	UStaticMesh* Mesh = NewObject<UStaticMesh>(GetTransientPackage());
	UBodySetup* Setup = NewObject<UBodySetup>(Mesh);
	Mesh->SetBodySetup(Setup);
	FKBoxElem& Box = Setup->AggGeom.BoxElems.AddDefaulted_GetRef();
	Box.X = 200.0f;
	Box.Y = 100.0f;
	Box.Z = 50.0f;

	FBox3DAsyncBodyId FloorId = 0;
	FBox3DAsyncBodyId BoxId = 0;
	if (!TestTrue(TEXT("Floor accepted"), Physics->TryCreateStaticBox(
		FTransform(FVector(0, 0, -50)), FVector(500, 500, 50), FloorId))) return false;
	if (!TestTrue(TEXT("Authored dynamic box accepted"), Physics->TryCreateDynamicMeshCollision(
		Mesh, FTransform(FVector(0, 0, 100)), FVector::OneVector, FVector::ZeroVector, BoxId))) return false;

	bool bCreated = false;
	double Deadline = FPlatformTime::Seconds() + 5.0;
	while (FPlatformTime::Seconds() < Deadline && !bCreated)
	{
		TArray<FBox3DAsyncCreationResult> Results;
		Physics->DrainCreationResults(Results);
		for (const FBox3DAsyncCreationResult& Result : Results)
		{
			if (Result.Kind == EBox3DAsyncCreationKind::Body && Result.ResourceId == BoxId)
			{
				if (!TestTrue(TEXT("Dynamic box created"), Result.bSucceeded)) return false;
				bCreated = true;
			}
		}
		FPlatformProcess::Sleep(0.01f);
	}
	if (!TestTrue(TEXT("Dynamic box creation completed"), bCreated)) return false;

	// A 50 cm tall box rests around z=25; a 100 cm radius sphere proxy would not.
	bool bRestedAtBoxHeight = false;
	double InRangeSince = 0.0;
	Deadline = FPlatformTime::Seconds() + 5.0;
	while (FPlatformTime::Seconds() < Deadline && !bRestedAtBoxHeight)
	{
		TArray<FBox3DAsyncPose> Poses;
		Physics->SamplePoses(Poses);
		for (const FBox3DAsyncPose& Pose : Poses)
		{
			if (Pose.BodyId == BoxId)
			{
				const double Height = Pose.Transform.GetLocation().Z;
				if (Height > 15.0 && Height < 35.0)
				{
					if (InRangeSince == 0.0) InRangeSince = FPlatformTime::Seconds();
					bRestedAtBoxHeight = FPlatformTime::Seconds() - InRangeSince >= 0.5;
				}
				else
				{
					InRangeSince = 0.0;
				}
			}
		}
		FPlatformProcess::Sleep(0.01f);
	}
	TestTrue(TEXT("Dynamic authored box falls and reaches the floor at box height"), bRestedAtBoxHeight);
	return true;
}
#endif

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBox3DAsyncMeshInputValidationTest,
	"Box3D.Async.MeshInputValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBox3DAsyncMeshInputValidationTest::RunTest(const FString& Parameters)
{
	TArray<FVector> Vertices = {
		FVector(0.0, 0.0, 0.0), FVector(100.0, 0.0, 0.0), FVector(100.0, 100.0, 0.0), FVector(0.0, 100.0, 0.0)
	};
	TArray<int32> Indices = {0, 1, 2, 0, 2, 3};
	TArray<b3Vec3> NativeVertices;
	TArray<int32> NativeIndices;
	uint64 ByteCount = 0;
	FVector BoundsMin;
	FVector BoundsMax;
	TestTrue(TEXT("Box3D winding quad is accepted"), Box3DAsyncPrivate::ValidateAndCopyGeometryForTest(Vertices, Indices,
		EBox3DAsyncMeshWinding::Box3D, NativeVertices, NativeIndices, ByteCount, BoundsMin, BoundsMax));
	TestEqual(TEXT("Geometry payload byte count is bounded and exact"), ByteCount,
		uint64(Vertices.Num() * sizeof(b3Vec3) + Indices.Num() * sizeof(int32)));

	TArray<int32> UnrealIndices = {0, 2, 1, 0, 3, 2};
	TestTrue(TEXT("Unreal winding is converted to Box3D winding"), Box3DAsyncPrivate::ValidateAndCopyGeometryForTest(Vertices, UnrealIndices,
		EBox3DAsyncMeshWinding::Unreal, NativeVertices, NativeIndices, ByteCount, BoundsMin, BoundsMax));
	TestEqual(TEXT("Winding conversion swaps the second and third indices"), NativeIndices[1], 1);

	TArray<int32> OutOfRangeIndices = {0, 1, 4};
	TestFalse(TEXT("Out-of-range indices are rejected"), Box3DAsyncPrivate::ValidateAndCopyGeometryForTest(Vertices, OutOfRangeIndices,
		EBox3DAsyncMeshWinding::Box3D, NativeVertices, NativeIndices, ByteCount, BoundsMin, BoundsMax));
	TArray<int32> InconsistentWindingIndices = {0, 1, 2, 0, 3, 2};
	TestFalse(TEXT("Shared edges with inconsistent winding are rejected"), Box3DAsyncPrivate::ValidateAndCopyGeometryForTest(Vertices,
		InconsistentWindingIndices, EBox3DAsyncMeshWinding::Box3D, NativeVertices, NativeIndices, ByteCount,
		BoundsMin, BoundsMax));
	TArray<int32> DegenerateIndices = {0, 1, 1};
	TestFalse(TEXT("Repeated-index degenerate triangles are rejected"), Box3DAsyncPrivate::ValidateAndCopyGeometryForTest(Vertices,
		DegenerateIndices, EBox3DAsyncMeshWinding::Box3D, NativeVertices, NativeIndices, ByteCount, BoundsMin, BoundsMax));
	Vertices[2].X = std::numeric_limits<double>::quiet_NaN();
	TestFalse(TEXT("Non-finite vertices are rejected"), Box3DAsyncPrivate::ValidateAndCopyGeometryForTest(Vertices, Indices,
		EBox3DAsyncMeshWinding::Box3D, NativeVertices, NativeIndices, ByteCount, BoundsMin, BoundsMax));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBox3DAsyncAuthoredCollisionCopyTest,
	"Box3D.Async.AuthoredCollisionCopyAndScale",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBox3DAsyncAuthoredCollisionCopyTest::RunTest(const FString& Parameters)
{
	UBodySetup* BodySetup = NewObject<UBodySetup>(GetTransientPackage());
	FKSphereElem& Sphere = BodySetup->AggGeom.SphereElems.AddDefaulted_GetRef();
	Sphere.Center = FVector(10.0f, 0.0f, 0.0f);
	Sphere.Radius = 5.0f;
	FKSphylElem& Capsule = BodySetup->AggGeom.SphylElems.AddDefaulted_GetRef();
	Capsule.Center = FVector(0.0f, 0.0f, 0.0f);
	Capsule.Radius = 2.0f;
	Capsule.Length = 10.0f;
	FKBoxElem& Box = BodySetup->AggGeom.BoxElems.AddDefaulted_GetRef();
	Box.Center = FVector(10.0f, 0.0f, 0.0f);
	Box.X = 2.0f;
	Box.Y = 4.0f;
	Box.Z = 6.0f;
	Box.Rotation = FRotator(0.0f, 45.0f, 0.0f);

	TArray<Box3DAsyncPrivate::FCopiedStaticShape> Shapes;
	FVector BoundsMin;
	FVector BoundsMax;
	uint64 PayloadBytes = 0;
	TestTrue(TEXT("Sphere, capsule, and box shapes copy at uniform scale"),
		Box3DAsyncPrivate::CopyAuthoredCollisionShapes(*BodySetup, FVector(2.0), Shapes,
			BoundsMin, BoundsMax, PayloadBytes));
	TestEqual(TEXT("Authored shapes remain attached to one body payload"), Shapes.Num(), 3);
	if (Shapes.Num() == 3)
	{
		TestTrue(TEXT("Sphere center and radius are copied in scaled meters"),
			FMath::IsNearlyEqual(Shapes[0].Sphere.center.x, 0.2f, 1.e-6f)
			&& FMath::IsNearlyEqual(Shapes[0].Sphere.radius, 0.1f, 1.e-6f));
		TestTrue(TEXT("Capsule endpoints and radius are copied in scaled meters"),
			FMath::IsNearlyEqual(Shapes[1].Capsule.center1.z, -0.1f)
			&& FMath::IsNearlyEqual(Shapes[1].Capsule.center2.z, 0.1f)
			&& FMath::IsNearlyEqual(Shapes[1].Capsule.radius, 0.04f));
		TestEqual(TEXT("Rotatable box is copied as eight transformed hull corners"), Shapes[2].HullPoints.Num(), 8);
		TestTrue(TEXT("Box rotation is baked into its copied hull points"),
			Shapes[2].HullPoints[0].x > 0.2f && Shapes[2].HullPoints[0].y < -0.04f);
	}
	TestTrue(TEXT("Copied bounds include scaled authored shapes"),
		FMath::IsNearlyEqual(BoundsMin.X, -0.04, 1.e-6) && FMath::IsNearlyEqual(BoundsMax.X, 0.3, 1.e-6)
		&& FMath::IsNearlyEqual(BoundsMin.Z, -0.14, 1.e-6) && FMath::IsNearlyEqual(BoundsMax.Z, 0.14, 1.e-6));
	TestFalse(TEXT("Round primitives reject nonuniform scale"),
		Box3DAsyncPrivate::IsSupportedAuthoredCollisionScale(true, FVector(2.0, 3.0, 4.0)));
	TestFalse(TEXT("Authored sphere and capsule data is rejected at nonuniform scale"),
		Box3DAsyncPrivate::CopyAuthoredCollisionShapes(*BodySetup, FVector(2.0, 3.0, 4.0), Shapes,
			BoundsMin, BoundsMax, PayloadBytes));
	TestTrue(TEXT("Round primitives accept uniform scale"),
		Box3DAsyncPrivate::IsSupportedAuthoredCollisionScale(true, FVector(2.0)));

	UBodySetup* BoxOnlySetup = NewObject<UBodySetup>(GetTransientPackage());
	FKBoxElem& BoxOnly = BoxOnlySetup->AggGeom.BoxElems.AddDefaulted_GetRef();
	BoxOnly.Center = FVector(10.0f, 0.0f, 0.0f);
	BoxOnly.X = 2.0f;
	BoxOnly.Y = 4.0f;
	BoxOnly.Z = 6.0f;
	TestTrue(TEXT("Box hull supports nonuniform positive scale"),
		Box3DAsyncPrivate::CopyAuthoredCollisionShapes(*BoxOnlySetup, FVector(2.0, 3.0, 4.0), Shapes,
			BoundsMin, BoundsMax, PayloadBytes));
	TestTrue(TEXT("Nonuniform scale is applied to copied box bounds"),
		FMath::IsNearlyEqual(BoundsMin.X, 0.18) && FMath::IsNearlyEqual(BoundsMax.X, 0.22)
		&& FMath::IsNearlyEqual(BoundsMin.Y, -0.06) && FMath::IsNearlyEqual(BoundsMax.Y, 0.06)
		&& FMath::IsNearlyEqual(BoundsMin.Z, -0.12) && FMath::IsNearlyEqual(BoundsMax.Z, 0.12));
	return true;
}
#endif


#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBox3DAsyncWeldedTriangleLossTest,
	"Box3D.Async.WeldedTriangleLossIsRejected",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBox3DAsyncWeldedTriangleLossTest::RunTest(const FString& Parameters)
{
	TArray<b3Vec3> Vertices = {
		b3Vec3{0.0f, 0.0f, 0.0f}, b3Vec3{0.0005f, 0.0f, 0.0f}, b3Vec3{0.0f, 0.01f, 0.0f},
		b3Vec3{1.0f, 0.0f, 0.0f}, b3Vec3{1.0f, 1.0f, 0.0f}
	};
	TArray<int32> Indices = {0, 1, 2, 0, 3, 4};
	b3MeshDef MeshDef = {};
	MeshDef.vertices = Vertices.GetData();
	MeshDef.indices = Indices.GetData();
	MeshDef.vertexCount = Vertices.Num();
	MeshDef.triangleCount = Indices.Num() / 3;
	MeshDef.weldVertices = true;
	MeshDef.weldTolerance = 0.001f;
	MeshDef.identifyEdges = true;
	TArray<int> DegenerateTriangles;
	DegenerateTriangles.SetNumZeroed(MeshDef.triangleCount);
	const b3MeshData* CookedMesh = b3CreateMesh(&MeshDef, DegenerateTriangles.GetData(), DegenerateTriangles.Num());
	TestNotNull(TEXT("The unaffected triangle still produces cooked mesh data"), CookedMesh);
	if (CookedMesh == nullptr)
	{
		return false;
	}
	TestEqual(TEXT("Native welding silently discards the collapsed triangle"), CookedMesh->triangleCount, 1);
	TestEqual(TEXT("Duplicate welded indices do not increment native degenerateCount"), CookedMesh->degenerateCount, 0);
	TestFalse(TEXT("A partial cook is rejected by the complete-triangle check"),
		Box3DAsyncPrivate::IsCompleteMeshCookForTest(CookedMesh, MeshDef.triangleCount));
	b3DestroyMesh(const_cast<b3MeshData*>(CookedMesh));
	return true;
}
#endif
