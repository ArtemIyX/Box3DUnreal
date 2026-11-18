#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Box3DAsyncSubsystem.generated.h"

/** World-local handle. Zero is invalid; IDs are never reused during subsystem lifetime. */
using FBox3DAsyncBodyId = uint64;
using FBox3DAsyncTraceRequestId = uint64;
using FBox3DAsyncGeometryId = uint64;
using FBox3DAsyncShapeId = uint64;

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

/** Opt-in settings for actorless simple-shape creation. Sensors with nonzero density contribute mass. */
struct BOX3DASYNC_API FBox3DAsyncShapeOptions
{
	bool bSensor = false;
	bool bDetectableBySensors = false;
	bool bContactEvents = false;
	bool bImpactEvents = false;
	uint64 CategoryBits = MAX_uint64;
	uint64 MaskBits = MAX_uint64;
	float Density = 1.0f;
};

enum class EBox3DAsyncEventKind : uint8 { ContactBegin, ContactEnd, Impact, SensorEnter, SensorExit };
enum class EBox3DAsyncEventEndCause : uint8 { NativeEnd, Removed };

struct BOX3DASYNC_API FBox3DAsyncEvent
{
	EBox3DAsyncEventKind Kind = EBox3DAsyncEventKind::ContactBegin;
	EBox3DAsyncEventEndCause EndCause = EBox3DAsyncEventEndCause::NativeEnd;
	FBox3DAsyncBodyId BodyA = 0, BodyB = 0;
	FBox3DAsyncShapeId ShapeA = 0, ShapeB = 0;
	FVector Point = FVector::ZeroVector;
	FVector Normal = FVector::ZeroVector;
	float ApproachSpeed = 0;
	uint64 MaterialA = 0, MaterialB = 0;
	/** Native contact identity, meaningful for contact and impact events within this world. */
	uint32 ContactIndex = 0, ContactWorld = 0, ContactGeneration = 0;
};

/** Raw native transitions, ordered by kind then endpoint/contact identity. Contact endpoints
 * are ordered by shape ID, with impact normal and materials oriented from A to B. Sensor
 * events retain sensor/visitor direction. A gap invalidates relationship state permanently. */
struct BOX3DASYNC_API FBox3DAsyncEventBatch
{
	FGuid World;
	uint64 Step = 0;
	uint64 Sequence = 0;
	TArray<FBox3DAsyncEvent> Events;
};

