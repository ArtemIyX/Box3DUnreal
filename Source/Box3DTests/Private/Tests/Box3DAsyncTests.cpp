#include "World/Box3DAsyncSubsystemImpl.h"
#include "Misc/AutomationTest.h"
#include "PhysicsEngine/BodySetup.h"
#include "box3d/collision.h"
#include <limits>

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
