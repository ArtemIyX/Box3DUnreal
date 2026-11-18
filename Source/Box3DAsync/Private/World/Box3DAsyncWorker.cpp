#include "Box3DAsyncSubsystemImpl.h"

#include "HAL/PlatformTime.h"
#include "Misc/ScopeExit.h"
#include "Simulation/Box3DConversion.h"
#include "box3d/collision.h"

namespace
{
	constexpr uint64 StaticCategory = 1ull << 0;
	constexpr uint64 DynamicCategory = 1ull << 1;
	constexpr int32 SnapshotCapacity = 8;
	constexpr int32 MaxCatchUpSteps = 4;

	bool IsCompleteMeshCook(const b3MeshData* MeshData, int32 ExpectedTriangleCount)
	{
		return MeshData != nullptr && MeshData->byteCount > 0 && MeshData->degenerateCount == 0
			&& MeshData->triangleCount == ExpectedTriangleCount;
	}
}

#if WITH_DEV_AUTOMATION_TESTS
bool Box3DAsyncPrivate::IsCompleteMeshCookForTest(const b3MeshData* MeshData, int32 ExpectedTriangleCount)
{
	return IsCompleteMeshCook(MeshData, ExpectedTriangleCount);
}
#endif

uint32 UBox3DAsyncSubsystem::FImpl::FWorker::Run()
{
	double NextDeadline = FPlatformTime::Seconds() + Owner.FixedTimeStep;
	while (!Owner.bStopRequested.Load())
	{
		double Now = FPlatformTime::Seconds();
		int32 StepsThisWake = 0;
		if (Now >= NextDeadline)
		{
			// Apply queued mutations only at a fixed-step boundary. A trace between
			// boundaries then observes the last fully completed simulation state.
			ApplyCommands();
		}
		while (Now >= NextDeadline && StepsThisWake < MaxCatchUpSteps && !Owner.bStopRequested.Load())
		{
			StepAndPublish(NextDeadline);
			NextDeadline += Owner.FixedTimeStep;
			++StepsThisWake;
			Now = FPlatformTime::Seconds();
		}

		if (Now >= NextDeadline && !Owner.bStopRequested.Load())
		{
			{
				FScopeLock Lock(&Owner.DiagnosticsMutex);
				++Owner.DeadlineMisses;
				++Owner.ClockRebases;
			}
			NextDeadline = Now + Owner.FixedTimeStep;
			{
				FScopeLock Lock(&Owner.SnapshotMutex);
				Owner.Snapshots.Reset();
			}
			StepAndPublish(Now, false);
		}
		ExecuteTraces();

		Now = FPlatformTime::Seconds();
		const double Remaining = FMath::Max(0.0, NextDeadline - Now);
		const uint32 WaitMilliseconds = static_cast<uint32>(FMath::Clamp(
			FMath::CeilToDouble(Remaining * 1000.0), 1.0, double(MAX_uint32 - 1)));
		Owner.WakeEvent->Wait(WaitMilliseconds);
	}
	for (const TPair<FBox3DAsyncBodyId, FBody>& Body : Owner.WorkerBodies)
	{
		b3DestroyBody(Body.Value.NativeId);
	}
	Owner.WorkerBodies.Reset();
	for (const TPair<FBox3DAsyncGeometryId, FGeometry>& Geometry : Owner.WorkerGeometries)
	{
		if (Geometry.Value.NativeData != nullptr)
		{
			b3DestroyMesh(Geometry.Value.NativeData);
		}
	}
	Owner.WorkerGeometries.Reset();
	return 0;
}

