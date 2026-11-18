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

	struct FQueryHitEntry
	{
		FBox3DAsyncLineHit Hit;
		uint64 NativeShape = 0;
	};

	struct FLineCollector
	{
		const FBox3DAsyncLineQuery* Query = nullptr;
		TArray<FQueryHitEntry> Hits;
		bool bAny = false;
		bool bTruncated = false;
	};

	bool QueryHitLess(const FQueryHitEntry& A, const FQueryHitEntry& B)
	{
		if (A.Hit.Fraction != B.Hit.Fraction) return A.Hit.Fraction < B.Hit.Fraction;
		if (A.Hit.BodyId != B.Hit.BodyId) return A.Hit.BodyId < B.Hit.BodyId;
		if (A.NativeShape != B.NativeShape) return A.NativeShape < B.NativeShape;
		if (A.Hit.ChildIndex != B.Hit.ChildIndex) return A.Hit.ChildIndex < B.Hit.ChildIndex;
		return A.Hit.TriangleIndex < B.Hit.TriangleIndex;
	}

	float CollectLineHit(b3ShapeId ShapeId, b3Pos Point, b3Vec3 Normal, float Fraction, uint64 Material,
	                     int Triangle, int Child, void* Context)
	{
		FLineCollector& Collector = *static_cast<FLineCollector*>(Context);
		const FBox3DAsyncLineQuery& Query = *Collector.Query;
		if (Collector.bAny && Query.Mode == EBox3DAsyncLineMode::Any) return 0.0f;
		if (!FMath::IsFinite(Fraction) || Fraction < 0.0f || Fraction > 1.0f) return 1.0f;
		const b3BodyId Body = b3Shape_GetBody(ShapeId);
		const b3BodyType Type = b3Body_GetType(Body);
		if ((Type == b3_staticBody && !Query.bStatic) || (Type == b3_dynamicBody && !Query.bDynamic)
			|| (Type != b3_staticBody && Type != b3_dynamicBody)) return 1.0f;
		const bool bSensor = b3Shape_IsSensor(ShapeId);
		if ((bSensor && !Query.bSensors) || (!bSensor && !Query.bSolids)) return 1.0f;
		const uint64 BodyId = static_cast<uint64>(reinterpret_cast<UPTRINT>(b3Body_GetUserData(Body)));
		if (Query.IgnoredBodies.Contains(BodyId)) return 1.0f;
		Collector.bAny = true;
		if (Query.Mode == EBox3DAsyncLineMode::Any) return 0.0f;
		FQueryHitEntry Entry;
		Entry.NativeShape = b3StoreShapeId(ShapeId);
		Entry.Hit.BodyId = BodyId;
		Entry.Hit.Fraction = Fraction;
		Entry.Hit.Point = Box3D::ToUEPos(Point);
		Entry.Hit.Normal = Box3D::ToUEDir(Normal);
		Entry.Hit.MaterialId = Material;
		Entry.Hit.TriangleIndex = Triangle;
		Entry.Hit.ChildIndex = Child;
		for (FQueryHitEntry& Existing : Collector.Hits)
		{
			if (Existing.NativeShape == Entry.NativeShape)
			{
				if (QueryHitLess(Entry, Existing)) Existing = Entry;
				return 1.0f;
			}
		}
		const int32 Limit = Query.Mode == EBox3DAsyncLineMode::Closest ? 1 : Query.MaxHits;
		if (Collector.Hits.Num() < Limit) Collector.Hits.Add(Entry);
		else
		{
			if (Query.Mode == EBox3DAsyncLineMode::Multi) Collector.bTruncated = true;
			int32 Worst = 0;
			for (int32 Index = 1; Index < Collector.Hits.Num(); ++Index)
				if (QueryHitLess(Collector.Hits[Worst], Collector.Hits[Index])) Worst = Index;
			if (QueryHitLess(Entry, Collector.Hits[Worst])) Collector.Hits[Worst] = Entry;
		}
		return 1.0f;
	}

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
		ExecuteTraces(NextDeadline);
		ExecuteQueries(NextDeadline);

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