struct BOX3DASYNC_API FBox3DAsyncCreationResult
{
	EBox3DAsyncCreationKind Kind = EBox3DAsyncCreationKind::Body;
	uint64 ResourceId = 0;
	bool bSucceeded = false;
	int32 DegenerateTriangleCount = 0;
	int32 DroppedTriangleCount = 0;
	TArray<FBox3DAsyncShapeId> ShapeIds;
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

/** A world-scoped polling consumer. Handles from another subsystem are rejected. */
struct BOX3DASYNC_API FBox3DAsyncQueryConsumer
{
	FGuid World;
	uint64 Id = 0;
};

struct BOX3DASYNC_API FBox3DAsyncQueryHandle
{
	FGuid World;
	uint64 Id = 0;
};

enum class EBox3DAsyncLineMode : uint8 { Any, Closest, Multi };
enum class EBox3DAsyncQueryStatus : uint8 { Hit, Miss, Truncated, Cancelled };

/** Copied at admission. Category/mask use Box3D's bilateral filtering. */
struct BOX3DASYNC_API FBox3DAsyncLineQuery
{
	FVector Start = FVector::ZeroVector;
	FVector End = FVector::ZeroVector;
	EBox3DAsyncLineMode Mode = EBox3DAsyncLineMode::Closest;
	uint64 CategoryBits = MAX_uint64;
	uint64 MaskBits = MAX_uint64;
	bool bStatic = true;
	bool bDynamic = true;
	bool bSensors = false;
	bool bSolids = true;
	int32 MaxHits = 1;
	uint64 UserTag = 0;
	TArray<FBox3DAsyncBodyId> IgnoredBodies;
};

struct BOX3DASYNC_API FBox3DAsyncLineHit
{
	FBox3DAsyncBodyId BodyId = 0;
	float Fraction = 1.0f;
	FVector Point = FVector::ZeroVector;
	FVector Normal = FVector::ZeroVector;
	uint64 MaterialId = 0;
	int32 TriangleIndex = -1;
	int32 ChildIndex = -1;
};

struct BOX3DASYNC_API FBox3DAsyncQueryResult
{
	FBox3DAsyncQueryHandle Request;
	uint64 UserTag = 0;
	uint64 SampledStep = 0;
	EBox3DAsyncQueryStatus Status = EBox3DAsyncQueryStatus::Miss;
	TArray<FBox3DAsyncLineHit> Hits;
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
	/** Actorless box with copied filter and event flags. */
	bool TryCreateStaticBox(const FTransform& Transform, const FVector& HalfExtents,
	                        const FBox3DAsyncShapeOptions& Options, FBox3DAsyncBodyId& OutBodyId);
	/** Queue a dynamic sphere. Radius is centimeters and velocity is centimeters per second. */
	bool TryCreateDynamicSphere(const FTransform& Transform, float Radius, const FVector& InitialVelocity, FBox3DAsyncBodyId& OutBodyId);
	/** Actorless sphere with copied filter and event flags. */
	bool TryCreateDynamicSphere(const FTransform& Transform, float Radius, const FVector& InitialVelocity,
	                            const FBox3DAsyncShapeOptions& Options, FBox3DAsyncBodyId& OutBodyId);
	/** Drain the single raw stream. Removed relationships end at the first subsequent real step.
	 * Missing identity or a capacity overflow sets a sticky gap; no subsequent events are published.
	 * Once bOutGap is true the stream is invalid until world teardown. */
	void DrainRawEvents(TArray<FBox3DAsyncEventBatch>& OutBatches, bool& bOutGap);
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
	/** Queue a dynamic body using the mesh's authored simple collision. Round shapes require uniform positive scale. */
	bool TryCreateDynamicMeshCollision(UStaticMesh* Mesh, const FTransform& Transform, const FVector& Scale,
		const FVector& InitialVelocity, FBox3DAsyncBodyId& OutBodyId);
	/** Queue a static instance of registered reusable geometry with positive scale components (minimum 1e-4). */
	bool TryCreateStaticMeshInstance(FBox3DAsyncGeometryId GeometryId, const FTransform& Transform,
		const FVector& Scale, FBox3DAsyncBodyId& OutBodyId);
	/** Request release. Existing instances retain the cooked mesh until they are removed. */
	bool TryReleaseMeshGeometry(FBox3DAsyncGeometryId GeometryId);
	/** Queue removal of a previously accepted body ID. */
	bool TryRemoveBody(FBox3DAsyncBodyId BodyId);

	/** Register a polling owner for bounded, independently routed line queries. */
	bool TryRegisterQueryConsumer(FBox3DAsyncQueryConsumer& OutConsumer);
	/** Removes the consumer and its unread requests. An executing query finishes before storage is released. */
	void UnregisterQueryConsumer(FBox3DAsyncQueryConsumer Consumer);
	bool TrySubmitQuery(FBox3DAsyncQueryConsumer Consumer, const FBox3DAsyncLineQuery& Query,
	                    FBox3DAsyncQueryHandle& OutRequest);
	bool TryCancelQuery(FBox3DAsyncQueryConsumer Consumer, FBox3DAsyncQueryHandle Request);
	bool TryTakeQueryResult(FBox3DAsyncQueryConsumer Consumer, FBox3DAsyncQueryHandle Request,
	                        FBox3DAsyncQueryResult& OutResult);

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
	bool TryCreateAuthoredMeshCollision(UStaticMesh* Mesh, const FTransform& Transform, const FVector& Scale,
		const FVector& InitialVelocity, bool bDynamic, FBox3DAsyncBodyId& OutBodyId);
	struct FImpl;
	TUniquePtr<FImpl> Impl;
};