void UBox3DAsyncSubsystem::FImpl::FWorker::ApplyCommands()
{
	TArray<FCommand> LocalCommands;
	{
		FScopeLock Lock(&Owner.QueueMutex);
		Swap(LocalCommands, Owner.Commands);
	}

	for (FCommand& Command : LocalCommands)
	{
		ON_SCOPE_EXIT
		{
			if (Command.PayloadBytes > 0)
			{
				FScopeLock Lock(&Owner.QueueMutex);
				Owner.ReservedStaticShapeBytes -= Command.PayloadBytes;
			}
		};

		if (Command.Type == ECommandType::Remove)
		{
			if (FBody* Body = Owner.WorkerBodies.Find(Command.BodyId))
			{
				const FBox3DAsyncGeometryId GeometryId = Body->GeometryId;
				b3DestroyBody(Body->NativeId);
				Owner.WorkerBodies.Remove(Command.BodyId);
				if (GeometryId != 0)
				{
					if (FGeometry* Geometry = Owner.WorkerGeometries.Find(GeometryId))
					{
						--Geometry->InstanceCount;
						if (Geometry->bReleasePending && Geometry->InstanceCount == 0)
						{
							b3DestroyMesh(Geometry->NativeData);
							Owner.WorkerGeometries.Remove(GeometryId);
							{
								FScopeLock Lock(&Owner.QueueMutex);
								if (uint64* Bytes = Owner.GeometryByteReservations.Find(GeometryId))
								{
									Owner.ReservedGeometryBytes -= *Bytes;
									Owner.GeometryByteReservations.Remove(GeometryId);
								}
							}
							Owner.PublishCreationResult(EBox3DAsyncCreationKind::MeshRelease, GeometryId, true);
						}
					}
				}
			}
			continue;
		}

		if (Command.Type == ECommandType::RegisterGeometry)
		{
			b3MeshDef MeshDef = {};
			MeshDef.vertices = Command.Vertices.GetData();
			MeshDef.indices = Command.Indices.GetData();
			MeshDef.vertexCount = Command.Vertices.Num();
			MeshDef.triangleCount = Command.Indices.Num() / 3;
			MeshDef.weldVertices = true;
			MeshDef.weldTolerance = 0.001f;
			MeshDef.identifyEdges = true;
			TArray<int> DegenerateTriangles;
			DegenerateTriangles.SetNumZeroed(MeshDef.triangleCount);
			b3MeshData* MeshData = b3CreateMesh(&MeshDef, DegenerateTriangles.GetData(), DegenerateTriangles.Num());
			const uint64 CookedBytes = MeshData != nullptr && MeshData->byteCount > 0
				? uint64(MeshData->byteCount) : 0;
			const int32 CookedDegenerateCount = MeshData != nullptr ? MeshData->degenerateCount : 0;
			const int32 DroppedTriangleCount = MeshData != nullptr
				? FMath::Max(0, MeshDef.triangleCount - MeshData->triangleCount)
				: MeshDef.triangleCount;
			bool bCookSucceeded = IsCompleteMeshCook(MeshData, MeshDef.triangleCount)
				&& CookedBytes <= UBox3DAsyncSubsystem::MaxMeshGeometryBytes;
			{
				FScopeLock Lock(&Owner.QueueMutex);
				const uint64* ReservedForThisGeometry = Owner.GeometryByteReservations.Find(Command.GeometryId);
				const uint64 SourceBytes = ReservedForThisGeometry != nullptr ? *ReservedForThisGeometry : 0;
				const uint64 OtherReservedBytes = Owner.ReservedGeometryBytes - SourceBytes;
				bCookSucceeded &= ReservedForThisGeometry != nullptr
					&& OtherReservedBytes + CookedBytes <= UBox3DAsyncSubsystem::MaxMeshGeometryBytes;
				if (bCookSucceeded)
				{
					Owner.ReservedGeometryBytes = OtherReservedBytes + CookedBytes;
					Owner.GeometryByteReservations[Command.GeometryId] = CookedBytes;
				}
				else
				{
					Owner.AcceptedGeometryIds.Remove(Command.GeometryId);
					Owner.AcceptedGeometryBounds.Remove(Command.GeometryId);
					Owner.GeometryByteReservations.Remove(Command.GeometryId);
					Owner.ReservedGeometryBytes = OtherReservedBytes;
				}
			}
			if (!bCookSucceeded)
			{
				if (MeshData != nullptr)
				{
					b3DestroyMesh(MeshData);
				}
				{
					FScopeLock Lock(&Owner.DiagnosticsMutex);
					++Owner.NativeCreationFailures;
				}
				Owner.PublishCreationResult(EBox3DAsyncCreationKind::MeshGeometry, Command.GeometryId, false,
					CookedDegenerateCount, DroppedTriangleCount);
				continue;
			}
			FGeometry Geometry;
			Geometry.NativeData = MeshData;
			Geometry.DegenerateTriangleCount = CookedDegenerateCount;
			Owner.WorkerGeometries.Add(Command.GeometryId, Geometry);
			Owner.PublishCreationResult(EBox3DAsyncCreationKind::MeshGeometry, Command.GeometryId, true, 0);
			continue;
		}

		if (Command.Type == ECommandType::ReleaseGeometry)
		{
			if (FGeometry* Geometry = Owner.WorkerGeometries.Find(Command.GeometryId))
			{
				Geometry->bReleasePending = true;
				if (Geometry->InstanceCount > 0)
				{
					continue;
				}
				b3DestroyMesh(Geometry->NativeData);
				Owner.WorkerGeometries.Remove(Command.GeometryId);
			}
			{
				FScopeLock Lock(&Owner.QueueMutex);
				if (uint64* Bytes = Owner.GeometryByteReservations.Find(Command.GeometryId))
				{
					Owner.ReservedGeometryBytes -= *Bytes;
					Owner.GeometryByteReservations.Remove(Command.GeometryId);
				}
			}
			Owner.PublishCreationResult(EBox3DAsyncCreationKind::MeshRelease, Command.GeometryId, true);
			continue;
		}

		if (Command.Type == ECommandType::CreateMeshInstance)
		{
			FGeometry* Geometry = Owner.WorkerGeometries.Find(Command.GeometryId);
			if (Geometry == nullptr || Geometry->bReleasePending || Geometry->NativeData == nullptr)
			{
				{
					FScopeLock Lock(&Owner.QueueMutex);
					Owner.AcceptedBodyIds.Remove(Command.BodyId);
				}
				{
					FScopeLock Lock(&Owner.DiagnosticsMutex);
					++Owner.NativeCreationFailures;
				}
				Owner.PublishCreationResult(EBox3DAsyncCreationKind::Body, Command.BodyId, false);
			continue;
			}
			b3BodyDef BodyDef = b3DefaultBodyDef();
			BodyDef.type = b3_staticBody;
			BodyDef.position = Box3D::ToB3Pos(Command.Transform.GetLocation());
			BodyDef.rotation = Box3D::ToB3(Command.Transform.GetRotation());
			BodyDef.userData = reinterpret_cast<void*>(static_cast<UPTRINT>(Command.BodyId));
			const b3BodyId NativeBody = b3CreateBody(Owner.WorldId, &BodyDef);
			b3ShapeDef ShapeDef = b3DefaultShapeDef();
			ShapeDef.filter.categoryBits = StaticCategory;
			ShapeDef.filter.maskBits = StaticCategory | DynamicCategory;
			const b3ShapeId ShapeId = b3Body_IsValid(NativeBody)
				? b3CreateMeshShape(NativeBody, &ShapeDef, Geometry->NativeData, Box3D::ToB3Dir(Command.Scale))
				: b3ShapeId{};
			if (!b3Body_IsValid(NativeBody) || !b3Shape_IsValid(ShapeId))
			{
				if (b3Body_IsValid(NativeBody))
				{
					b3DestroyBody(NativeBody);
				}
				{
					FScopeLock Lock(&Owner.QueueMutex);
					Owner.AcceptedBodyIds.Remove(Command.BodyId);
				}
				{
					FScopeLock Lock(&Owner.DiagnosticsMutex);
					++Owner.NativeCreationFailures;
				}
				Owner.PublishCreationResult(EBox3DAsyncCreationKind::Body, Command.BodyId, false);
				continue;
			}
			++Geometry->InstanceCount;
			Owner.WorkerBodies.Add(Command.BodyId, FBody(NativeBody, false, Command.GeometryId));
			Owner.PublishCreationResult(EBox3DAsyncCreationKind::Body, Command.BodyId, true);
			continue;
		}

		if (Command.Type == ECommandType::CreateAuthoredStaticBody)
		{
			b3BodyDef BodyDef = b3DefaultBodyDef();
			BodyDef.type = b3_staticBody;
			BodyDef.position = Box3D::ToB3Pos(Command.Transform.GetLocation());
			BodyDef.rotation = Box3D::ToB3(Command.Transform.GetRotation());
			BodyDef.userData = reinterpret_cast<void*>(static_cast<UPTRINT>(Command.BodyId));
			const b3BodyId NativeBody = b3CreateBody(Owner.WorldId, &BodyDef);
			bool bSucceeded = b3Body_IsValid(NativeBody);
			b3ShapeDef ShapeDef = b3DefaultShapeDef();
			ShapeDef.filter.categoryBits = StaticCategory;
			ShapeDef.filter.maskBits = StaticCategory | DynamicCategory;
			for (const Box3DAsyncPrivate::FCopiedStaticShape& Shape : Command.StaticShapes)
			{
				b3ShapeId ShapeId{};
				if (!bSucceeded)
				{
					break;
				}
				switch (Shape.Type)
				{
				case Box3DAsyncPrivate::EStaticShapeType::Sphere:
					ShapeId = b3CreateSphereShape(NativeBody, &ShapeDef, &Shape.Sphere);
					break;
				case Box3DAsyncPrivate::EStaticShapeType::Capsule:
					ShapeId = b3CreateCapsuleShape(NativeBody, &ShapeDef, &Shape.Capsule);
					break;
				case Box3DAsyncPrivate::EStaticShapeType::Hull:
					if (Shape.HullPoints.Num() >= 4)
					{
						b3HullData* Hull = b3CreateHull(Shape.HullPoints.GetData(), Shape.HullPoints.Num(), 64);
						if (Hull != nullptr)
						{
							ShapeId = b3CreateHullShape(NativeBody, &ShapeDef, Hull);
							b3DestroyHull(Hull);
						}
					}
					break;
				}
				bSucceeded = b3Shape_IsValid(ShapeId);
			}
			if (!bSucceeded)
			{
				if (b3Body_IsValid(NativeBody))
				{
					b3DestroyBody(NativeBody);
				}
				{
					FScopeLock Lock(&Owner.QueueMutex);
					Owner.AcceptedBodyIds.Remove(Command.BodyId);
				}
				{
					FScopeLock Lock(&Owner.DiagnosticsMutex);
					++Owner.NativeCreationFailures;
				}
				Owner.PublishCreationResult(EBox3DAsyncCreationKind::Body, Command.BodyId, false);
				continue;
			}
			Owner.WorkerBodies.Add(Command.BodyId, FBody(NativeBody, false));
			Owner.PublishCreationResult(EBox3DAsyncCreationKind::Body, Command.BodyId, true);
			continue;
		}

		b3BodyDef BodyDef = b3DefaultBodyDef();
		BodyDef.type = Command.Type == ECommandType::CreateBox ? b3_staticBody : b3_dynamicBody;
		BodyDef.position = Box3D::ToB3Pos(Command.Transform.GetLocation());
		BodyDef.rotation = Box3D::ToB3(Command.Transform.GetRotation());
		BodyDef.userData = reinterpret_cast<void*>(static_cast<UPTRINT>(Command.BodyId));
		if (Command.Type == ECommandType::CreateSphere)
		{
			BodyDef.linearVelocity = Box3D::ToB3(Command.InitialVelocity);
		}
		const b3BodyId NativeBody = b3CreateBody(Owner.WorldId, &BodyDef);
		if (!b3Body_IsValid(NativeBody))
		{
			{
				FScopeLock Lock(&Owner.DiagnosticsMutex);
				++Owner.NativeCreationFailures;
			}
			{
				FScopeLock Lock(&Owner.QueueMutex);
				Owner.AcceptedBodyIds.Remove(Command.BodyId);
			}
			Owner.PublishCreationResult(EBox3DAsyncCreationKind::Body, Command.BodyId, false);
			continue;
		}

		b3ShapeDef ShapeDef = b3DefaultShapeDef();
		ShapeDef.filter.categoryBits = Command.Type == ECommandType::CreateBox ? StaticCategory : DynamicCategory;
		ShapeDef.filter.maskBits = StaticCategory | DynamicCategory;
		if (Command.Type == ECommandType::CreateBox)
		{
			const FVector HalfExtentMeters = Command.Dimensions * Box3D::UEToMeters;
			const b3BoxHull Hull = b3MakeBoxHull(float(HalfExtentMeters.X), float(HalfExtentMeters.Y),
			                                     float(HalfExtentMeters.Z));
			const b3ShapeId ShapeId = b3CreateHullShape(NativeBody, &ShapeDef, &Hull.base);
			if (!b3Shape_IsValid(ShapeId))
			{
				b3DestroyBody(NativeBody);
				{
					FScopeLock Lock(&Owner.DiagnosticsMutex);
					++Owner.NativeCreationFailures;
				}
				{
					FScopeLock Lock(&Owner.QueueMutex);
					Owner.AcceptedBodyIds.Remove(Command.BodyId);
				}
				Owner.PublishCreationResult(EBox3DAsyncCreationKind::Body, Command.BodyId, false);
				continue;
			}
		}
		else
		{
			ShapeDef.baseMaterial.restitution = 0.5f;
			const b3Sphere Sphere{b3Vec3{0.0f, 0.0f, 0.0f}, float(Command.Dimensions.X * Box3D::UEToMeters)};
			const b3ShapeId ShapeId = b3CreateSphereShape(NativeBody, &ShapeDef, &Sphere);
			if (!b3Shape_IsValid(ShapeId))
			{
				b3DestroyBody(NativeBody);
				{
					FScopeLock Lock(&Owner.DiagnosticsMutex);
					++Owner.NativeCreationFailures;
				}
				{
					FScopeLock Lock(&Owner.QueueMutex);
					Owner.AcceptedBodyIds.Remove(Command.BodyId);
				}
				Owner.PublishCreationResult(EBox3DAsyncCreationKind::Body, Command.BodyId, false);
				continue;
			}
		}
		Owner.WorkerBodies.Add(Command.BodyId, FBody(NativeBody, Command.Type == ECommandType::CreateSphere));
		Owner.PublishCreationResult(EBox3DAsyncCreationKind::Body, Command.BodyId, true);
	}
}