bool UBox3DAsyncSubsystem::FImpl::FWorker::RegisterBodyShapes(b3BodyId BodyId, FBox3DAsyncBodyId PublicBody,
                                                               TArray<FBox3DAsyncShapeId>& OutShapeIds)
{
	const int32 Count = b3Body_GetShapeCount(BodyId);
	if (Count <= 0 || Count > 8192 || Owner.WorkerShapes.Num() > 8192 - Count
		|| Owner.NextShapeId == 0 || uint64(Count - 1) > MAX_uint64 - Owner.NextShapeId) return false;
	TArray<b3ShapeId> Shapes;
	Shapes.SetNumUninitialized(Count);
	if (b3Body_GetShapes(BodyId, Shapes.GetData(), Count) != Count) return false;
	OutShapeIds.Reserve(Count);
	for (const b3ShapeId Shape : Shapes)
	{
		FShapeIdentity Identity;
		Identity.Body = PublicBody;
		Identity.Shape = Owner.NextShapeId++;
		Owner.WorkerShapes.Add(b3StoreShapeId(Shape), Identity);
		OutShapeIds.Add(Identity.Shape);
	}
	return true;
}

void UBox3DAsyncSubsystem::FImpl::FWorker::ApplyCommands()
{
	TArray<FCommand> LocalCommands;
	{
		FScopeLock Lock(&Owner.QueueMutex);
		Swap(LocalCommands, Owner.Commands);
	}

	auto FinalizeBody = [this](const FCommand& Command, b3BodyId NativeBody, bool bDynamic,
	                           FBox3DAsyncGeometryId GeometryId = 0) -> bool
	{
		TArray<FBox3DAsyncShapeId> ShapeIds;
		if (!RegisterBodyShapes(NativeBody, Command.BodyId, ShapeIds))
		{
			// Preserve legacy body creation when the bounded identity table is full.
			// No shape IDs can be promised for this body, so invalidate the stream.
			FScopeLock Lock(&Owner.QueueMutex);
			Owner.bEventGap = true;
		}
		Owner.WorkerBodies.Add(Command.BodyId, FBody(NativeBody, bDynamic, GeometryId));
		Owner.PublishCreationResult(EBox3DAsyncCreationKind::Body, Command.BodyId, true, 0, 0, MoveTemp(ShapeIds));
		return true;
	};

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
				// Keep generation-qualified shape mappings until the next real step
				// has copied native destruction and sensor-merge events.
				Owner.RetiringBodies.Add(Command.BodyId);
				for (const auto& Pair : Owner.ActiveContacts)
					if (Pair.Value.BodyA == Command.BodyId || Pair.Value.BodyB == Command.BodyId)
						Owner.PendingRemovedBodies.Add(Command.BodyId);
				for (const FBox3DAsyncEvent& Pair : Owner.ActiveSensors)
					if (Pair.BodyA == Command.BodyId || Pair.BodyB == Command.BodyId)
						Owner.PendingRemovedBodies.Add(Command.BodyId);
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
				                           ? uint64(MeshData->byteCount)
				                           : 0;
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
				                          ? b3CreateMeshShape(NativeBody, &ShapeDef, Geometry->NativeData,
				                                              Box3D::ToB3Dir(Command.Scale))
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
			if (FinalizeBody(Command, NativeBody, false, Command.GeometryId)) ++Geometry->InstanceCount;
			continue;
		}

		if (Command.Type == ECommandType::CreateAuthoredStaticBody || Command.Type ==
			ECommandType::CreateAuthoredDynamicBody)
		{
			const bool bDynamic = Command.Type == ECommandType::CreateAuthoredDynamicBody;
			b3BodyDef BodyDef = b3DefaultBodyDef();
			BodyDef.type = bDynamic ? b3_dynamicBody : b3_staticBody;
			BodyDef.position = Box3D::ToB3Pos(Command.Transform.GetLocation());
			BodyDef.rotation = Box3D::ToB3(Command.Transform.GetRotation());
			BodyDef.linearVelocity = Box3D::ToB3(Command.InitialVelocity);
			BodyDef.userData = reinterpret_cast<void*>(static_cast<UPTRINT>(Command.BodyId));
			const b3BodyId NativeBody = b3CreateBody(Owner.WorldId, &BodyDef);
			bool bSucceeded = b3Body_IsValid(NativeBody);
			b3ShapeDef ShapeDef = b3DefaultShapeDef();
			ShapeDef.filter.categoryBits = bDynamic ? DynamicCategory : StaticCategory;
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
			FinalizeBody(Command, NativeBody, bDynamic);
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
		if (Command.bHasShapeOptions)
		{
			ShapeDef.filter.categoryBits = Command.ShapeOptions.CategoryBits;
			ShapeDef.filter.maskBits = Command.ShapeOptions.MaskBits;
			ShapeDef.isSensor = Command.ShapeOptions.bSensor;
			ShapeDef.enableSensorEvents = Command.ShapeOptions.bSensor || Command.ShapeOptions.bDetectableBySensors;
			ShapeDef.enableContactEvents = Command.ShapeOptions.bContactEvents;
			ShapeDef.enableHitEvents = Command.ShapeOptions.bImpactEvents;
			ShapeDef.density = Command.ShapeOptions.Density;
		}
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
		FinalizeBody(Command, NativeBody, Command.Type == ECommandType::CreateSphere);
	}
}

