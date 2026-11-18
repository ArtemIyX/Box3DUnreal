#pragma once

#include "World/Box3DAsyncSubsystem.h"
#include "HAL/Event.h"
#include "HAL/PlatformProcess.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "Misc/ScopeLock.h"
#include "PhysicsEngine/BodySetup.h"
#include "box3d/box3d.h"
#include "Box3DAsyncStats.h"

namespace Box3DAsyncPrivate
{
#if WITH_DEV_AUTOMATION_TESTS
	BOX3DASYNC_API bool ValidateAndCopyGeometryForTest(const TArray<FVector>& Vertices, const TArray<int32>& Indices,
	                                                   EBox3DAsyncMeshWinding Winding, TArray<b3Vec3>& OutVertices,
	                                                   TArray<int32>& OutIndices,
	                                                   uint64& OutByteCount, FVector& OutBoundsMin,
	                                                   FVector& OutBoundsMax);
	BOX3DASYNC_API bool IsCompleteMeshCookForTest(const b3MeshData* MeshData, int32 ExpectedTriangleCount);
#endif

	enum class EStaticShapeType : uint8 { Sphere, Capsule, Hull };

	struct FCopiedStaticShape
	{
		EStaticShapeType Type = EStaticShapeType::Hull;
		b3Sphere Sphere{};
		b3Capsule Capsule{};
		TArray<b3Vec3> HullPoints;
	};

	struct FCopiedBodyShape
	{
		EStaticShapeType Type = EStaticShapeType::Hull;
		b3Sphere Sphere{};
		b3Capsule Capsule{};
		TArray<b3Vec3> HullPoints;
		FBox3DAsyncShapeOptions Options;
		float Friction = 0.6f;
		float Restitution = 0.0f;
		uint64 SubshapeKey = 0;
	};

	BOX3DASYNC_API bool IsSupportedAuthoredCollisionScale(bool bHasRoundShapes, const FVector& Scale);
	BOX3DASYNC_API bool CopyAuthoredCollisionShapes(const UBodySetup& BodySetup, const FVector& Scale,
	                                                TArray<FCopiedStaticShape>& OutShapes, FVector& OutBoundsMin,
	                                                FVector& OutBoundsMax, uint64& OutPayloadBytes);
}

struct UBox3DAsyncSubsystem::FImpl
{
	enum class ECommandType : uint8
	{
		CreateBox, CreateSphere, RegisterGeometry, CreateMeshInstance, CreateAuthoredStaticBody,
		CreateAuthoredDynamicBody, CreateActorlessBody, ReleaseGeometry, Remove, BodyCommand, SetField, RemoveField, CreateJoint, RemoveJoint, JointCommand, UpdateJoint, SetBodiesEnabled, GroupImpulse
	};

	struct FCommand
	{
		ECommandType Type = ECommandType::Remove;
		FBox3DAsyncBodyId BodyId = 0;
		FBox3DAsyncGeometryId GeometryId = 0;
		FTransform Transform = FTransform::Identity;
		FVector Dimensions = FVector::ZeroVector;
		FVector InitialVelocity = FVector::ZeroVector;
		FVector InitialAngularVelocity = FVector::ZeroVector;
		float GravityScale = 1.0f;
		float LinearDamping = 0.0f;
		float AngularDamping = 0.0f;
		float MassOverride = 0.0f;
		bool bEnableSleep = true;
		bool bStartAwake = true;
		bool bStartEnabled = true;
		bool bBullet = false;
		FVector Scale = FVector::OneVector;
		FBox3DAsyncShapeOptions ShapeOptions;
		FBox3DAsyncBodyCommand BodyCommand;
		FBox3DAsyncFieldDesc Field;
		uint64 FieldId = 0;
		uint64 JointId = 0;
		FBox3DAsyncJointDesc Joint;
		FBox3DAsyncJointCommand JointControl;
		TArray<uint64> BodyIds;
		bool bBodiesEnabled = true;
		bool bHasShapeOptions = false;
		int32 ReservedShapeIdentities = 0;
		uint64 PayloadBytes = 0;
		TArray<b3Vec3> Vertices;
		TArray<int32> Indices;
		TArray<Box3DAsyncPrivate::FCopiedStaticShape> StaticShapes;
		TArray<Box3DAsyncPrivate::FCopiedBodyShape> BodyShapes;
		bool bDynamic = false;
		bool bKinematic = false;
	};

	struct FTraceRequest
	{
		FBox3DAsyncTraceRequestId RequestId = 0;
		FVector Start = FVector::ZeroVector;
		FVector End = FVector::ZeroVector;
	};

