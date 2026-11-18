#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Data/Structs/Box3DAsyncBodyHandle.h"
#include "Data/Structs/Box3DAsyncBodyCommand.h"
#include "Data/Structs/Box3DAsyncBodyState.h"
#include "Data/Structs/Box3DAsyncFieldDesc.h"
#include "Data/Structs/Box3DAsyncFieldHandle.h"
#include "Data/Structs/Box3DAsyncJointDesc.h"
#include "Data/Structs/Box3DAsyncJointHandle.h"
#include "Data/Structs/Box3DAsyncJointState.h"
#include "Data/Structs/Box3DAsyncJointCommand.h"
#include "Data/Structs/Box3DAsyncJointBreakEvent.h"
#include "Data/Enums/Box3DAsyncJointStatus.h"
#include "Data/Enums/Box3DAsyncBodyMotion.h"
#include "Data/Enums/Box3DAsyncBodyShapeKind.h"
#include "Box3DAsyncSubsystem.generated.h"

/** World-local handle. Zero is invalid; IDs are never reused during subsystem lifetime. */
using FBox3DAsyncBodyId = uint64;
using FBox3DAsyncTraceRequestId = uint64;
using FBox3DAsyncGeometryId = uint64;
using FBox3DAsyncShapeId = uint64;

/** Generation-safe public shape identity. */
struct BOX3DASYNC_API FBox3DAsyncShapeHandle
{
	FGuid World;
	FBox3DAsyncShapeId Id = 0;
};

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
	/** Equal negative group values suppress collision within a group. */
	int32 GroupIndex = 0;
	float Density = 1.0f;
	uint64 MaterialId = 0;
};


/** Local geometry in centimeters. LocalTransform must have unit scale and a normalized rotation.
 * Capsule HalfHeight includes the hemispheres; convex points are local to LocalTransform. */
struct BOX3DASYNC_API FBox3DAsyncBodyShapeDesc
{
	EBox3DAsyncBodyShapeKind Kind = EBox3DAsyncBodyShapeKind::Box;
	FTransform LocalTransform = FTransform::Identity;
	FVector HalfExtents = FVector(50.0);
	float Radius = 50.0f;
	float HalfHeight = 50.0f;
	TArray<FVector> ConvexPoints;
	FBox3DAsyncShapeOptions Options;
	float Friction = 0.6f;
	float Restitution = 0.0f;
	uint64 SubshapeKey = 0;
};

/** Copied actorless body creation. The body transform must have unit scale. */
struct BOX3DASYNC_API FBox3DAsyncBodyDesc
{
	EBox3DAsyncBodyMotion Motion = EBox3DAsyncBodyMotion::Static;
	FTransform Transform = FTransform::Identity;
	FVector InitialVelocity = FVector::ZeroVector;
	FVector InitialAngularVelocity = FVector::ZeroVector;
	float GravityScale = 1.0f;
	float LinearDamping = 0.0f;
	float AngularDamping = 0.0f;
	/** Positive mass overrides shape-derived mass while preserving inertia proportions. */
	float MassOverride = 0.0f;
	bool bEnableSleep = true;
	bool bStartAwake = true;
	bool bStartEnabled = true;
	bool bBullet = false;
	TArray<FBox3DAsyncBodyShapeDesc> Shapes;
};

struct BOX3DASYNC_API FBox3DAsyncCreationConsumer
{
	FGuid World;
	uint64 Id = 0;
};

enum class EBox3DAsyncEventKind : uint8 { ContactBegin, ContactEnd, Impact, SensorEnter, SensorExit };
enum class EBox3DAsyncEventEndCause : uint8 { NativeEnd, Removed };

/** Stamp of a completed outer physics step; presentation can sample a different time. */
struct BOX3DASYNC_API FBox3DAsyncSampleStamp
{
	uint64 Step = 0;
	double ScheduledMonotonicSeconds = 0.0;
	uint64 ClockEpoch = 0;
};

struct BOX3DASYNC_API FBox3DAsyncEvent
{
	EBox3DAsyncEventKind Kind = EBox3DAsyncEventKind::ContactBegin;
	EBox3DAsyncEventEndCause EndCause = EBox3DAsyncEventEndCause::NativeEnd;
	FBox3DAsyncBodyId BodyA = 0, BodyB = 0;
	FBox3DAsyncShapeId ShapeA = 0, ShapeB = 0;
	FBox3DAsyncBodyHandle BodyHandleA, BodyHandleB;
	FBox3DAsyncShapeHandle ShapeHandleA, ShapeHandleB;
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
	FBox3DAsyncSampleStamp Sample;
	TArray<FBox3DAsyncEvent> Events;
};

