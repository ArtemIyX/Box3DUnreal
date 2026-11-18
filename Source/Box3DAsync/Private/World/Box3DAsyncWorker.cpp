#include "Box3DAsyncSubsystemImpl.h"
#include "Box3DAsyncEventOrdering.h"

#include "HAL/PlatformTime.h"
#include "Misc/ScopeExit.h"
#include "Simulation/Box3DConversion.h"
#include "box3d/collision.h"
#include <cmath>
#include <limits>

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
		bool bFailed = false;
		TFunction<FBox3DAsyncShapeId(uint64)> LookupShape;
	};

	bool QueryHitLess(const FQueryHitEntry& A, const FQueryHitEntry& B)
	{
		if (A.Hit.Fraction != B.Hit.Fraction) return A.Hit.Fraction < B.Hit.Fraction;
		if (A.Hit.BodyId != B.Hit.BodyId) return A.Hit.BodyId < B.Hit.BodyId;
		if (A.Hit.ShapeId != B.Hit.ShapeId) return A.Hit.ShapeId < B.Hit.ShapeId;
		if (A.Hit.ChildIndex != B.Hit.ChildIndex) return A.Hit.ChildIndex < B.Hit.ChildIndex;
		return A.Hit.TriangleIndex < B.Hit.TriangleIndex;
	}

	bool SelectQueryShape(FLineCollector& Collector, b3ShapeId ShapeId, FQueryHitEntry& Entry)
	{
		const FBox3DAsyncLineQuery& Query = *Collector.Query;
		const b3BodyId Body = b3Shape_GetBody(ShapeId);
		const b3BodyType Type = b3Body_GetType(Body);
		if ((Type == b3_staticBody && !Query.bStatic) || (Type == b3_dynamicBody && !Query.bDynamic)
			|| (Type != b3_staticBody && Type != b3_dynamicBody)) return false;
		const bool bSensor = b3Shape_IsSensor(ShapeId);
		if ((bSensor && !Query.bSensors) || (!bSensor && !Query.bSolids)) return false;
		const uint64 BodyId = static_cast<uint64>(reinterpret_cast<UPTRINT>(b3Body_GetUserData(Body)));
		if (Query.IgnoredBodies.Contains(BodyId)) return false;
		if (Query.IgnoredBodyHandles.ContainsByPredicate([BodyId](const FBox3DAsyncBodyHandle& H) { return H.Id == BodyId; })) return false;
		Entry.NativeShape = b3StoreShapeId(ShapeId);
		Entry.Hit.BodyId = BodyId;
		Entry.Hit.ShapeId = Collector.LookupShape(Entry.NativeShape);
		Entry.Hit.ShapeType = static_cast<int32>(b3Shape_GetType(ShapeId));
		if (Entry.Hit.ShapeId == 0 && Query.Mode != EBox3DAsyncLineMode::Any) Collector.bFailed = true;
		return true;
	}

	void AddQueryHit(FLineCollector& Collector, const FQueryHitEntry& Entry)
	{
		const FBox3DAsyncLineQuery& Query = *Collector.Query;
		for (FQueryHitEntry& Existing : Collector.Hits)
		{
			if (Existing.NativeShape == Entry.NativeShape)
			{
				if (QueryHitLess(Entry, Existing)) Existing = Entry;
				return;
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
	}

	float CollectLineHit(b3ShapeId ShapeId, b3Pos Point, b3Vec3 Normal, float Fraction, uint64 Material,
	                     int Triangle, int Child, void* Context)
	{
		FLineCollector& Collector = *static_cast<FLineCollector*>(Context);
		const FBox3DAsyncLineQuery& Query = *Collector.Query;
		if (Collector.bAny && Query.Mode == EBox3DAsyncLineMode::Any) return 0.0f;
		if (!FMath::IsFinite(Fraction) || Fraction < 0.0f || Fraction > 1.0f) return 1.0f;
		FQueryHitEntry Entry;
		if (!SelectQueryShape(Collector, ShapeId, Entry)) return 1.0f;
		Collector.bAny = true;
		if (Query.Mode == EBox3DAsyncLineMode::Any) return 0.0f;
		Entry.Hit.Fraction = Fraction;
		Entry.Hit.Distance = float(FVector::Distance(Query.Start, Query.End) * Fraction);
		Entry.Hit.Point = Box3D::ToUEPos(Point);
		Entry.Hit.Normal = Box3D::ToUEDir(Normal);
		Entry.Hit.MaterialId = Material;
		const b3ShapeType ShapeType = b3Shape_GetType(ShapeId);
		// Primitive native outputs default these fields to zero. A compound may
		// contain either primitive or mesh children, and the public API does not
		// expose the child type, so it cannot promise a triangle index there.
		Entry.Hit.TriangleIndex = ShapeType == b3_meshShape || ShapeType == b3_heightShape ? Triangle : INDEX_NONE;
		Entry.Hit.ChildIndex = ShapeType == b3_compoundShape ? Child : INDEX_NONE;
		AddQueryHit(Collector, Entry);
		return 1.0f;
	}

	bool CollectOverlap(b3ShapeId ShapeId, void* Context)
	{
		FLineCollector& Collector = *static_cast<FLineCollector*>(Context);
		if (Collector.bAny && Collector.Query->Mode == EBox3DAsyncLineMode::Any) return false;
		FQueryHitEntry Entry;
		if (!SelectQueryShape(Collector, ShapeId, Entry)) return true;
		Collector.bAny = true;
		if (Collector.Query->Mode == EBox3DAsyncLineMode::Any) return false;
		AddQueryHit(Collector, Entry);
		return true;
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
				++Owner.ClockEpoch;
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
	                                                               int32 Reserved, TArray<FBox3DAsyncShapeId>& OutShapeIds)
{
	const int32 Count = b3Body_GetShapeCount(BodyId);
	if (Count <= 0 || Count > 8192
		|| Owner.NextShapeId == 0 || uint64(Count - 1) > MAX_uint64 - Owner.NextShapeId) return false;
	{
		FScopeLock Lock(&Owner.QueueMutex);
		if (Count > 8192 - Owner.LiveShapeIdentities - Owner.ReservedShapeIdentities + Reserved) return false;
		Owner.LiveShapeIdentities += Count;
	}
	TArray<b3ShapeId> Shapes;
	Shapes.SetNumUninitialized(Count);
	if (b3Body_GetShapes(BodyId, Shapes.GetData(), Count) != Count)
	{
		FScopeLock Lock(&Owner.QueueMutex);
		Owner.LiveShapeIdentities -= Count;
		return false;
	}
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
	                           FBox3DAsyncGeometryId GeometryId = 0,
	                           const TMap<uint64, uint64>* NativeShapeKeys = nullptr) -> bool
	{
		TArray<FBox3DAsyncShapeId> ShapeIds;
		if (!RegisterBodyShapes(NativeBody, Command.BodyId, Command.ReservedShapeIdentities, ShapeIds))
		{
			if (Command.ReservedShapeIdentities > 0)
			{
				b3DestroyBody(NativeBody);
				{ FScopeLock Lock(&Owner.QueueMutex); Owner.AcceptedBodyIds.Remove(Command.BodyId); }
				{ FScopeLock Lock(&Owner.DiagnosticsMutex); ++Owner.NativeCreationFailures; }
				Owner.PublishCreationResult(EBox3DAsyncCreationKind::Body, Command.BodyId, false);
				return false;
			}
			// Preserve legacy body creation when the bounded identity table is full.
			// No shape IDs can be promised for this body, so invalidate the stream.
			FScopeLock Lock(&Owner.QueueMutex);
			Owner.bEventGap = true;
			Owner.bEventTrackingValid = false;
			Owner.bEventBaselineReady = false;
		}
		Owner.WorkerBodies.Add(Command.BodyId, FBody(NativeBody, bDynamic, GeometryId));
		TArray<uint64> Keys;
		if (NativeShapeKeys != nullptr)
		{
			TArray<b3ShapeId> Shapes;
			Shapes.SetNumUninitialized(ShapeIds.Num());
			const int32 Count = b3Body_GetShapes(NativeBody, Shapes.GetData(), Shapes.Num());
			check(Count == ShapeIds.Num());
			for (const b3ShapeId Shape : Shapes) Keys.Add(NativeShapeKeys->FindChecked(b3StoreShapeId(Shape)));
		}
		Owner.PublishCreationResult(EBox3DAsyncCreationKind::Body, Command.BodyId, true, 0, 0, MoveTemp(ShapeIds), MoveTemp(Keys));
		return true;
	};

	for (FCommand& Command : LocalCommands)
	{
		ON_SCOPE_EXIT
		{
			if (Command.ReservedShapeIdentities > 0)
			{
				FScopeLock Lock(&Owner.QueueMutex);
				Owner.ReservedShapeIdentities -= Command.ReservedShapeIdentities;
			}
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

		if (Command.Type == ECommandType::CreateActorlessBody)
		{
			b3BodyDef BodyDef = b3DefaultBodyDef();
			BodyDef.type = Command.bDynamic ? b3_dynamicBody : b3_staticBody;
			BodyDef.position = Box3D::ToB3Pos(Command.Transform.GetLocation());
			BodyDef.rotation = Box3D::ToB3(Command.Transform.GetRotation());
			BodyDef.linearVelocity = Box3D::ToB3(Command.InitialVelocity);
			BodyDef.gravityScale = Command.GravityScale;
			BodyDef.userData = reinterpret_cast<void*>(static_cast<UPTRINT>(Command.BodyId));
			const b3BodyId NativeBody = b3CreateBody(Owner.WorldId, &BodyDef);
			bool bSucceeded = b3Body_IsValid(NativeBody);
			TMap<uint64, uint64> NativeShapeKeys;
			for (const Box3DAsyncPrivate::FCopiedBodyShape& Shape : Command.BodyShapes)
			{
				if (!bSucceeded) break;
				b3ShapeDef ShapeDef = b3DefaultShapeDef();
				ShapeDef.filter.categoryBits = Shape.Options.CategoryBits;
				ShapeDef.filter.maskBits = Shape.Options.MaskBits;
				ShapeDef.isSensor = Shape.Options.bSensor;
				ShapeDef.enableSensorEvents = Shape.Options.bSensor || Shape.Options.bDetectableBySensors;
				ShapeDef.enableContactEvents = Shape.Options.bContactEvents;
				ShapeDef.enableHitEvents = Shape.Options.bImpactEvents;
				ShapeDef.density = Shape.Options.Density;
				ShapeDef.baseMaterial.userMaterialId = Shape.Options.MaterialId;
				b3ShapeId ShapeId{};
				switch (Shape.Type)
				{
				case Box3DAsyncPrivate::EStaticShapeType::Sphere:
					ShapeId = b3CreateSphereShape(NativeBody, &ShapeDef, &Shape.Sphere); break;
				case Box3DAsyncPrivate::EStaticShapeType::Capsule:
					ShapeId = b3CreateCapsuleShape(NativeBody, &ShapeDef, &Shape.Capsule); break;
				case Box3DAsyncPrivate::EStaticShapeType::Hull:
					if (b3HullData* Hull = b3CreateHull(Shape.HullPoints.GetData(), Shape.HullPoints.Num(), 64))
					{
						ShapeId = b3CreateHullShape(NativeBody, &ShapeDef, Hull);
						b3DestroyHull(Hull);
					}
					break;
				}
				bSucceeded = b3Shape_IsValid(ShapeId);
				if (bSucceeded) NativeShapeKeys.Add(b3StoreShapeId(ShapeId), Shape.SubshapeKey);
			}
			if (!bSucceeded)
			{
				if (b3Body_IsValid(NativeBody)) b3DestroyBody(NativeBody);
				{ FScopeLock Lock(&Owner.QueueMutex); Owner.AcceptedBodyIds.Remove(Command.BodyId); }
				{ FScopeLock Lock(&Owner.DiagnosticsMutex); ++Owner.NativeCreationFailures; }
				Owner.PublishCreationResult(EBox3DAsyncCreationKind::Body, Command.BodyId, false);
				continue;
			}
			FinalizeBody(Command, NativeBody, Command.bDynamic, 0, &NativeShapeKeys);
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
			if (Command.bHasShapeOptions)
			{
				ShapeDef.filter.categoryBits = Command.ShapeOptions.CategoryBits;
				ShapeDef.filter.maskBits = Command.ShapeOptions.MaskBits;
				ShapeDef.isSensor = Command.ShapeOptions.bSensor;
				ShapeDef.enableSensorEvents = Command.ShapeOptions.bSensor || Command.ShapeOptions.bDetectableBySensors;
				ShapeDef.enableContactEvents = Command.ShapeOptions.bContactEvents;
				ShapeDef.enableHitEvents = Command.ShapeOptions.bImpactEvents;
				ShapeDef.density = Command.ShapeOptions.Density;
				ShapeDef.baseMaterial.userMaterialId = Command.ShapeOptions.MaterialId;
			}
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
			ShapeDef.baseMaterial.userMaterialId = Command.ShapeOptions.MaterialId;
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
	FBox3DAsyncSampleStamp Sample;
	{
		FScopeLock Lock(&Owner.DiagnosticsMutex);
		Sample = Owner.CompletedSample;
	}
	if (Sample.Step == 0) return;
	// Bound each service slice; a single native traversal cannot be preempted.
	for (int32 Serviced = 0; Serviced < 4 && !Owner.bStopRequested.Load()
		&& FPlatformTime::Seconds() < Deadline; ++Serviced)
	{
#if WITH_DEV_AUTOMATION_TESTS
		Owner.WaitAtQueryTestGate(1);
		if (Owner.bStopRequested.Load()) return;
#endif
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
#if WITH_DEV_AUTOMATION_TESTS
		Owner.WaitAtQueryTestGate(2);
		if (Owner.bStopRequested.Load()) return;
#endif
		FLineCollector Collector;
		Collector.Query = &Query;
		Collector.LookupShape = [this](uint64 NativeId)
		{
			const FShapeIdentity* Identity = Owner.WorkerShapes.Find(NativeId);
			return Identity != nullptr ? Identity->Shape : FBox3DAsyncShapeId(0);
		};
		b3QueryFilter Filter = b3DefaultQueryFilter();
		Filter.categoryBits = Query.CategoryBits;
		Filter.maskBits = Query.MaskBits;
		const double QueryStartSeconds = FPlatformTime::Seconds();
		if (Query.Kind == EBox3DAsyncQueryKind::Line)
		{
			b3World_CastRay(Owner.WorldId, Box3D::ToB3Pos(Query.Start), Box3D::ToB3(Query.End - Query.Start),
			                Filter, CollectLineHit, &Collector);
		}
		else if (Query.Kind == EBox3DAsyncQueryKind::AabbCandidates)
		{
			// Native broad-phase boxes are float even in double-position builds. Round
			// bounds outward so narrowing does not discard edge candidates.
			const auto Lower = [](double Value)
			{
				const float F = float(Value * Box3D::UEToMeters);
				return double(F) > Value * Box3D::UEToMeters ? std::nextafter(F, -std::numeric_limits<float>::infinity()) : F;
			};
			const auto Upper = [](double Value)
			{
				const float F = float(Value * Box3D::UEToMeters);
				return double(F) < Value * Box3D::UEToMeters ? std::nextafter(F, std::numeric_limits<float>::infinity()) : F;
			};
			const b3AABB Bounds{{Lower(Query.Start.X), Lower(Query.Start.Y), Lower(Query.Start.Z)},
			                    {Upper(Query.End.X), Upper(Query.End.Y), Upper(Query.End.Z)}};
			b3World_OverlapAABB(Owner.WorldId, Bounds, Filter, CollectOverlap, &Collector);
		}
		else
		{
			TArray<b3Vec3, TInlineAllocator<B3_MAX_SHAPE_CAST_POINTS>> Points;
			float Radius = 0.0f;
			auto AddPoint = [&Points, &Query](const FVector& Point)
			{
				Points.Add(Box3D::ToB3(Query.Rotation.RotateVector(Point)));
			};
			switch (Query.Shape)
			{
			case EBox3DAsyncQueryShape::Sphere:
				AddPoint(FVector::ZeroVector);
				Radius = float(double(Query.Radius) * Box3D::UEToMeters);
				break;
			case EBox3DAsyncQueryShape::Capsule:
			{
				const double Segment = Query.HalfHeight - Query.Radius;
				AddPoint(FVector(0, 0, -Segment));
				if (Segment > 0.0) AddPoint(FVector(0, 0, Segment));
				Radius = float(double(Query.Radius) * Box3D::UEToMeters);
				break;
			}
			case EBox3DAsyncQueryShape::Box:
				for (int32 Corner = 0; Corner < 8; ++Corner)
					AddPoint(FVector((Corner & 1 ? 1.0 : -1.0) * Query.HalfExtents.X,
					                 (Corner & 2 ? 1.0 : -1.0) * Query.HalfExtents.Y,
					                 (Corner & 4 ? 1.0 : -1.0) * Query.HalfExtents.Z));
				break;
			case EBox3DAsyncQueryShape::Convex:
				for (const FVector& Point : Query.ConvexPoints) AddPoint(Point);
				break;
			}
			const b3ShapeProxy Proxy{Points.GetData(), Points.Num(), Radius};
			if (Query.Kind == EBox3DAsyncQueryKind::Sweep)
				b3World_CastShape(Owner.WorldId, Box3D::ToB3Pos(Query.Start), &Proxy,
				                  Box3D::ToB3(Query.End - Query.Start), Filter, CollectLineHit, &Collector);
			else
				b3World_OverlapShape(Owner.WorldId, Box3D::ToB3Pos(Query.Start), &Proxy, Filter, CollectOverlap, &Collector);
		}
		const double QueryExecutionSeconds = FMath::Max(0.0, FPlatformTime::Seconds() - QueryStartSeconds);
		Collector.Hits.Sort(QueryHitLess);
		FScopeLock Lock(&Owner.QueueMutex);
		++Owner.ExchangeDiagnostics.ExecutedQueries;
		Owner.ExchangeDiagnostics.TotalQueryExecutionSeconds += QueryExecutionSeconds;
		Owner.ExchangeDiagnostics.MaxQueryExecutionSeconds = FMath::Max(Owner.ExchangeDiagnostics.MaxQueryExecutionSeconds, QueryExecutionSeconds);
		FQueryRecord* Record = Owner.Queries.Find(RequestId);
		if (!Record) continue;
		if (Record->bAbandoned) { Owner.Queries.Remove(RequestId); continue; }
		Record->bExecuting = false;
		if (Record->bCancelled) Record->Result.Status = EBox3DAsyncQueryStatus::Cancelled;
		else
		{
			Record->Result.SampledStep = Sample.Step;
			Record->Result.Sample = Sample;
			Record->Result.Status = Collector.bFailed ? EBox3DAsyncQueryStatus::Failed
				: Collector.bTruncated ? EBox3DAsyncQueryStatus::Truncated
				: Collector.bAny ? EBox3DAsyncQueryStatus::Hit : EBox3DAsyncQueryStatus::Miss;
			Record->Result.Failure = Collector.bFailed ? EBox3DAsyncQueryFailure::IdentityUnavailable
				: EBox3DAsyncQueryFailure::None;
			if (Record->Result.Status == EBox3DAsyncQueryStatus::Truncated) ++Owner.ExchangeDiagnostics.TruncatedQueries;
			if (!Collector.bFailed)
				for (const FQueryHitEntry& Entry : Collector.Hits)
				{
					FBox3DAsyncLineHit& Hit = Record->Result.Hits.Add_GetRef(Entry.Hit);
					Hit.BodyHandle = {Owner.QueryWorld, Hit.BodyId};
					Hit.ShapeHandle = {Owner.QueryWorld, Hit.ShapeId};
				}
		}
		Record->bReady = true;
	}
}

void UBox3DAsyncSubsystem::FImpl::FWorker::CollectEvents(uint64 Step)
{
	// Keep tracking after a legacy or transport gap. Publication loss must not
	// prevent a later subscriber from obtaining a complete worker baseline.
	auto CaptureRelationships = [this]()
	{
		TArray<FBox3DAsyncEvent> Out;
		TSet<Box3DAsyncPrivate::FEventPairKey> Seen;
		for (const auto& Pair : Owner.ActiveContacts)
		{
			FBox3DAsyncEvent E = Pair.Value;
			const auto Key = Box3DAsyncPrivate::ShapePair(E);
			if (Seen.Contains(Key)) continue;
			Seen.Add(Key);
			// Child contacts reduce to one public shape pair. A transient native
			// contact is not the stable identity of that relationship.
			E.ContactIndex = E.ContactWorld = E.ContactGeneration = 0;
			Out.Add(E);
		}
		Out.Append(Owner.ActiveSensors);
		Out.Sort(Box3DAsyncPrivate::EventLess);
		return Out;
	};
	const TArray<FBox3DAsyncEvent> Before = CaptureRelationships();
	const b3ContactEvents Contacts = b3World_GetContactEvents(Owner.WorldId);
	const b3SensorEvents Sensors = b3World_GetSensorEvents(Owner.WorldId);
	const int64 Count = int64(Contacts.beginCount) + Contacts.endCount + Contacts.hitCount
		+ Sensors.beginCount + Sensors.endCount;
	int32 RecordLimit = 1024;
#if WITH_DEV_AUTOMATION_TESTS
	{ FScopeLock Lock(&Owner.QueueMutex); RecordLimit = Owner.EventRecordLimitForTest; }
#endif
	// A rejected delta is still a real step. Skip the transient arrays but
	// rebuild current membership immediately, even during consecutive bursts.
	const bool bInputOverflow = Count < 0 || Count > RecordLimit;
	FBox3DAsyncEventBatch Batch;
	Batch.World = Owner.QueryWorld;
	Batch.Step = Step;
	{ FScopeLock Lock(&Owner.DiagnosticsMutex); Batch.Sample = Owner.CompletedSample; }
	if (!bInputOverflow) Batch.Events.Reserve(int32(Count));
	bool bLoss = bInputOverflow;
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
		Event.BodyHandleA = {Owner.QueryWorld, A->Body}; Event.ShapeHandleA = {Owner.QueryWorld, A->Shape};
		Event.BodyHandleB = {Owner.QueryWorld, B->Body}; Event.ShapeHandleB = {Owner.QueryWorld, B->Shape};
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
	if (!bInputOverflow)
	{
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
		if (Owner.ActiveContacts.GetAllocatedSize() + Owner.ActiveSensors.GetAllocatedSize() > FImpl::MaxEventActiveBytes)
		{
			Owner.ActiveContacts.Remove(Key);
			Owner.ActiveContacts.Shrink();
			bLoss = true;
			continue;
		}
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
		if (Owner.ActiveContacts.GetAllocatedSize() + Owner.ActiveSensors.GetAllocatedSize() > FImpl::MaxEventActiveBytes)
		{
			Owner.ActiveSensors.Pop(EAllowShrinking::Yes);
			bLoss = true;
			continue;
		}
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
	}
	Owner.PendingRemovedBodies.Reset();
	// Rebuild complete truth after uncertain native transitions. The native
	// contact getter includes non-opted contacts, while sensor data is the
	// recorded overlap set (including its limited CCD visitor path).
	bool bPreviouslyValid;
	{ FScopeLock Lock(&Owner.QueueMutex); bPreviouslyValid = Owner.bEventTrackingValid; }
	if (bLoss || !bPreviouslyValid)
	{
		TMap<uint64, FBox3DAsyncEvent> RebuiltContacts;
		TArray<FBox3DAsyncEvent> RebuiltSensors;
		bool bComplete = true;
		for (const auto& Body : Owner.WorkerBodies)
		{
			const int32 ShapeCount = b3Body_GetShapeCount(Body.Value.NativeId);
			if (ShapeCount < 0 || ShapeCount > 8192 || uint64(ShapeCount) * sizeof(b3ShapeId) > FImpl::MaxEventScratchBytes) { bComplete = false; break; }
			TArray<b3ShapeId> BodyShapes;
			BodyShapes.SetNumUninitialized(ShapeCount);
			if (b3Body_GetShapes(Body.Value.NativeId, BodyShapes.GetData(), ShapeCount) != ShapeCount)
			{ bComplete = false; break; }
			for (const b3ShapeId Shape : BodyShapes)
				if (!Owner.WorkerShapes.Contains(b3StoreShapeId(Shape))) { bComplete = false; break; }
			if (!bComplete) break;
		}
		for (const auto& ShapePair : Owner.WorkerShapes)
		{
			if (!bComplete) break;
			const b3ShapeId Shape = b3LoadShapeId(ShapePair.Key);
			if (!b3Shape_IsValid(Shape)) continue; // post-removal tombstone
			const int32 Capacity = b3Shape_GetContactCapacity(Shape);
			if (Capacity < 0 || Capacity > 8192 || uint64(Capacity) * sizeof(b3ContactData)
				+ RebuiltContacts.GetAllocatedSize() + RebuiltSensors.GetAllocatedSize() > FImpl::MaxEventScratchBytes)
			{ bComplete = false; break; }
			TArray<b3ContactData> Data;
			Data.SetNumUninitialized(Capacity);
			const int32 Actual = Capacity ? b3Shape_GetContactData(Shape, Data.GetData(), Capacity) : 0;
			if (Actual < 0 || Actual > Capacity) { bComplete = false; break; }
			for (int32 I = 0; I < Actual; ++I)
			{
				const auto& Contact = Data[I];
				if (!b3Shape_IsValid(Contact.shapeIdA) || !b3Shape_IsValid(Contact.shapeIdB)) { bComplete = false; break; }
				if (!b3Shape_AreContactEventsEnabled(Contact.shapeIdA) && !b3Shape_AreContactEventsEnabled(Contact.shapeIdB)) continue;
				FBox3DAsyncEvent Event;
				bool bSwap = false;
				if (!MakeEvent(EBox3DAsyncEventKind::ContactBegin, Contact.shapeIdA, Contact.shapeIdB, Contact.contactId, bSwap, Event))
				{ bComplete = false; break; }
				RebuiltContacts.Add(ContactKey(Contact.contactId), Event);
				if (RebuiltContacts.Num() > 8192 || RebuiltContacts.GetAllocatedSize() + RebuiltSensors.GetAllocatedSize()
					+ Data.GetAllocatedSize() > FImpl::MaxEventScratchBytes) { bComplete = false; break; }
			}
			if (!bComplete || !b3Shape_IsSensor(Shape) || !b3Shape_AreSensorEventsEnabled(Shape)) continue;
			const int32 SensorCapacity = b3Shape_GetSensorCapacity(Shape);
			if (SensorCapacity < 0 || SensorCapacity > 8192 || uint64(SensorCapacity) * sizeof(b3ShapeId)
				+ Data.GetAllocatedSize() + RebuiltContacts.GetAllocatedSize() + RebuiltSensors.GetAllocatedSize() > FImpl::MaxEventScratchBytes)
			{ bComplete = false; break; }
			TArray<b3ShapeId> Visitors;
			Visitors.SetNumUninitialized(SensorCapacity);
			const int32 SensorActual = SensorCapacity ? b3Shape_GetSensorData(Shape, Visitors.GetData(), SensorCapacity) : 0;
			if (SensorActual < 0 || SensorActual > SensorCapacity) { bComplete = false; break; }
			for (int32 I = 0; I < SensorActual; ++I)
			{
				if (!b3Shape_IsValid(Visitors[I])) { bComplete = false; break; }
				if (!b3Shape_AreSensorEventsEnabled(Visitors[I])) continue;
				FBox3DAsyncEvent Event;
				bool bSwap = false;
				if (!MakeEvent(EBox3DAsyncEventKind::SensorEnter, Shape, Visitors[I], b3ContactId{}, bSwap, Event))
				{ bComplete = false; break; }
				if (!RebuiltSensors.ContainsByPredicate([&Event](const FBox3DAsyncEvent& V)
					{ return V.ShapeA == Event.ShapeA && V.ShapeB == Event.ShapeB; })) RebuiltSensors.Add(Event);
				if (RebuiltSensors.Num() > 8192 || RebuiltContacts.GetAllocatedSize() + RebuiltSensors.GetAllocatedSize()
					+ Data.GetAllocatedSize() + Visitors.GetAllocatedSize() > FImpl::MaxEventScratchBytes) { bComplete = false; break; }
			}
		}
		if (bComplete && RebuiltContacts.GetAllocatedSize() + RebuiltSensors.GetAllocatedSize() > FImpl::MaxEventActiveBytes)
			bComplete = false;
		if (bComplete)
		{
			Owner.ActiveContacts = MoveTemp(RebuiltContacts);
			Owner.ActiveSensors = MoveTemp(RebuiltSensors);
		}
		{ FScopeLock Lock(&Owner.QueueMutex); Owner.bEventTrackingValid = bComplete; }
	}
	const TArray<FBox3DAsyncEvent> After = CaptureRelationships();
	FBox3DAsyncEventBatch SubscriptionBatch;
	SubscriptionBatch.World = Owner.QueryWorld;
	SubscriptionBatch.Step = Step;
	SubscriptionBatch.Sample = Batch.Sample;
	if (!bLoss && bPreviouslyValid)
	{
		TSet<Box3DAsyncPrivate::FEventPairKey> BeforeKeys, AfterKeys, RemovedKeys;
		for (const FBox3DAsyncEvent& E : Before) BeforeKeys.Add(Box3DAsyncPrivate::ShapePair(E));
		for (const FBox3DAsyncEvent& E : After) AfterKeys.Add(Box3DAsyncPrivate::ShapePair(E));
		for (const FBox3DAsyncEvent& E : Batch.Events)
			if ((E.Kind == EBox3DAsyncEventKind::ContactEnd || E.Kind == EBox3DAsyncEventKind::SensorExit)
				&& E.EndCause == EBox3DAsyncEventEndCause::Removed) RemovedKeys.Add(Box3DAsyncPrivate::ShapePair(E));
		for (const FBox3DAsyncEvent& E : Before)
			if (!AfterKeys.Contains(Box3DAsyncPrivate::ShapePair(E)))
			{
				FBox3DAsyncEvent End = E;
				End.Kind = E.Kind == EBox3DAsyncEventKind::ContactBegin ? EBox3DAsyncEventKind::ContactEnd : EBox3DAsyncEventKind::SensorExit;
				if (RemovedKeys.Contains(Box3DAsyncPrivate::ShapePair(E))) End.EndCause = EBox3DAsyncEventEndCause::Removed;
				SubscriptionBatch.Events.Add(End);
			}
		for (const FBox3DAsyncEvent& E : After)
			if (!BeforeKeys.Contains(Box3DAsyncPrivate::ShapePair(E))) SubscriptionBatch.Events.Add(E);
		for (const FBox3DAsyncEvent& E : Batch.Events)
			if (E.Kind == EBox3DAsyncEventKind::Impact) SubscriptionBatch.Events.Add(E);
	}
	SubscriptionBatch.Events.Sort(Box3DAsyncPrivate::EventLess);
	{
		FScopeLock Lock(&Owner.QueueMutex);
		const bool bGap = bLoss || !bPreviouslyValid || !Owner.bEventTrackingValid;
		if (bGap || !SubscriptionBatch.Events.IsEmpty())
		{
			if (Owner.EventSequence == MAX_uint64) Owner.bEventTrackingValid = false;
			else ++Owner.EventSequence;
			SubscriptionBatch.Sequence = Owner.EventSequence;
			const int32 RecordCount = SubscriptionBatch.Events.Num();
			bool bDrop = bGap || Owner.EventIngress.Num() >= 128 || RecordCount > 1024;
			if (!bDrop)
			{
				Owner.EventIngress.Add(MoveTemp(SubscriptionBatch));
				if (FImpl::EventArrayBytes(Owner.EventIngress) > FImpl::MaxEventIngressBytes)
				{
					Owner.EventIngress.RemoveAt(Owner.EventIngress.Num() - 1, 1, EAllowShrinking::Yes);
					if (Owner.EventIngress.IsEmpty()) Owner.EventIngress.Empty();
					bDrop = true;
				}
			}
			if (bDrop)
			{
				++Owner.ExchangeDiagnostics.DroppedEventBatches;
				Owner.ExchangeDiagnostics.DroppedEventRecords += bInputOverflow
					? uint64(FMath::Max<int64>(Count, 0)) : uint64(RecordCount);
				if (Owner.FirstLostEventSequence == 0) Owner.FirstLostEventSequence = Owner.EventSequence;
				Owner.LastLostEventSequence = Owner.EventSequence;
			}
			else
			{
				Owner.ExchangeDiagnostics.PeakEventIngressBatches = FMath::Max(Owner.ExchangeDiagnostics.PeakEventIngressBatches, Owner.EventIngress.Num());
				Owner.ExchangeDiagnostics.PeakEventIngressBytes = FMath::Max(Owner.ExchangeDiagnostics.PeakEventIngressBytes, FImpl::EventArrayBytes(Owner.EventIngress));
			}
		}
		Owner.bEventBaselineReady = Owner.bEventTrackingValid && After.GetAllocatedSize() <= FImpl::MaxEventBaselineBytes;
		if (Owner.bEventBaselineReady)
		{
			Owner.EventBaseline.World = Owner.QueryWorld;
			Owner.EventBaseline.Step = Step;
			Owner.EventBaseline.Sample = Batch.Sample;
			Owner.EventBaseline.Sequence = Owner.EventSequence;
			Owner.EventBaseline.Events = After;
		}
		else
		{
			// Never retain or publish a partial relationship snapshot.
			Owner.EventBaseline.Events.Empty();
		}
	}
	if (Batch.Events.IsEmpty() && !bLoss) return;
	if (Owner.NextEventSequence == 0) bLoss = true;
	Batch.Sequence = Owner.NextEventSequence++;
	Batch.Events.Sort(Box3DAsyncPrivate::EventLess);
	FScopeLock Lock(&Owner.QueueMutex);
	if (bLoss || Owner.EventBatches.Num() >= 128) Owner.bEventGap = true;
	else if (!Owner.bEventGap)
	{
		Owner.EventBatches.Add(MoveTemp(Batch));
		if (FImpl::EventArrayBytes(Owner.EventBatches) > FImpl::MaxEventIngressBytes)
		{
			Owner.EventBatches.RemoveAt(Owner.EventBatches.Num() - 1, 1, EAllowShrinking::Yes);
			if (Owner.EventBatches.IsEmpty()) Owner.EventBatches.Empty();
			Owner.bEventGap = true;
		}
	}
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
			Owner.CompletedSample.Step = CompletedStep;
			Owner.CompletedSample.ScheduledMonotonicSeconds = ScheduledTime;
			Owner.CompletedSample.ClockEpoch = Owner.ClockEpoch;
		}
		CollectEvents(CompletedStep);
		// Native end buffers rotate once per step; sensor overlap history is
		// merged during that step. Any unexpected later unresolved end gaps.
		if (!Owner.RetiringBodies.IsEmpty())
		{
			int32 RetiredIdentities = 0;
			for (auto It = Owner.WorkerShapes.CreateIterator(); It; ++It)
				if (Owner.RetiringBodies.Contains(It.Value().Body)) { It.RemoveCurrent(); ++RetiredIdentities; }
			{ FScopeLock Lock(&Owner.QueueMutex); Owner.LiveShapeIdentities -= RetiredIdentities; }
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
	Snapshot.CaptureMonotonicSeconds = FPlatformTime::Seconds();
	{
		FScopeLock Lock(&Owner.DiagnosticsMutex);
		Snapshot.CaptureSerial = ++Owner.CaptureSerial;
		Snapshot.SimulationSample = Owner.CompletedSample;
	}
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
		Pose.bAwake = b3Body_IsAwake(Pair.Value.NativeId);
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