void UBox3DAsyncSubsystem::FImpl::FWorker::ExecuteTraces()
{
	{
		FScopeLock Lock(&Owner.DiagnosticsMutex);
		if (Owner.CompletedSteps == 0)
		{
			return;
		}
	}
	TArray<FTraceRequest> LocalRequests;
	{
		FScopeLock Lock(&Owner.QueueMutex);
		Swap(LocalRequests, Owner.TraceRequests);
		Owner.InFlightTraces += LocalRequests.Num();
	}
	for (const FTraceRequest& Request : LocalRequests)
	{
		FBox3DAsyncTraceResult Output;
		Output.RequestId = Request.RequestId;
		{
			FScopeLock Lock(&Owner.DiagnosticsMutex);
			Output.SampledStep = Owner.CompletedSteps;
		}
		b3QueryFilter Filter = b3DefaultQueryFilter();
		Filter.categoryBits = DynamicCategory;
		Filter.maskBits = StaticCategory;
		const FVector Delta = Request.End - Request.Start;
		const b3RayResult Hit = b3World_CastRayClosest(Owner.WorldId, Box3D::ToB3Pos(Request.Start), Box3D::ToB3(Delta),
		                                               Filter);
		if (b3Shape_IsValid(Hit.shapeId))
		{
			Output.bHit = true;
			Output.BodyId = static_cast<FBox3DAsyncBodyId>(reinterpret_cast<UPTRINT>(
				b3Body_GetUserData(b3Shape_GetBody(Hit.shapeId))));
			Output.Fraction = Hit.fraction;
			Output.Point = Box3D::ToUEPos(Hit.point);
			Output.Normal = Box3D::ToUEDir(Hit.normal);
		}
		FScopeLock Lock(&Owner.QueueMutex);
		Owner.TraceResults.Add(Output);
		--Owner.InFlightTraces;
	}
}