/** World-scoped event subscription; IDs are never reused within a world. */
struct BOX3DASYNC_API FBox3DAsyncEventSubscription
{
	FGuid World;
	uint64 Id = 0;
};

/** Immutable subscription filter. Zero endpoint IDs match either endpoint. */
struct BOX3DASYNC_API FBox3DAsyncEventFilter
{
	FBox3DAsyncBodyId Body = 0;
	FBox3DAsyncShapeId Shape = 0;
	/** Prefer qualified filters for cross-world safety; do not combine with the corresponding bare ID. */
	FBox3DAsyncBodyHandle BodyHandle;
	FBox3DAsyncShapeHandle ShapeHandle;
	bool bContacts = true;
	bool bSensors = true;
	bool bImpacts = true;
	/** Aggregate contacts and sensors by body pair; impacts remain per shape. */
	bool bBodyPairs = false;
	float MinimumImpactSpeed = 0.0f;
};

enum class EBox3DAsyncEventPollStatus : uint8 { Pending, Baseline, Events, NeedsResync, Unavailable, Closed };

/** A baseline replaces relationship state at Sequence; deltas continue after it.
 * A gap can lose impacts and transients, even after a new baseline is installed. */
struct BOX3DASYNC_API FBox3DAsyncEventDelivery
{
	EBox3DAsyncEventPollStatus Status = EBox3DAsyncEventPollStatus::Pending;
	uint64 FirstMissedSequence = 0;
	uint64 LastMissedSequence = 0;
	FBox3DAsyncEventBatch Batch;
};

struct BOX3DASYNC_API FBox3DAsyncCreationResult
{
	EBox3DAsyncCreationKind Kind = EBox3DAsyncCreationKind::Body;
	uint64 ResourceId = 0;
	bool bSucceeded = false;
	int32 DegenerateTriangleCount = 0;
	int32 DroppedTriangleCount = 0;
	TArray<FBox3DAsyncShapeId> ShapeIds;
	FBox3DAsyncBodyHandle BodyHandle;
	TArray<FBox3DAsyncShapeHandle> ShapeHandles;
	/** Parallel to ShapeHandles, preserving each descriptor's opaque authored key. */
	TArray<uint64> SubshapeKeys;
};

struct BOX3DASYNC_API FBox3DAsyncPose
{
	FBox3DAsyncBodyId BodyId = 0;
	FTransform Transform = FTransform::Identity;
	bool bAwake = true;
};