	struct FQueryRecord
	{
		uint64 ConsumerId = 0;
		FBox3DAsyncLineQuery Query;
		FBox3DAsyncQueryResult Result;
		uint64 ReservedBytes = 0;
		double SubmittedSeconds = 0.0;
		bool bExecuting = false;
		bool bReady = false;
		// The game thread commits a ready outcome, or a cancellation claims it.
		bool bCommitted = false;
		bool bCancelled = false;
		bool bAbandoned = false;
	};

	struct FSnapshot
	{
		uint64 CaptureSerial = 0;
		double CaptureMonotonicSeconds = 0.0;
		FBox3DAsyncSampleStamp SimulationSample;
		double ScheduledTime = 0.0;
		TArray<FBox3DAsyncPose> Poses;
	};

	struct FBody
	{
		FBody() = default;

		FBody(b3BodyId InNativeId, bool bInDynamic, FBox3DAsyncGeometryId InGeometryId = 0)
			: NativeId(InNativeId), bDynamic(bInDynamic), GeometryId(InGeometryId)
		{
		}

		b3BodyId NativeId{};
		bool bDynamic = false;
		bool bHasDeferredVelocity = false;
		bool bDeferredStartAwake = true;
		FVector DeferredLinearVelocity = FVector::ZeroVector;
		FVector DeferredAngularVelocity = FVector::ZeroVector;
		FBox3DAsyncGeometryId GeometryId = 0;
	};

	struct FGeometry
	{
		b3MeshData* NativeData = nullptr;
		int32 InstanceCount = 0;
		int32 DegenerateTriangleCount = 0;
		bool bReleasePending = false;
	};

	class FWorker final : public FRunnable
	{
	public:
		explicit FWorker(FImpl& InOwner) : Owner(InOwner)
		{
		}

		virtual uint32 Run() override;

		virtual void Stop() override
		{
			Owner.bStopRequested.Store(true);
			Owner.WakeEvent->Trigger();
		}

	private:
		void ApplyCommands();
		void ExecuteTraces(double Deadline);
		void ExecuteQueries(double Deadline);
		bool RegisterBodyShapes(b3BodyId BodyId, FBox3DAsyncBodyId PublicBody, int32 Reserved, TArray<FBox3DAsyncShapeId>& OutShapeIds);
		void CollectEvents(uint64 Step);
		void StepAndPublish(double ScheduledTime, bool bAdvanceSimulation = true);
		FImpl& Owner;
	};