void UBox3DAsyncSubsystem::FImpl::FWorker::StepAndPublish(double ScheduledTime, bool bAdvanceSimulation)
{
	if (bAdvanceSimulation)
	{
		b3World_Step(Owner.WorldId, Owner.FixedTimeStep, Owner.SubStepCount);
		{
			FScopeLock Lock(&Owner.DiagnosticsMutex);
			++Owner.CompletedSteps;
		}
	}
	{
		FScopeLock Lock(&Owner.DiagnosticsMutex);
		Owner.LatestScheduledTime = ScheduledTime;
		Owner.LatestCompletionTime = FPlatformTime::Seconds();
	}

	FSnapshot Snapshot;
	Snapshot.ScheduledTime = ScheduledTime;
	Snapshot.Poses.Reserve(Owner.WorkerBodies.Num());
	for (const TPair<FBox3DAsyncBodyId, FBody>& Pair : Owner.WorkerBodies)
	{
		if (!Pair.Value.bDynamic || !b3Body_IsValid(Pair.Value.NativeId))
		{
			continue;
		}
		const b3WorldTransform NativeTransform = b3Body_GetTransform(Pair.Value.NativeId);
		FBox3DAsyncPose& Pose = Snapshot.Poses.AddDefaulted_GetRef();
		Pose.BodyId = Pair.Key;
		Pose.Transform = FTransform(Box3D::ToUE(NativeTransform.q), Box3D::ToUEPos(NativeTransform.p),
		                            FVector::OneVector);
	}
	Snapshot.Poses.Sort([](const FBox3DAsyncPose& A, const FBox3DAsyncPose& B)
	{
		return A.BodyId < B.BodyId;
	});
	TSharedPtr<const FSnapshot, ESPMode::ThreadSafe> PublishedSnapshot = MakeShared<FSnapshot, ESPMode::ThreadSafe>(
		MoveTemp(Snapshot));
	{
		FScopeLock Lock(&Owner.SnapshotMutex);
		Owner.Snapshots.Add(MoveTemp(PublishedSnapshot));
		if (Owner.Snapshots.Num() > SnapshotCapacity)
		{
			Owner.Snapshots.RemoveAt(0, Owner.Snapshots.Num() - SnapshotCapacity, EAllowShrinking::No);
		}
	}
}