/** Latest completed worker publication for transport and diagnostics consumers. */
struct BOX3DASYNC_API FBox3DAsyncPoseFrame
{
	FGuid World;
	uint64 CaptureSerial = 0;
	double CaptureMonotonicSeconds = 0.0;
	FBox3DAsyncSampleStamp SimulationSample;
	TArray<FBox3DAsyncPose> Poses;
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
enum class EBox3DAsyncQueryStatus : uint8 { Hit, Miss, Truncated, Cancelled, Failed, WorldEnded };
/** Admission failures do not allocate a request handle. */
enum class EBox3DAsyncQueryAdmission : uint8 { Accepted, InvalidInput, Unsupported, InvalidConsumer, WrongWorld, Capacity, Unavailable };
/** A Failed completion makes no hit/miss claim. */
enum class EBox3DAsyncQueryFailure : uint8 { None, IdentityUnavailable, WorkLimit, WorkerUnavailable };
enum class EBox3DAsyncQueryKind : uint8 { Line, Sweep, Overlap, AabbCandidates };
enum class EBox3DAsyncQueryShape : uint8 { Sphere, Capsule, Box, Convex };

/** Copied at admission. Category/mask use Box3D's bilateral filtering. World
 * coordinates and rotated local offsets must be within +/- 1e9 cm per axis.
 * Casts use native initial-overlap behavior; issue an overlap for start-state tests. */
struct BOX3DASYNC_API FBox3DAsyncLineQuery
{
	/** Start/End are world centimeters. End is ignored for an overlap; for AABB they are min/max. */
	FVector Start = FVector::ZeroVector;
	FVector End = FVector::ZeroVector;
	EBox3DAsyncQueryKind Kind = EBox3DAsyncQueryKind::Line;
	EBox3DAsyncQueryShape Shape = EBox3DAsyncQueryShape::Sphere;
	/** Fixed geometry orientation. Capsules use local Z; convex points are local centimeters. */
	FQuat Rotation = FQuat::Identity;
	float Radius = 0.0f;
	float HalfHeight = 0.0f;
	FVector HalfExtents = FVector::ZeroVector;
	TArray<FVector> ConvexPoints;
	EBox3DAsyncLineMode Mode = EBox3DAsyncLineMode::Closest;
	uint64 CategoryBits = MAX_uint64;
	uint64 MaskBits = MAX_uint64;
	bool bStatic = true;
	bool bDynamic = true;
	bool bKinematic = true;
	bool bSensors = false;
	bool bSolids = true;
	int32 MaxHits = 1;
	uint64 UserTag = 0;
	TArray<FBox3DAsyncBodyId> IgnoredBodies;
	/** Qualified ignore identities reject stale or foreign worlds at submission. */
	TArray<FBox3DAsyncBodyHandle> IgnoredBodyHandles;
};

/** General query descriptor; the original name remains available for source compatibility. */
using FBox3DAsyncQueryDesc = FBox3DAsyncLineQuery;

struct BOX3DASYNC_API FBox3DAsyncLineHit
{
	FBox3DAsyncBodyId BodyId = 0;
	FBox3DAsyncShapeId ShapeId = 0;
	FBox3DAsyncBodyHandle BodyHandle;
	FBox3DAsyncShapeHandle ShapeHandle;
	/** Native b3ShapeType value, or -1 when unavailable. */
	int32 ShapeType = -1;
	float Fraction = 1.0f;
	float Distance = 0.0f;
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
	FBox3DAsyncSampleStamp Sample;
	/** Distinguishes exact overlaps from broad-phase candidates. Overlap hits have no contact data. */
	EBox3DAsyncQueryKind Kind = EBox3DAsyncQueryKind::Line;
	EBox3DAsyncQueryStatus Status = EBox3DAsyncQueryStatus::Miss;
	EBox3DAsyncQueryFailure Failure = EBox3DAsyncQueryFailure::None;
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
	/** Per-world counters reset with the subsystem. Timing uses monotonic seconds. */
	uint64 AcceptedQueries = 0;
	uint64 RejectedQueries = 0;
	uint64 TruncatedQueries = 0;
	int32 OutstandingQueries = 0;
	int32 PeakOutstandingQueries = 0;
	uint64 QueryReservedBytes = 0;
	uint64 PeakQueryReservedBytes = 0;
	uint64 ExecutedQueries = 0;
	double TotalQueryExecutionSeconds = 0.0;
	double MaxQueryExecutionSeconds = 0.0;
	uint64 DeliveredQueryResults = 0;
	double TotalQueryLatencySeconds = 0.0;
	double MaxQueryLatencySeconds = 0.0;
	int32 EventIngressBatches = 0;
	int32 PeakEventIngressBatches = 0;
	uint64 EventIngressBytes = 0;
	uint64 PeakEventIngressBytes = 0;
	int32 EventHistoryBatches = 0;
	int32 PeakEventHistoryBatches = 0;
	uint64 EventHistoryBytes = 0;
	uint64 PeakEventHistoryBytes = 0;
	uint64 EventBaselineBytes = 0;
	/** Lost subscription batches and known record counts; unknown losses may contain more records. */
	uint64 DroppedEventBatches = 0;
	uint64 DroppedEventRecords = 0;
	uint64 EventResyncs = 0;
	uint64 EventBaselineUnavailable = 0;
};

/**
 * Runs a private Box3D world on one dedicated worker. Initialize, Deinitialize,
 * and all public methods must be called on the game thread. Inputs and returned
 * transforms use Unreal centimeters. The caller owns presentation and must apply
 * sampled transforms to actors/components on the game thread. Deinitialize joins
 * the worker before destroying its native world.
 */
UCLASS()
class BOX3DASYNC_API UBox3DAsyncSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	UBox3DAsyncSubsystem();
	static constexpr int32 CommandCapacity = 256;
	static constexpr int32 OutstandingTraceCapacity = 64;
	static constexpr int32 OutstandingCreationResultCapacity = 256;
	/** Query reservations include queued, executing, ready, and unread records. */
	static constexpr int32 QueryCapacity = 64;
	static constexpr int32 QueryCapacityPerConsumer = 16;
	static constexpr uint64 QueryReservedBytes = 2ull * 1024 * 1024;
	static constexpr uint64 QueryReservedBytesPerConsumer = 512ull * 1024;
	/// Per-world admission budget for accepted payloads, estimated cook scratch, and retained native meshes. It excludes caller-owned inputs and temporary validation allocations.
	static constexpr uint64 MaxMeshGeometryBytes = 64ull * 1024ull * 1024ull;
	virtual ~UBox3DAsyncSubsystem() override;

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