	b3WorldId WorldId{};
	int32 CommandQueueCapacity = UBox3DAsyncSubsystem::CommandCapacity;
	bool bPublishBodyStates = true;
	bool bPublishJointStates = true;
	int32 MaxPersistentFields = 64;
	float MaxFieldForce = 1.0e12f;
	int32 MaxJoints = 4096;
	int32 MaxJointBreakEvents = 1024;
	float FixedTimeStep = 1.0f / 60.0f;
	int32 SubStepCount = 1;
	TAtomic<bool> bStopRequested{false};
#if WITH_DEV_AUTOMATION_TESTS
	TAtomic<int32> QueryTestGate{0};
	TAtomic<int32> QueryTestGateReached{0};
	void WaitAtQueryTestGate(int32 Gate)
	{
		if (QueryTestGate.Load() != Gate) return;
		QueryTestGateReached.Store(Gate);
		while (QueryTestGate.Load() == Gate && !bStopRequested.Load()) FPlatformProcess::Sleep(0.001f);
		QueryTestGateReached.Store(0);
	}
#endif
	FCriticalSection DiagnosticsMutex;
	uint64 CompletedSteps = 0;
	FBox3DAsyncSampleStamp CompletedSample;
	uint64 CaptureSerial = 0;
	uint64 ClockEpoch = 0;
	uint64 NativeCreationFailures = 0;
	uint64 DeadlineMisses = 0;
	uint64 ClockRebases = 0;
	double LatestScheduledTime = 0.0;
	double LatestCompletionTime = 0.0;
	FCriticalSection QueueMutex;
	FCriticalSection SnapshotMutex;
	TSharedPtr<const TMap<uint64, FBox3DAsyncBodyState>, ESPMode::ThreadSafe> LatestBodyStates;
	TSharedPtr<const TMap<uint64, FBox3DAsyncJointState>, ESPMode::ThreadSafe> LatestJointStates;
	TArray<FCommand> Commands;
	TArray<FTraceRequest> TraceRequests;
	FGuid QueryWorld = FGuid::NewGuid();
	uint64 NextQueryConsumerId = 1;
	uint64 NextQueryId = 1;
	TSet<uint64> QueryConsumers;
	TMap<uint64, FQueryRecord> Queries;
	// QueueMutex protects these diagnostics and all query/event queue ownership.
	FBox3DAsyncDiagnostics ExchangeDiagnostics;
	static uint64 EventArrayBytes(const TArray<FBox3DAsyncEventBatch>& Batches)
	{
		uint64 Bytes = Batches.GetAllocatedSize();
		for (const FBox3DAsyncEventBatch& Batch : Batches) Bytes += Batch.Events.GetAllocatedSize();
		return Bytes;
	}
	// Independent storage limits. These account for owned UE container allocations,
	// not native Box3D's own event arrays. Access exchange storage under QueueMutex.
	static constexpr uint64 MaxEventIngressBytes = 4ull * 1024 * 1024;
	static constexpr uint64 MaxEventHistoryBytes = 8ull * 1024 * 1024;
	static constexpr uint64 MaxEventBaselineBytes = 4ull * 1024 * 1024;
	static constexpr uint64 MaxEventActiveBytes = 8ull * 1024 * 1024;
	static constexpr uint64 MaxEventScratchBytes = 4ull * 1024 * 1024;
	static constexpr int32 MaxEventCallbacksPerPump = 8;
	static constexpr int32 MaxEventBatchesPerPoll = 8;
#if WITH_DEV_AUTOMATION_TESTS
	// QueueMutex protects this per-world deterministic pressure seam.
	int32 EventRecordLimitForTest = 1024;
#endif
	void RecordQueryDelivery(const FQueryRecord& Record, double Now)
	{
		const double Latency = FMath::Max(0.0, Now - Record.SubmittedSeconds);
		++ExchangeDiagnostics.DeliveredQueryResults;
		ExchangeDiagnostics.TotalQueryLatencySeconds += Latency;
		ExchangeDiagnostics.MaxQueryLatencySeconds = FMath::Max(ExchangeDiagnostics.MaxQueryLatencySeconds, Latency);
	}
	// Game-thread only; never dereferenced by the worker.
	struct FQueryCallback
	{
		TWeakObjectPtr<UObject> Owner;
		TFunction<void(const FBox3DAsyncQueryResult&)> Callback;
	};
	struct FShutdownCallback
	{
		TWeakObjectPtr<UObject> Owner;
		TFunction<void()> Callback;
	};
	TMap<uint64, FQueryCallback> QueryCallbacks;
	TMap<uint64, FShutdownCallback> ShutdownCallbacks;
	bool bDispatchingQueries = false;
	bool bClosingQueries = false;
	TArray<FBox3DAsyncTraceResult> TraceResults;
	TArray<FBox3DAsyncCreationResult> CreationResults;
	uint64 NextCreationConsumerId = 1;
	TMap<uint64, TMap<FBox3DAsyncBodyId, FBox3DAsyncCreationResult>> OwnedCreationResults;
	TMap<FBox3DAsyncBodyId, uint64> CreationOwners;
	struct FShapeIdentity { FBox3DAsyncBodyId Body = 0; FBox3DAsyncShapeId Shape = 0; };
	TMap<uint64, FShapeIdentity> WorkerShapes;
	// Protected by QueueMutex. Reservations include accepted commands not yet finalized.
	int32 LiveShapeIdentities = 0;
	int32 ReservedShapeIdentities = 0;
	TMap<uint64, FBox3DAsyncEvent> ActiveContacts;
	TArray<FBox3DAsyncEvent> ActiveSensors;
	TSet<FBox3DAsyncBodyId> PendingRemovedBodies;
	TSet<FBox3DAsyncBodyId> RetiringBodies;
	uint64 NextShapeId = 1;
	uint64 NextEventSequence = 1;
	TArray<FBox3DAsyncEventBatch> EventBatches;
	bool bEventGap = false;
	// Separate bounded transport for subscriptions. The legacy drain owns EventBatches.
	TArray<FBox3DAsyncEventBatch> EventIngress;
	TArray<FBox3DAsyncEventBatch> EventHistory;
	FBox3DAsyncEventBatch EventBaseline;
	bool bEventBaselineReady = false;
	bool bEventTrackingValid = true;
	uint64 EventSequence = 0;
	uint64 FirstLostEventSequence = 0;
	uint64 LastLostEventSequence = 0;
	struct FEventSubscriber
	{
		FBox3DAsyncEventFilter Filter;
		uint64 NextSequence = 0;
		bool bNeedsBaseline = true;
		bool bReportGap = false;
		uint64 FirstMissed = 0;
		uint64 LastMissed = 0;
		TArray<FBox3DAsyncEvent> ActiveShapePairs;
		TWeakObjectPtr<UObject> Owner;
		TFunction<void(const FBox3DAsyncEventDelivery&)> Callback;
		bool bNotifyOnClose = false;
	};
	TMap<uint64, FEventSubscriber> EventSubscribers;
	uint64 NextEventSubscriberId = 1;
	uint64 LastPumpedEventSubscriberId = 0;
	bool bPumpingEvents = false;
	bool bClosingEvents = false;
	int32 InFlightTraces = 0;
	TSet<FBox3DAsyncBodyId> AcceptedBodyIds;
	TSet<uint64> AcceptedFieldIds;
	uint64 NextFieldId = 1;
	TMap<uint64, FBox3DAsyncFieldDesc> WorkerFields;
	uint64 NextJointId = 1;
	TMap<uint64, EBox3DAsyncJointStatus> JointStatuses;
	TArray<FBox3DAsyncJointBreakEvent> JointBreakEvents;
	bool bJointBreakEventGap = false;
	TMap<uint64, EBox3DAsyncJointType> JointTypes;
	struct FJoint
	{
		b3JointId NativeId{};
		uint64 BodyA = 0;
		uint64 BodyB = 0;
	};
	TMap<uint64, FJoint> WorkerJoints;
	TMap<uint64, uint64> JointIdsByNative;
	TSet<FBox3DAsyncGeometryId> AcceptedGeometryIds;
	TMap<FBox3DAsyncGeometryId, uint64> GeometryByteReservations;