void UBox3DAsyncSubsystem::FImpl::FWorker::ExecuteTraces(double Deadline)
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
	for (int32 RequestIndex = 0; RequestIndex < LocalRequests.Num(); ++RequestIndex)
	{
		if (Owner.bStopRequested.Load() || FPlatformTime::Seconds() >= Deadline)
		{
			FScopeLock Lock(&Owner.QueueMutex);
			const int32 Remaining = LocalRequests.Num() - RequestIndex;
			Owner.TraceRequests.Insert(LocalRequests.GetData() + RequestIndex, Remaining, 0);
			Owner.InFlightTraces -= Remaining;
			break;
		}
		const FTraceRequest& Request = LocalRequests[RequestIndex];
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

void UBox3DAsyncSubsystem::FImpl::FWorker::ExecuteQueries(double Deadline)
{
	uint64 SampledStep = 0;
	{
		FScopeLock Lock(&Owner.DiagnosticsMutex);
		SampledStep = Owner.CompletedSteps;
	}
	if (SampledStep == 0) return;
	// Bound each service slice; a single native traversal cannot be preempted.
	for (int32 Serviced = 0; Serviced < 4 && !Owner.bStopRequested.Load()
		&& FPlatformTime::Seconds() < Deadline; ++Serviced)
	{
		uint64 RequestId = 0;
		FBox3DAsyncLineQuery Query;
		{
			FScopeLock Lock(&Owner.QueueMutex);
			for (auto& Pair : Owner.Queries)
			{
				if (!Pair.Value.bReady && !Pair.Value.bExecuting && !Pair.Value.bCancelled
					&& (RequestId == 0 || Pair.Key < RequestId)) RequestId = Pair.Key;
			}
			if (RequestId == 0) return;
			FQueryRecord& Record = Owner.Queries.FindChecked(RequestId);
			Record.bExecuting = true;
			Query = Record.Query;
		}
		FLineCollector Collector;
		Collector.Query = &Query;
		b3QueryFilter Filter = b3DefaultQueryFilter();
		Filter.categoryBits = Query.CategoryBits;
		Filter.maskBits = Query.MaskBits;
		b3World_CastRay(Owner.WorldId, Box3D::ToB3Pos(Query.Start), Box3D::ToB3(Query.End - Query.Start),
		                Filter, CollectLineHit, &Collector);
		Collector.Hits.Sort(QueryHitLess);
		FScopeLock Lock(&Owner.QueueMutex);
		FQueryRecord* Record = Owner.Queries.Find(RequestId);
		if (!Record) continue;
		if (Record->bAbandoned) { Owner.Queries.Remove(RequestId); continue; }
		Record->bExecuting = false;
		if (Record->bCancelled) Record->Result.Status = EBox3DAsyncQueryStatus::Cancelled;
		else
		{
			Record->Result.SampledStep = SampledStep;
			Record->Result.Status = Collector.bTruncated ? EBox3DAsyncQueryStatus::Truncated
				: Collector.bAny ? EBox3DAsyncQueryStatus::Hit : EBox3DAsyncQueryStatus::Miss;
			for (const FQueryHitEntry& Entry : Collector.Hits) Record->Result.Hits.Add(Entry.Hit);
		}
		Record->bReady = true;
	}
}

void UBox3DAsyncSubsystem::FImpl::FWorker::CollectEvents(uint64 Step)
{
	{
		FScopeLock Lock(&Owner.QueueMutex);
		if (Owner.bEventGap) return;
	}
	const b3ContactEvents Contacts = b3World_GetContactEvents(Owner.WorldId);
	const b3SensorEvents Sensors = b3World_GetSensorEvents(Owner.WorldId);
	const int64 Count = int64(Contacts.beginCount) + Contacts.endCount + Contacts.hitCount
		+ Sensors.beginCount + Sensors.endCount;
	if (Count < 0 || Count > 1024)
	{
		FScopeLock Lock(&Owner.QueueMutex);
		Owner.bEventGap = true;
		return;
	}
	FBox3DAsyncEventBatch Batch;
	Batch.World = Owner.QueryWorld;
	Batch.Step = Step;
	Batch.Events.Reserve(int32(Count));
	bool bLoss = false;
	auto Emit = [&Batch, &bLoss](const FBox3DAsyncEvent& Event)
	{
		if (Batch.Events.Num() >= 1024) bLoss = true;
		else Batch.Events.Add(Event);
	};
	auto MakeEvent = [this, &bLoss](EBox3DAsyncEventKind Kind, b3ShapeId NativeA, b3ShapeId NativeB,
	                                 b3ContactId Contact, bool& bSwapped, FBox3DAsyncEvent& Event) -> bool
	{
		const FShapeIdentity* A = Owner.WorkerShapes.Find(b3StoreShapeId(NativeA));
		const FShapeIdentity* B = Owner.WorkerShapes.Find(b3StoreShapeId(NativeB));
		if (!A || !B) { bLoss = true; return false; }
		const bool bSensor = Kind == EBox3DAsyncEventKind::SensorEnter || Kind == EBox3DAsyncEventKind::SensorExit;
		bSwapped = !bSensor && A->Shape > B->Shape;
		if (bSwapped) Swap(A, B);
		Event.Kind = Kind;
		Event.BodyA = A->Body; Event.ShapeA = A->Shape;
		Event.BodyB = B->Body; Event.ShapeB = B->Shape;
		uint32 Parts[3];
		b3StoreContactId(Contact, Parts);
		Event.ContactIndex = Parts[0]; Event.ContactWorld = Parts[1]; Event.ContactGeneration = Parts[2];
		return true;
	};
	auto ContactKey = [](b3ContactId Id) -> uint64
	{
		uint32 Parts[3]; b3StoreContactId(Id, Parts);
		return (uint64(Parts[0]) << 32) | Parts[2];
	};
	auto WasRemoved = [this](const FBox3DAsyncEvent& Event)
	{
		return Owner.PendingRemovedBodies.Contains(Event.BodyA) || Owner.PendingRemovedBodies.Contains(Event.BodyB);
	};
	// The native arrays do not establish ordering between opposite transitions
	// in the same step. Do not publish an invented final membership.
	for (int32 Begin = 0; Begin < Contacts.beginCount; ++Begin)
		for (int32 End = 0; End < Contacts.endCount; ++End)
			if (ContactKey(Contacts.beginEvents[Begin].contactId) == ContactKey(Contacts.endEvents[End].contactId)) bLoss = true;
	for (int32 Begin = 0; Begin < Sensors.beginCount; ++Begin)
		for (int32 End = 0; End < Sensors.endCount; ++End)
			if (b3StoreShapeId(Sensors.beginEvents[Begin].sensorShapeId) == b3StoreShapeId(Sensors.endEvents[End].sensorShapeId)
				&& b3StoreShapeId(Sensors.beginEvents[Begin].visitorShapeId) == b3StoreShapeId(Sensors.endEvents[End].visitorShapeId)) bLoss = true;
	bool bSwapped = false;
	for (int32 Index = 0; Index < Contacts.beginCount; ++Index)
	{
		const auto& Native = Contacts.beginEvents[Index];
		const uint64 Key = ContactKey(Native.contactId);
		if (Owner.ActiveContacts.Contains(Key)) continue;
		FBox3DAsyncEvent Event;
		if (!MakeEvent(EBox3DAsyncEventKind::ContactBegin, Native.shapeIdA, Native.shapeIdB, Native.contactId, bSwapped, Event)) continue;
		if (Owner.ActiveContacts.Num() >= 8192) { bLoss = true; continue; }
		Owner.ActiveContacts.Add(Key, Event);
		Emit(Event);
	}
	for (int32 Index = 0; Index < Contacts.endCount; ++Index)
	{
		const uint64 Key = ContactKey(Contacts.endEvents[Index].contactId);
		FBox3DAsyncEvent* Active = Owner.ActiveContacts.Find(Key);
		if (!Active) continue; // Includes late native ends after a synthetic removal end.
		FBox3DAsyncEvent Event = *Active;
		Event.Kind = EBox3DAsyncEventKind::ContactEnd;
		if (WasRemoved(Event)) Event.EndCause = EBox3DAsyncEventEndCause::Removed;
		Owner.ActiveContacts.Remove(Key);
		Emit(Event);
	}
	for (int32 Index = 0; Index < Contacts.hitCount; ++Index)
	{
		const auto& Native = Contacts.hitEvents[Index];
		FBox3DAsyncEvent Event;
		if (!MakeEvent(EBox3DAsyncEventKind::Impact, Native.shapeIdA, Native.shapeIdB, Native.contactId, bSwapped, Event)) continue;
		Event.Point = Box3D::ToUEPos(Native.point);
		Event.Normal = Box3D::ToUEDir(Native.normal) * (bSwapped ? -1.0 : 1.0);
		Event.ApproachSpeed = Native.approachSpeed * Box3D::MetersToUE;
		Event.MaterialA = bSwapped ? Native.userMaterialIdB : Native.userMaterialIdA;
		Event.MaterialB = bSwapped ? Native.userMaterialIdA : Native.userMaterialIdB;
		Emit(Event);
	}
	for (int32 Index = 0; Index < Sensors.beginCount; ++Index)
	{
		const auto& Native = Sensors.beginEvents[Index];
		FBox3DAsyncEvent Event;
		if (!MakeEvent(EBox3DAsyncEventKind::SensorEnter, Native.sensorShapeId, Native.visitorShapeId,
		               b3ContactId{}, bSwapped, Event)) continue;
		if (Owner.ActiveSensors.ContainsByPredicate([&Event](const FBox3DAsyncEvent& Active)
		    { return Active.ShapeA == Event.ShapeA && Active.ShapeB == Event.ShapeB; })) continue;
		if (Owner.ActiveSensors.Num() >= 8192) { bLoss = true; continue; }
		Owner.ActiveSensors.Add(Event);
		Emit(Event);
	}
	for (int32 Index = 0; Index < Sensors.endCount; ++Index)
	{
		const auto& Native = Sensors.endEvents[Index];
		FBox3DAsyncEvent Event;
		if (!MakeEvent(EBox3DAsyncEventKind::SensorExit, Native.sensorShapeId, Native.visitorShapeId,
		               b3ContactId{}, bSwapped, Event)) continue;
		const int32 Existing = Owner.ActiveSensors.IndexOfByPredicate([&Event](const FBox3DAsyncEvent& Active)
		    { return Active.ShapeA == Event.ShapeA && Active.ShapeB == Event.ShapeB; });
		if (Existing == INDEX_NONE) continue;
		if (WasRemoved(Event)) Event.EndCause = EBox3DAsyncEventEndCause::Removed;
		Owner.ActiveSensors.RemoveAtSwap(Existing);
		Emit(Event);
	}
	for (auto It = Owner.ActiveContacts.CreateIterator(); It; ++It)
	{
		if (!WasRemoved(It.Value())) continue;
		FBox3DAsyncEvent Event = It.Value();
		Event.Kind = EBox3DAsyncEventKind::ContactEnd;
		Event.EndCause = EBox3DAsyncEventEndCause::Removed;
		Emit(Event);
		It.RemoveCurrent();
	}
	for (int32 Index = Owner.ActiveSensors.Num() - 1; Index >= 0; --Index)
	{
		if (!WasRemoved(Owner.ActiveSensors[Index])) continue;
		FBox3DAsyncEvent Event = Owner.ActiveSensors[Index];
		Event.Kind = EBox3DAsyncEventKind::SensorExit;
		Event.EndCause = EBox3DAsyncEventEndCause::Removed;
		Emit(Event);
		Owner.ActiveSensors.RemoveAtSwap(Index);
	}
	Owner.PendingRemovedBodies.Reset();
	if (Batch.Events.IsEmpty() && !bLoss) return;
	if (Owner.NextEventSequence == 0) bLoss = true;
	Batch.Sequence = Owner.NextEventSequence++;
	Batch.Events.Sort([](const FBox3DAsyncEvent& A, const FBox3DAsyncEvent& B)
	{
		if (A.Kind != B.Kind) return uint8(A.Kind) < uint8(B.Kind);
		if (A.ShapeA != B.ShapeA) return A.ShapeA < B.ShapeA;
		if (A.ShapeB != B.ShapeB) return A.ShapeB < B.ShapeB;
		if (A.ContactWorld != B.ContactWorld) return A.ContactWorld < B.ContactWorld;
		if (A.ContactIndex != B.ContactIndex) return A.ContactIndex < B.ContactIndex;
		return A.ContactGeneration < B.ContactGeneration;
	});
	FScopeLock Lock(&Owner.QueueMutex);
	if (bLoss || Owner.EventBatches.Num() >= 128) Owner.bEventGap = true;
	else Owner.EventBatches.Add(MoveTemp(Batch));
}

void UBox3DAsyncSubsystem::FImpl::FWorker::StepAndPublish(double ScheduledTime, bool bAdvanceSimulation)
{
	if (bAdvanceSimulation)
	{
		b3World_Step(Owner.WorldId, Owner.FixedTimeStep, Owner.SubStepCount);
		uint64 CompletedStep;
		{
			FScopeLock Lock(&Owner.DiagnosticsMutex);
			CompletedStep = ++Owner.CompletedSteps;
		}
		CollectEvents(CompletedStep);
		// Native end buffers rotate once per step; sensor overlap history is
		// merged during that step. Any unexpected later unresolved end gaps.
		if (!Owner.RetiringBodies.IsEmpty())
		{
			for (auto It = Owner.WorkerShapes.CreateIterator(); It; ++It)
				if (Owner.RetiringBodies.Contains(It.Value().Body)) It.RemoveCurrent();
			Owner.RetiringBodies.Reset();
		}
		Box3DAsyncStats::FWorldSnapshot Stats;
		Stats.Profile = b3World_GetProfile(Owner.WorldId);
		Stats.Counters = b3World_GetCounters(Owner.WorldId);
		Stats.AwakeBodies = b3World_GetAwakeBodyCount(Owner.WorldId);
		{
			FScopeLock Lock(&Owner.DiagnosticsMutex);
			Stats.CompletedSteps = Owner.CompletedSteps;
			Stats.DeadlineMisses = Owner.DeadlineMisses;
			Stats.ClockRebases = Owner.ClockRebases;
		}
		{
			FScopeLock Lock(&Owner.QueueMutex);
			Stats.QueuedCommands = Owner.Commands.Num();
			Stats.OutstandingTraces = Owner.TraceRequests.Num() + Owner.InFlightTraces + Owner.TraceResults.Num();
		}
		Box3DAsyncStats::Publish(&Owner, Stats);
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