	/** Independent owner for actorless creation acknowledgments. */
	bool TryRegisterCreationConsumer(FBox3DAsyncCreationConsumer& OutConsumer);
	void UnregisterCreationConsumer(FBox3DAsyncCreationConsumer Consumer);
	bool TryCreateBody(FBox3DAsyncCreationConsumer Consumer, const FBox3DAsyncBodyDesc& Desc,
	                   FBox3DAsyncBodyHandle& OutBody);
	bool TryTakeCreationResult(FBox3DAsyncCreationConsumer Consumer, FBox3DAsyncBodyHandle Body,
	                           FBox3DAsyncCreationResult& OutResult);
	/** Query callbacks are dispatched on the game thread in the world subsystem tick. */
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	/** Queue a static box. Half extents are centimeters; transform scale must be one. */
	bool TryCreateStaticBox(const FTransform& Transform, const FVector& HalfExtents, FBox3DAsyncBodyId& OutBodyId);
	/** Actorless box with copied filter and event flags. */
	bool TryCreateStaticBox(const FTransform& Transform, const FVector& HalfExtents,
	                        const FBox3DAsyncShapeOptions& Options, FBox3DAsyncBodyId& OutBodyId);
	bool TryCreateStaticBox(const FTransform& Transform, const FVector& HalfExtents,
	                        const FBox3DAsyncShapeOptions& Options, FBox3DAsyncBodyHandle& OutBody);
	/** Queue a dynamic sphere. Radius is centimeters and velocity is centimeters per second. */
	bool TryCreateDynamicSphere(const FTransform& Transform, float Radius, const FVector& InitialVelocity, FBox3DAsyncBodyId& OutBodyId);
	/** Actorless sphere with copied filter and event flags. */
	bool TryCreateDynamicSphere(const FTransform& Transform, float Radius, const FVector& InitialVelocity,
	                            const FBox3DAsyncShapeOptions& Options, FBox3DAsyncBodyId& OutBodyId);
	bool TryCreateDynamicSphere(const FTransform& Transform, float Radius, const FVector& InitialVelocity,
	                            const FBox3DAsyncShapeOptions& Options, FBox3DAsyncBodyHandle& OutBody);
	/** Drain the single raw stream. Removed relationships end at the first subsequent real step.
	 * Missing identity or a capacity overflow sets a sticky gap; no subsequent events are published.
	 * Once bOutGap is true the stream is invalid until world teardown. */
	void DrainRawEvents(TArray<FBox3DAsyncEventBatch>& OutBatches, bool& bOutGap);
#if WITH_DEV_AUTOMATION_TESTS
	/** Override the native event-record cap to exercise recovery under sustained pressure. */
	void SetEventRecordLimitForTest(int32 Limit);
#endif
	/** Independent polling subscription. The first delivery is a complete baseline. */
	bool SubscribeEvents(const FBox3DAsyncEventFilter& Filter, FBox3DAsyncEventSubscription& OutSubscription);
	/** Callback subscriptions use the same cursor contract and run on the game thread.
	 * Owner is weak; destruction prevents subsequent callbacks. */
	bool SubscribeEvents(const FBox3DAsyncEventFilter& Filter, UObject* Owner,
	                     TFunction<void(const FBox3DAsyncEventDelivery&)> Callback,
	                     bool bNotifyOnClose, FBox3DAsyncEventSubscription& OutSubscription);
	void UnsubscribeEvents(FBox3DAsyncEventSubscription Subscription);
	/** Returns one baseline, delta, or gap/control record. Call again to consume more. */
	bool PollEvents(FBox3DAsyncEventSubscription Subscription, FBox3DAsyncEventDelivery& OutDelivery);
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
	/** Static authored mesh body with an explicit filter/event policy shared by all copied shapes. */
	bool TryCreateStaticMeshCollision(UStaticMesh* Mesh, const FTransform& Transform, const FVector& Scale,
		const FBox3DAsyncShapeOptions& Options, FBox3DAsyncBodyId& OutBodyId);
	/** Queue a dynamic body using the mesh's authored simple collision. Round shapes require uniform positive scale. */
	bool TryCreateDynamicMeshCollision(UStaticMesh* Mesh, const FTransform& Transform, const FVector& Scale,
		const FVector& InitialVelocity, FBox3DAsyncBodyId& OutBodyId);
	/** Dynamic authored mesh body with an explicit filter/event policy shared by all copied shapes. */
	bool TryCreateDynamicMeshCollision(UStaticMesh* Mesh, const FTransform& Transform, const FVector& Scale,
		const FVector& InitialVelocity, const FBox3DAsyncShapeOptions& Options, FBox3DAsyncBodyId& OutBodyId);
	/** Queue a static instance of registered reusable geometry with positive scale components (minimum 1e-4). */
	bool TryCreateStaticMeshInstance(FBox3DAsyncGeometryId GeometryId, const FTransform& Transform,
		const FVector& Scale, FBox3DAsyncBodyId& OutBodyId);
	/** Request release. Existing instances retain the cooked mesh until they are removed. */
	bool TryReleaseMeshGeometry(FBox3DAsyncGeometryId GeometryId);
	/** Queue removal of a previously accepted body ID. */
	bool TryRemoveBody(FBox3DAsyncBodyId BodyId);
	bool TryRemoveBody(FBox3DAsyncBodyHandle Body);

