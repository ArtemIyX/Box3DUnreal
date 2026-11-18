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
		CreateAuthoredDynamicBody, ReleaseGeometry, Remove
	};

	struct FCommand
	{
		ECommandType Type = ECommandType::Remove;
		FBox3DAsyncBodyId BodyId = 0;
		FBox3DAsyncGeometryId GeometryId = 0;
		FTransform Transform = FTransform::Identity;
		FVector Dimensions = FVector::ZeroVector;
		FVector InitialVelocity = FVector::ZeroVector;
		FVector Scale = FVector::OneVector;
		FBox3DAsyncShapeOptions ShapeOptions;
		bool bHasShapeOptions = false;
		uint64 PayloadBytes = 0;
		TArray<b3Vec3> Vertices;
		TArray<int32> Indices;
		TArray<Box3DAsyncPrivate::FCopiedStaticShape> StaticShapes;
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
		bool bExecuting = false;
		bool bReady = false;
		bool bCancelled = false;
		bool bAbandoned = false;
	};

	struct FSnapshot
	{
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
		bool RegisterBodyShapes(b3BodyId BodyId, FBox3DAsyncBodyId PublicBody, TArray<FBox3DAsyncShapeId>& OutShapeIds);
		void CollectEvents(uint64 Step);
		void StepAndPublish(double ScheduledTime, bool bAdvanceSimulation = true);
		FImpl& Owner;
	};

	b3WorldId WorldId{};
	float FixedTimeStep = 1.0f / 60.0f;
	int32 SubStepCount = 1;
	TAtomic<bool> bStopRequested{false};
	FCriticalSection DiagnosticsMutex;
	uint64 CompletedSteps = 0;
	uint64 NativeCreationFailures = 0;
	uint64 DeadlineMisses = 0;
	uint64 ClockRebases = 0;
	double LatestScheduledTime = 0.0;
	double LatestCompletionTime = 0.0;
	FCriticalSection QueueMutex;
	FCriticalSection SnapshotMutex;
	TArray<FCommand> Commands;
	TArray<FTraceRequest> TraceRequests;
	FGuid QueryWorld = FGuid::NewGuid();
	uint64 NextQueryConsumerId = 1;
	uint64 NextQueryId = 1;
	TSet<uint64> QueryConsumers;
	TMap<uint64, FQueryRecord> Queries;
	TArray<FBox3DAsyncTraceResult> TraceResults;
	TArray<FBox3DAsyncCreationResult> CreationResults;
	struct FShapeIdentity { FBox3DAsyncBodyId Body = 0; FBox3DAsyncShapeId Shape = 0; };
	TMap<uint64, FShapeIdentity> WorkerShapes;
	TMap<uint64, FBox3DAsyncEvent> ActiveContacts;
	TArray<FBox3DAsyncEvent> ActiveSensors;
	TSet<FBox3DAsyncBodyId> PendingRemovedBodies;
	TSet<FBox3DAsyncBodyId> RetiringBodies;
	uint64 NextShapeId = 1;
	uint64 NextEventSequence = 1;
	TArray<FBox3DAsyncEventBatch> EventBatches;
	bool bEventGap = false;
	int32 InFlightTraces = 0;
	TSet<FBox3DAsyncBodyId> AcceptedBodyIds;
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
	                           int32 DegenerateCount = 0, int32 DroppedTriangleCount = 0, TArray<FBox3DAsyncShapeId> ShapeIds = {})
	{
		FScopeLock Lock(&QueueMutex);
		FBox3DAsyncCreationResult& Result = CreationResults.AddDefaulted_GetRef();
		Result.Kind = Kind;
		Result.ResourceId = ResourceId;
		Result.bSucceeded = bSucceeded;
		Result.DegenerateTriangleCount = DegenerateCount;
		Result.DroppedTriangleCount = DroppedTriangleCount;
		Result.ShapeIds = MoveTemp(ShapeIds);
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
