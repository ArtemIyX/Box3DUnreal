#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Box3DAsyncSubsystem.generated.h"

/** World-local handle. Zero is invalid; IDs are never reused during subsystem lifetime. */
using FBox3DAsyncBodyId = uint64;
using FBox3DAsyncTraceRequestId = uint64;
using FBox3DAsyncGeometryId = uint64;

class UStaticMesh;
class UBox3DCollisionData;

enum class EBox3DAsyncMeshWinding : uint8
{
	Box3D,
	Unreal
};

enum class EBox3DAsyncCreationKind : uint8
{
	Body,
	MeshGeometry,
	MeshRelease
};

struct BOX3DASYNC_API FBox3DAsyncCreationResult
{
	EBox3DAsyncCreationKind Kind = EBox3DAsyncCreationKind::Body;
	uint64 ResourceId = 0;
	bool bSucceeded = false;
	int32 DegenerateTriangleCount = 0;
	int32 DroppedTriangleCount = 0;
};

struct BOX3DASYNC_API FBox3DAsyncPose
{
	FBox3DAsyncBodyId BodyId = 0;
	FTransform Transform = FTransform::Identity;
};

struct BOX3DASYNC_API FBox3DAsyncTraceResult
{
	FBox3DAsyncTraceRequestId RequestId = 0;
	uint64 SampledStep = 0;
	bool bHit = false;
	FBox3DAsyncBodyId BodyId = 0;
	float Fraction = 1.0f;
	FVector Point = FVector::ZeroVector;
	FVector Normal = FVector::ZeroVector;
};

struct BOX3DASYNC_API FBox3DAsyncDiagnostics
{
	uint64 CompletedSteps = 0;
	uint64 NativeCreationFailures = 0;
	uint64 DeadlineMisses = 0;
	uint64 ClockRebases = 0;
	double LatestScheduledTimeSeconds = 0.0;
	double LatestCompletionTimeSeconds = 0.0;
	int32 QueuedCommands = 0;
	int32 OutstandingTraces = 0;
	int32 OutstandingCreationResults = 0;
	uint64 ReservedMeshGeometryBytes = 0;
	int32 RegisteredMeshGeometries = 0;
};

/**
 * Runs a private Box3D world on one dedicated worker. Initialize, Deinitialize,
 * and all public methods must be called on the game thread. Inputs and returned
 * transforms use Unreal centimeters. The caller owns presentation and must apply
 * sampled transforms to actors/components on the game thread. Deinitialize joins
 * the worker before destroying its native world.
 */
UCLASS()
class BOX3DASYNC_API UBox3DAsyncSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	static constexpr int32 CommandCapacity = 256;
	static constexpr int32 OutstandingTraceCapacity = 64;
	static constexpr int32 OutstandingCreationResultCapacity = 256;
	/// Per-world admission budget for accepted payloads, estimated cook scratch, and retained native meshes. It excludes caller-owned inputs and temporary validation allocations.
	static constexpr uint64 MaxMeshGeometryBytes = 64ull * 1024ull * 1024ull;
	virtual ~UBox3DAsyncSubsystem() override;

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

	/** Queue a static box. Half extents are centimeters; transform scale must be one. */
	bool TryCreateStaticBox(const FTransform& Transform, const FVector& HalfExtents, FBox3DAsyncBodyId& OutBodyId);
	/** Queue a dynamic sphere. Radius is centimeters and velocity is centimeters per second. */
	bool TryCreateDynamicSphere(const FTransform& Transform, float Radius, const FVector& InitialVelocity, FBox3DAsyncBodyId& OutBodyId);
	/** Queue copied local-space mesh geometry in centimeters; accepted cooking completes through DrainCreationResults. */
	bool TryRegisterStaticMeshGeometry(const TArray<FVector>& Vertices, const TArray<int32>& Indices,
		EBox3DAsyncMeshWinding Winding, FBox3DAsyncGeometryId& OutGeometryId);
	/** Copy CPU-accessible LOD0 render geometry and convert Unreal triangle winding. */
	bool TryRegisterStaticMeshLOD0(UStaticMesh* Mesh, FBox3DAsyncGeometryId& OutGeometryId);
	/** Copy a mesh shape from existing baked collision data (already Box3D winding). */
	bool TryRegisterBakedMesh(const UBox3DCollisionData* Data, int32 BodyIndex, int32 ShapeIndex,
		FBox3DAsyncGeometryId& OutGeometryId);
	/** Copy authored UStaticMesh simple collision and queue one static body with multiple shapes. Boxes and convex hulls support nonuniform positive scale; meshes with spheres or capsules require uniform positive scale. */
	bool TryCreateStaticMeshCollision(UStaticMesh* Mesh, const FTransform& Transform, const FVector& Scale,
		FBox3DAsyncBodyId& OutBodyId);
	/** Queue a static instance of registered reusable geometry with positive scale components (minimum 1e-4). */
	bool TryCreateStaticMeshInstance(FBox3DAsyncGeometryId GeometryId, const FTransform& Transform,
		const FVector& Scale, FBox3DAsyncBodyId& OutBodyId);
	/** Request release. Existing instances retain the cooked mesh until they are removed. */
	bool TryReleaseMeshGeometry(FBox3DAsyncGeometryId GeometryId);
	/** Queue removal of a previously accepted body ID. */
	bool TryRemoveBody(FBox3DAsyncBodyId BodyId);

	/** Queue a closest static-body trace. Start and End are centimeters. */
	bool TrySubmitLineTrace(const FVector& Start, const FVector& End, FBox3DAsyncTraceRequestId& OutRequestId);
	/** Drain ready results; accepted requests remain capacity-accounted until drained. */
	void DrainTraceResults(TArray<FBox3DAsyncTraceResult>& OutResults);
	/** Drain asynchronous native body creation, geometry cooking, and release results. */
	void DrainCreationResults(TArray<FBox3DAsyncCreationResult>& OutResults);

	/** Sample interpolated dynamic poses at monotonic time minus two fixed steps. */
	void SamplePoses(TArray<FBox3DAsyncPose>& OutPoses, double* OutSampleTimeSeconds = nullptr) const;
	uint64 GetCompletedStepCount() const;
	FBox3DAsyncDiagnostics GetDiagnostics() const;

private:
	struct FImpl;
	TUniquePtr<FImpl> Impl;
};