	/** Enqueue a copied body command. True means accepted, not that native execution succeeded.
	 * Commands are ordered with body creation/removal; a failed create makes later commands no-ops.
	 * Force and torque commands affect one real worker step. The caller must retry capacity failures. */
	bool TrySubmitBodyCommand(FBox3DAsyncBodyHandle Body, const FBox3DAsyncBodyCommand& Command);
	/** Atomically enable or disable a bounded set at a worker step boundary. */
	bool TrySetBodiesEnabled(const TArray<FBox3DAsyncBodyHandle>& Bodies, bool bEnabled);
	/** Apply one total linear impulse distributed among dynamic bodies by mass. */
	bool TryApplyGroupImpulse(const TArray<FBox3DAsyncBodyHandle>& Bodies, FVector Impulse);

	/** Fields persist until removed or world teardown. Accepted operations are applied in queue order. */
	bool TryCreateField(const FBox3DAsyncFieldDesc& Desc, FBox3DAsyncFieldHandle& OutField);
	bool TryUpdateField(FBox3DAsyncFieldHandle Field, const FBox3DAsyncFieldDesc& Desc);
	bool TryRemoveField(FBox3DAsyncFieldHandle Field);

	/** Creation is asynchronous; poll GetJointStatus. Both body handles must belong to this world. */
	bool TryCreateJoint(const FBox3DAsyncJointDesc& Desc, FBox3DAsyncJointHandle& OutJoint);
	EBox3DAsyncJointStatus GetJointStatus(FBox3DAsyncJointHandle Joint) const;
	bool TryGetLatestJointState(FBox3DAsyncJointHandle Joint, FBox3DAsyncJointState& OutState) const;
	/** Queued controls apply at a fixed-step boundary and may be queued while creation is pending. */
	bool TrySubmitJointCommand(FBox3DAsyncJointHandle Joint, const FBox3DAsyncJointCommand& Command);
	/** Update tunable properties; joint type, bodies and local frames remain fixed.
	 * Native spherical cone updates are limited to 90 degrees, even though creation allows wider cones. */
	bool TryUpdateJoint(FBox3DAsyncJointHandle Joint, const FBox3DAsyncJointDesc& Settings);
	/** Removes an active, pending or failed joint; retry if the command queue is full. */
	bool TryRemoveJoint(FBox3DAsyncJointHandle Joint);
	/** Single raw stream. Gap is sticky after overflow; poll joint status to resynchronize. */
	void DrainJointBreakEvents(TArray<FBox3DAsyncJointBreakEvent>& OutEvents, bool& bOutGap);