	struct FGeometryBounds
	{
		FVector Min = FVector::ZeroVector;
		FVector Max = FVector::ZeroVector;
	};

	TMap<FBox3DAsyncGeometryId, FGeometryBounds> AcceptedGeometryBounds;
	uint64 ReservedGeometryBytes = 0;
	uint64 ReservedStaticShapeBytes = 0;
	int32 OutstandingCreationResults = 0;
	FBox3DAsyncBodyId NextBodyId = 1;
	FBox3DAsyncTraceRequestId NextRequestId = 1;
	FBox3DAsyncGeometryId NextGeometryId = 1;
	TArray<TSharedPtr<const FSnapshot, ESPMode::ThreadSafe>> Snapshots;
	TMap<FBox3DAsyncBodyId, FBody> WorkerBodies;
	TMap<FBox3DAsyncGeometryId, FGeometry> WorkerGeometries;
	FEvent* WakeEvent = nullptr;
	TUniquePtr<FWorker> Worker;
	FRunnableThread* Thread = nullptr;
	bool bAccepting = false;
	bool bStatsRegistered = false;

	void PublishCreationResult(EBox3DAsyncCreationKind Kind, uint64 ResourceId, bool bSucceeded,
	                           int32 DegenerateCount = 0, int32 DroppedTriangleCount = 0, TArray<FBox3DAsyncShapeId> ShapeIds = {},
	                           TArray<uint64> SubshapeKeys = {})
	{
		FScopeLock Lock(&QueueMutex);
		FBox3DAsyncCreationResult Result;
		Result.Kind = Kind;
		Result.ResourceId = ResourceId;
		Result.bSucceeded = bSucceeded;
		Result.DegenerateTriangleCount = DegenerateCount;
		Result.DroppedTriangleCount = DroppedTriangleCount;
		Result.ShapeIds = MoveTemp(ShapeIds);
		Result.SubshapeKeys = MoveTemp(SubshapeKeys);
		if (Kind == EBox3DAsyncCreationKind::Body && bSucceeded)
		{
			Result.BodyHandle = {QueryWorld, ResourceId};
			Result.ShapeHandles.Reserve(Result.ShapeIds.Num());
			for (FBox3DAsyncShapeId Id : Result.ShapeIds) Result.ShapeHandles.Add({QueryWorld, Id});
		}
		if (Kind == EBox3DAsyncCreationKind::Body)
		{
			if (uint64* ConsumerId = CreationOwners.Find(ResourceId))
			{
				if (auto* Results = OwnedCreationResults.Find(*ConsumerId)) Results->Add(ResourceId, MoveTemp(Result));
				else --OutstandingCreationResults; // Owner detached while the command was executing.
				CreationOwners.Remove(ResourceId);
				return;
			}
		}
		CreationResults.Add(MoveTemp(Result));
	}

	void Shutdown()
	{
		{
			FScopeLock Lock(&QueueMutex);
			bAccepting = false;
		}
		if (Worker.IsValid())
		{
			Worker->Stop();
		}
		if (Thread != nullptr)
		{
			Thread->WaitForCompletion();
			delete Thread;
			Thread = nullptr;
		}
		Worker.Reset();
		if (bStatsRegistered)
		{
			Box3DAsyncStats::UnregisterWorld(this);
			bStatsRegistered = false;
		}
		if (b3World_IsValid(WorldId))
		{
			b3DestroyWorld(WorldId);
			WorldId = b3WorldId{};
		}
	}

	~FImpl()
	{
		if (WakeEvent != nullptr)
		{
			FPlatformProcess::ReturnSynchEventToPool(WakeEvent);
		}
	}
};