	/** Register a polling owner for bounded, independently routed line queries. */
	bool TryRegisterQueryConsumer(FBox3DAsyncQueryConsumer& OutConsumer);
	/** Removes the consumer and its unread requests. An executing query finishes before storage is released. */
	void UnregisterQueryConsumer(FBox3DAsyncQueryConsumer Consumer);
	bool TrySubmitQuery(FBox3DAsyncQueryConsumer Consumer, const FBox3DAsyncLineQuery& Query,
	                    FBox3DAsyncQueryHandle& OutRequest);
	/** Detailed variant of TrySubmitQuery. Rejected requests have an empty handle. */
	EBox3DAsyncQueryAdmission SubmitQuery(FBox3DAsyncQueryConsumer Consumer, const FBox3DAsyncLineQuery& Query,
	                                     FBox3DAsyncQueryHandle& OutRequest);
	bool TryCancelQuery(FBox3DAsyncQueryConsumer Consumer, FBox3DAsyncQueryHandle Request);
	bool TryTakeQueryResult(FBox3DAsyncQueryConsumer Consumer, FBox3DAsyncQueryHandle Request,
	                        FBox3DAsyncQueryResult& OutResult);
	/** Transfer a request to callback delivery. Owner is weak and must remain valid at dispatch. */
	bool TrySetQueryCallback(FBox3DAsyncQueryConsumer Consumer, FBox3DAsyncQueryHandle Request,
	                         UObject* Owner, TFunction<void(const FBox3DAsyncQueryResult&)> Callback);
	/** Register a game-thread shutdown-window notification for a polling consumer. During the
	 * notification, committed results and WorldEnded outcomes can still be taken. */
	bool SetQueryShutdownCallback(FBox3DAsyncQueryConsumer Consumer, UObject* Owner, TFunction<void()> Callback);
	/** Pump ready query callbacks once; reentrant calls are ignored. */
	void PumpQueryCallbacks();

#if WITH_DEV_AUTOMATION_TESTS
	/** Test-only worker gate: 1 before selecting a query, 2 after selection. */
	void SetQueryTestGate(int32 Gate);
	int32 GetQueryTestGateReached() const;
	/** Returns 0 queued, 1 executing, 2 worker-ready, or -1 if absent. */
	int32 GetQueryTestPhase(FBox3DAsyncQueryHandle Request) const;
#endif

	/** Queue a closest static-body trace. Start and End are centimeters. */
	bool TrySubmitLineTrace(const FVector& Start, const FVector& End, FBox3DAsyncTraceRequestId& OutRequestId);
	/** Drain ready results; accepted requests remain capacity-accounted until drained. */
	void DrainTraceResults(TArray<FBox3DAsyncTraceResult>& OutResults);
	/** Drain asynchronous native body creation, geometry cooking, and release results. */
	void DrainCreationResults(TArray<FBox3DAsyncCreationResult>& OutResults);

	/** Sample interpolated dynamic poses at monotonic time minus two fixed steps. */
	void SamplePoses(TArray<FBox3DAsyncPose>& OutPoses, double* OutSampleTimeSeconds = nullptr) const;
	/** Copy the newest completed worker publication without rendering interpolation. */
	bool TryGetLatestPoseFrame(FBox3DAsyncPoseFrame& OutFrame) const;
	/** Latest completed state, not a synchronous native read. Returns false for unavailable or removed bodies. */
	bool TryGetLatestBodyState(FBox3DAsyncBodyHandle Body, FBox3DAsyncBodyState& OutState) const;
	uint64 GetCompletedStepCount() const;
	FBox3DAsyncDiagnostics GetDiagnostics() const;

private:
	void DispatchQueryCallbacks(bool bShutdown);
	bool TryCreateSimpleBox(const FTransform& Transform, const FVector& HalfExtents,
	                        const FBox3DAsyncShapeOptions& Options, bool bRequireIdentity, FBox3DAsyncBodyId& OutBodyId);
	bool TryCreateSimpleSphere(const FTransform& Transform, float Radius, const FVector& InitialVelocity,
	                           const FBox3DAsyncShapeOptions& Options, bool bRequireIdentity, FBox3DAsyncBodyId& OutBodyId);
	void PumpEventSubscriptions();
	void CloseEventSubscriptions();
	bool TryCreateAuthoredMeshCollision(UStaticMesh* Mesh, const FTransform& Transform, const FVector& Scale,
		const FVector& InitialVelocity, bool bDynamic, const FBox3DAsyncShapeOptions* Options,
		FBox3DAsyncBodyId& OutBodyId);
	struct FImpl;
	struct FImplDeleter
	{
		void operator()(FImpl* Pointer) const;
	};
	TUniquePtr<FImpl, FImplDeleter> Impl;
};
