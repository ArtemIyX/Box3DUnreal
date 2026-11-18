#include "World/Box3DAsyncSubsystem.h"

#include "Box3DAsyncSubsystemImpl.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "PhysicsEngine/BodySetup.h"
#include "HAL/PlatformTime.h"
#include "Misc/ScopeExit.h"
#include "Simulation/Box3DConversion.h"
#include "StaticMeshResources.h"
#include "World/Box3DSettings.h"
#include "Collision/Box3DCollisionData.h"
#include "Data/Structs/Box3DBakedBody.h"
#include "Data/Structs/Box3DBakedShape.h"
#include "Data/Enums/Box3DBakedShapeKind.h"
#include "box3d/collision.h"

namespace
{
	constexpr uint64 MaxGeometryVertices = 1'000'000;
	constexpr uint64 MaxGeometryIndices = 1'500'000;
	constexpr double MinTriangleAreaSquareMeters = 2.5e-7;

	bool IsFiniteVector(const FVector& Value)
	{
		return FMath::IsFinite(Value.X) && FMath::IsFinite(Value.Y) && FMath::IsFinite(Value.Z);
	}

	bool IsFiniteNativeVector(const b3Vec3& Value)
	{
		return FMath::IsFinite(Value.x) && FMath::IsFinite(Value.y) && FMath::IsFinite(Value.z);
	}

	bool IsFiniteNativePosition(const b3Pos& Value)
	{
		return FMath::IsFinite(double(Value.x)) && FMath::IsFinite(double(Value.y)) && FMath::IsFinite(double(Value.z));
	}

	bool IsValidTransform(const FTransform& Transform)
	{
		if (!IsFiniteVector(Transform.GetLocation()) || !Transform.GetRotation().IsNormalized()
			|| !Transform.GetScale3D().Equals(FVector::OneVector, KINDA_SMALL_NUMBER))
		{
			return false;
		}
		const b3Pos Position = Box3D::ToB3Pos(Transform.GetLocation());
		const b3Quat Rotation = Box3D::ToB3(Transform.GetRotation());
		return IsFiniteNativePosition(Position) && FMath::IsFinite(Rotation.v.x) && FMath::IsFinite(Rotation.v.y)
			&& FMath::IsFinite(Rotation.v.z) && FMath::IsFinite(Rotation.s);
	}

	bool IsValidPositiveScale(const FVector& Scale)
	{
		if (!IsFiniteVector(Scale) || Scale.X <= 0.0 || Scale.Y <= 0.0 || Scale.Z <= 0.0)
		{
			return false;
		}
		const b3Vec3 NativeScale = Box3D::ToB3Dir(Scale);
		return IsFiniteNativeVector(NativeScale)
			&& NativeScale.x >= 1.0e-4f && NativeScale.y >= 1.0e-4f && NativeScale.z >= 1.0e-4f;
	}

	bool IsValidMeshPlacement(const FTransform& Transform, const FVector& Scale,
	                          const FVector& BoundsMin, const FVector& BoundsMax)
	{
		const b3Vec3 NativeScale = Box3D::ToB3Dir(Scale);
		const FVector ScaledMin = BoundsMin * FVector(NativeScale.x, NativeScale.y, NativeScale.z);
		const FVector ScaledMax = BoundsMax * FVector(NativeScale.x, NativeScale.y, NativeScale.z);
		if (!IsFiniteVector(ScaledMin) || !IsFiniteVector(ScaledMax))
		{
			return false;
		}
		const FVector MaxAbs = FVector(
			FMath::Max(FMath::Abs(ScaledMin.X), FMath::Abs(ScaledMax.X)),
			FMath::Max(FMath::Abs(ScaledMin.Y), FMath::Abs(ScaledMax.Y)),
			FMath::Max(FMath::Abs(ScaledMin.Z), FMath::Abs(ScaledMax.Z)));
		const double Radius = MaxAbs.Size();
		if (!FMath::IsFinite(Radius) || Radius > double(TNumericLimits<float>::Max()))
		{
			return false;
		}
		const FVector Center = Transform.GetLocation();
		const FVector WorldMin = Center - FVector(Radius * Box3D::MetersToUE);
		const FVector WorldMax = Center + FVector(Radius * Box3D::MetersToUE);
		return IsFiniteNativePosition(Box3D::ToB3Pos(WorldMin))
			&& IsFiniteNativePosition(Box3D::ToB3Pos(WorldMax));
	}

	bool ValidateAndCopyGeometry(const TArray<FVector>& Vertices, const TArray<int32>& InputIndices,
	                             EBox3DAsyncMeshWinding Winding, TArray<b3Vec3>& OutVertices,
	                             TArray<int32>& OutIndices, uint64& OutByteCount,
	                             FVector& OutBoundsMin, FVector& OutBoundsMax)
	{
		OutVertices.Reset();
		OutIndices.Reset();
		OutByteCount = 0;
		OutBoundsMin = FVector::ZeroVector;
		OutBoundsMax = FVector::ZeroVector;
		if (Vertices.Num() < 3 || uint64(Vertices.Num()) > MaxGeometryVertices || InputIndices.Num() < 3
			|| uint64(InputIndices.Num()) > MaxGeometryIndices || InputIndices.Num() % 3 != 0
			|| (Winding != EBox3DAsyncMeshWinding::Box3D && Winding != EBox3DAsyncMeshWinding::Unreal))
		{
			return false;
		}

		const uint64 ByteCount = uint64(Vertices.Num()) * sizeof(b3Vec3) + uint64(InputIndices.Num()) * sizeof(int32);
		if (ByteCount > UBox3DAsyncSubsystem::MaxMeshGeometryBytes)
		{
			return false;
		}

		OutVertices.Reserve(Vertices.Num());
		TArray<FVector> NativeVertexPositions;
		NativeVertexPositions.Reserve(Vertices.Num());
		OutBoundsMin = FVector(TNumericLimits<double>::Max());
		OutBoundsMax = FVector(TNumericLimits<double>::Lowest());
		for (const FVector& Vertex : Vertices)
		{
			if (!IsFiniteVector(Vertex))
			{
				return false;
			}
			const b3Vec3 NativeVertex = Box3D::ToB3(Vertex);
			if (!IsFiniteNativeVector(NativeVertex))
			{
				return false;
			}
			OutVertices.Add(NativeVertex);
			NativeVertexPositions.Emplace(NativeVertex.x, NativeVertex.y, NativeVertex.z);
			OutBoundsMin = OutBoundsMin.ComponentMin(NativeVertexPositions.Last());
			OutBoundsMax = OutBoundsMax.ComponentMax(NativeVertexPositions.Last());
		}

		OutIndices = InputIndices;
		if (Winding == EBox3DAsyncMeshWinding::Unreal)
		{
			for (int32 Triangle = 0; Triangle < OutIndices.Num(); Triangle += 3)
			{
				Swap(OutIndices[Triangle + 1], OutIndices[Triangle + 2]);
			}
		}

		struct FEdgeDirection
		{
			FEdgeDirection() = default;

			FEdgeDirection(int8 InDirection, uint8 InCount) : Direction(InDirection), Count(InCount)
			{
			}

			int8 Direction = 0;
			uint8 Count = 0;
		};
		TMap<uint64, FEdgeDirection> Edges;
		Edges.Reserve(OutIndices.Num());
		double SignedVolumeTimesSix = 0.0;
		for (int32 Triangle = 0; Triangle < OutIndices.Num(); Triangle += 3)
		{
			const int32 A = OutIndices[Triangle];
			const int32 B = OutIndices[Triangle + 1];
			const int32 C = OutIndices[Triangle + 2];
			if (A < 0 || B < 0 || C < 0 || A >= Vertices.Num() || B >= Vertices.Num() || C >= Vertices.Num()
				|| A == B || B == C || C == A)
			{
				return false;
			}

			const FVector& VA = NativeVertexPositions[A];
			const FVector& VB = NativeVertexPositions[B];
			const FVector& VC = NativeVertexPositions[C];
			const FVector Cross = FVector::CrossProduct(VB - VA, VC - VA);
			const double Area = 0.5 * FMath::Sqrt(Cross.SizeSquared());
			if (!FMath::IsFinite(Area) || Area < MinTriangleAreaSquareMeters)
			{
				return false;
			}
			SignedVolumeTimesSix += FVector::DotProduct(VA, FVector::CrossProduct(VB, VC));

			const int32 TriangleIndices[3] = {A, B, C};
			for (int32 EdgeIndex = 0; EdgeIndex < 3; ++EdgeIndex)
			{
				const int32 From = TriangleIndices[EdgeIndex];
				const int32 To = TriangleIndices[(EdgeIndex + 1) % 3];
				const uint32 Low = static_cast<uint32>(FMath::Min(From, To));
				const uint32 High = static_cast<uint32>(FMath::Max(From, To));
				const uint64 EdgeKey = (uint64(Low) << 32) | uint64(High);
				const int8 Direction = From < To ? 1 : -1;
				FEdgeDirection* Existing = Edges.Find(EdgeKey);
				if (Existing == nullptr)
				{
					Edges.Add(EdgeKey, FEdgeDirection(Direction, 1));
				}
				else
				{
					if (Existing->Direction == Direction || Existing->Count >= 2)
					{
						return false;
					}
					++Existing->Count;
				}
			}
		}

		bool bClosedMesh = !Edges.IsEmpty();
		for (const TPair<uint64, FEdgeDirection>& Edge : Edges)
		{
			bClosedMesh &= Edge.Value.Count == 2;
		}
		if (bClosedMesh && SignedVolumeTimesSix <= 0.0)
		{
			return false;
		}
		OutByteCount = ByteCount;
		return true;
	}
}

namespace Box3DAsyncPrivate
{
#if WITH_DEV_AUTOMATION_TESTS
	bool ValidateAndCopyGeometryForTest(const TArray<FVector>& Vertices, const TArray<int32>& Indices,
	                                    EBox3DAsyncMeshWinding Winding, TArray<b3Vec3>& OutVertices,
	                                    TArray<int32>& OutIndices,
	                                    uint64& OutByteCount, FVector& OutBoundsMin, FVector& OutBoundsMax)
	{
		return ValidateAndCopyGeometry(Vertices, Indices, Winding, OutVertices, OutIndices,
		                               OutByteCount, OutBoundsMin, OutBoundsMax);
	}
#endif
	bool IsSupportedAuthoredCollisionScale(bool bHasRoundShapes, const FVector& Scale)
	{
		return !bHasRoundShapes || (Scale.X == Scale.Y && Scale.Y == Scale.Z);
	}

	bool CopyAuthoredCollisionShapes(const UBodySetup& BodySetup, const FVector& Scale,
	                                 TArray<FCopiedStaticShape>& OutShapes, FVector& OutBoundsMin,
	                                 FVector& OutBoundsMax, uint64& OutPayloadBytes)
	{
		OutShapes.Reset();
		OutBoundsMin = FVector(TNumericLimits<double>::Max());
		OutBoundsMax = FVector(TNumericLimits<double>::Lowest());
		OutPayloadBytes = 0;
		const FKAggregateGeom& AggGeom = BodySetup.AggGeom;
		const int64 ShapeCount = int64(AggGeom.SphereElems.Num()) + AggGeom.SphylElems.Num()
			+ AggGeom.BoxElems.Num() + AggGeom.ConvexElems.Num();
		const bool bHasRoundShapes = !AggGeom.SphereElems.IsEmpty() || !AggGeom.SphylElems.IsEmpty();
		if (ShapeCount <= 0 || ShapeCount > 256 || !IsSupportedAuthoredCollisionScale(bHasRoundShapes, Scale))
		{
			return false;
		}
		auto IncludeBounds = [&OutBoundsMin, &OutBoundsMax](const FVector& Min, const FVector& Max)
		{
			OutBoundsMin = OutBoundsMin.ComponentMin(Min);
			OutBoundsMax = OutBoundsMax.ComponentMax(Max);
		};
		auto AddHull = [&OutShapes, &IncludeBounds, &OutPayloadBytes](const TArray<FVector>& LocalPoints) -> bool
		{
			if (LocalPoints.Num() < 4 || LocalPoints.Num() > 65536) return false;
			FCopiedStaticShape& Shape = OutShapes.AddDefaulted_GetRef();
			Shape.Type = EStaticShapeType::Hull;
			Shape.HullPoints.Reserve(LocalPoints.Num());
			for (const FVector& Point : LocalPoints)
			{
				if (!IsFiniteVector(Point)) return false;
				const b3Vec3 NativePoint = Box3D::ToB3(Point);
				if (!IsFiniteNativeVector(NativePoint)) return false;
				Shape.HullPoints.Add(NativePoint);
				const FVector P(NativePoint.x, NativePoint.y, NativePoint.z);
				IncludeBounds(P, P);
			}
			const uint64 Bytes = sizeof(Shape) + uint64(Shape.HullPoints.Num()) * sizeof(b3Vec3);
			if (Bytes > UBox3DAsyncSubsystem::MaxMeshGeometryBytes - OutPayloadBytes) return false;
			OutPayloadBytes += Bytes;
			return true;
		};
		auto AddPrimitiveBytes = [&OutShapes, &OutPayloadBytes]() -> bool
		{
			const uint64 Bytes = sizeof(FCopiedStaticShape);
			if (Bytes > UBox3DAsyncSubsystem::MaxMeshGeometryBytes - OutPayloadBytes) return false;
			OutPayloadBytes += Bytes;
			return true;
		};
		for (const FKSphereElem& Element : AggGeom.SphereElems)
		{
			const FVector Center = FVector(Element.Center) * Scale;
			const float Radius = float(double(Element.Radius) * Scale.X * Box3D::UEToMeters);
			const b3Vec3 NativeCenter = Box3D::ToB3(Center);
			if (!IsFiniteNativeVector(NativeCenter) || !FMath::IsFinite(Radius) || Radius <= 0.0f) return false;
			IncludeBounds(FVector(NativeCenter.x - Radius, NativeCenter.y - Radius, NativeCenter.z - Radius),
			              FVector(NativeCenter.x + Radius, NativeCenter.y + Radius, NativeCenter.z + Radius));
			FCopiedStaticShape& Shape = OutShapes.AddDefaulted_GetRef();
			Shape.Type = EStaticShapeType::Sphere;
			Shape.Sphere = b3Sphere{NativeCenter, Radius};
			if (!AddPrimitiveBytes()) return false;
		}
		for (const FKSphylElem& Element : AggGeom.SphylElems)
		{
			const FVector AxisOffset = Element.Rotation.RotateVector(FVector(0.0, 0.0, Element.Length * 0.5));
			const b3Vec3 NativeA = Box3D::ToB3((FVector(Element.Center) - AxisOffset) * Scale);
			const b3Vec3 NativeB = Box3D::ToB3((FVector(Element.Center) + AxisOffset) * Scale);
			const float Radius = float(double(Element.Radius) * Scale.X * Box3D::UEToMeters);
			if (!IsFiniteNativeVector(NativeA) || !IsFiniteNativeVector(NativeB) || !FMath::IsFinite(Radius) || Radius
				<= 0.0f)
				return false;
			IncludeBounds(FVector(FMath::Min(NativeA.x, NativeB.x) - Radius, FMath::Min(NativeA.y, NativeB.y) - Radius,
			                      FMath::Min(NativeA.z, NativeB.z) - Radius),
			              FVector(FMath::Max(NativeA.x, NativeB.x) + Radius, FMath::Max(NativeA.y, NativeB.y) + Radius,
			                      FMath::Max(NativeA.z, NativeB.z) + Radius));
			FCopiedStaticShape& Shape = OutShapes.AddDefaulted_GetRef();
			Shape.Type = EStaticShapeType::Capsule;
			Shape.Capsule = b3Capsule{NativeA, NativeB, Radius};
			if (!AddPrimitiveBytes()) return false;
		}
		for (const FKBoxElem& Element : AggGeom.BoxElems)
		{
			const FTransform ElementTransform(Element.Rotation.Quaternion(), FVector(Element.Center));
			TArray<FVector> Points;
			Points.Reserve(8);
			for (int32 Corner = 0; Corner < 8; ++Corner)
			{
				const FVector Half(Element.X * ((Corner & 1) ? 0.5 : -0.5), Element.Y * ((Corner & 2) ? 0.5 : -0.5),
				                   Element.Z * ((Corner & 4) ? 0.5 : -0.5));
				Points.Add(ElementTransform.TransformPosition(Half) * Scale);
			}
			if (!AddHull(Points)) return false;
		}
		for (const FKConvexElem& Element : AggGeom.ConvexElems)
		{
			const FTransform ElementTransform = Element.GetTransform();
			TArray<FVector> Points;
			Points.Reserve(Element.VertexData.Num());
			for (const FVector& Vertex : Element.VertexData)
			{
				Points.Add(ElementTransform.TransformPosition(Vertex) * Scale);
			}
			if (!AddHull(Points)) return false;
		}
		return !OutShapes.IsEmpty();
	}
}

UBox3DAsyncSubsystem::~UBox3DAsyncSubsystem()
{
	check(IsInGameThread());
	if (Impl.IsValid())
	{
		Impl->Shutdown();
		Impl.Reset();
	}
}

bool UBox3DAsyncSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	check(IsInGameThread());
	if (!Super::ShouldCreateSubsystem(Outer))
	{
		return false;
	}
	const UWorld* World = Cast<UWorld>(Outer);
	if (World == nullptr || (World->WorldType != EWorldType::Game && World->WorldType != EWorldType::PIE))
	{
		return false;
	}
	const UBox3DSettings* Settings = GetDefault<UBox3DSettings>();
	return Settings != nullptr && Settings->IsBackendEnabled(EBox3DBackend::Async)
		&& (!Settings->bAuthorityOnlySimulation || World->GetNetMode() != NM_Client);
}

void UBox3DAsyncSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	check(IsInGameThread());
	Super::Initialize(Collection);
	Impl = MakeUnique<FImpl>();
	const UBox3DSettings* Settings = GetDefault<UBox3DSettings>();
	if (Settings == nullptr || !FMath::IsFinite(Settings->FixedTimeStep)
		|| Settings->FixedTimeStep < 0.001f || Settings->FixedTimeStep > 1.0f || !IsFiniteVector(Settings->Gravity))
	{
		return;
	}

	Impl->FixedTimeStep = Settings->FixedTimeStep;
	Impl->SubStepCount = FMath::Clamp(Settings->SubStepCount, 1, 16);
	const b3Vec3 NativeGravity = Box3D::ToB3Accel(Settings->Gravity);
	if (!IsFiniteNativeVector(NativeGravity))
	{
		return;
	}
	b3WorldDef WorldDef = b3DefaultWorldDef();
	WorldDef.gravity = NativeGravity;
	WorldDef.enableSleep = Settings->bEnableSleep;
	WorldDef.enableContinuous = Settings->bEnableContinuous;
	WorldDef.workerCount = 1;
	WorldDef.hitEventThreshold = Settings->HitEventThreshold * Box3D::UEToMeters;
	Impl->WorldId = b3CreateWorld(&WorldDef);
	if (!b3World_IsValid(Impl->WorldId))
	{
		return;
	}

	Impl->WakeEvent = FPlatformProcess::GetSynchEventFromPool(false);
	Impl->bAccepting = Impl->WakeEvent != nullptr;
	if (!Impl->bAccepting)
	{
		b3DestroyWorld(Impl->WorldId);
		Impl->WorldId = b3WorldId{};
		return;
	}
	Impl->Worker = MakeUnique<FImpl::FWorker>(*Impl);
	Box3DAsyncStats::RegisterWorld(Impl.Get());
	Impl->bStatsRegistered = true;
	Impl->Thread = FRunnableThread::Create(Impl->Worker.Get(), TEXT("Box3DAsyncWorld"), 0, TPri_AboveNormal);
	if (Impl->Thread == nullptr)
	{
		Box3DAsyncStats::UnregisterWorld(Impl.Get());
		Impl->bStatsRegistered = false;
		Impl->bAccepting = false;
		Impl->Worker.Reset();
		FPlatformProcess::ReturnSynchEventToPool(Impl->WakeEvent);
		Impl->WakeEvent = nullptr;
		b3DestroyWorld(Impl->WorldId);
		Impl->WorldId = b3WorldId{};
	}
}

void UBox3DAsyncSubsystem::Deinitialize()
{
	check(IsInGameThread());
	if (Impl.IsValid() && Impl->bClosingQueries) return;
	if (Impl.IsValid())
	{
		Impl->bClosingQueries = true;
		Impl->Shutdown();
		// The worker has joined. Committed outcomes survive; all other accepted
		// requests select WorldEnded, including work completed after admission closed.
		{
			FScopeLock Lock(&Impl->QueueMutex);
			for (auto& Pair : Impl->Queries)
			{
				FImpl::FQueryRecord& Record = Pair.Value;
				if (!Record.bCommitted)
				{
					Record.Result.Status = EBox3DAsyncQueryStatus::WorldEnded;
					Record.Result.Hits.Reset();
					Record.bReady = true;
					Record.bCommitted = true;
				}
			}
		}
		DispatchQueryCallbacks(true);
		CloseEventSubscriptions();
		TArray<uint64> Consumers;
		Impl->ShutdownCallbacks.GetKeys(Consumers);
		Consumers.Sort();
		for (uint64 ConsumerId : Consumers)
		{
			FImpl::FShutdownCallback* Entry = Impl->ShutdownCallbacks.Find(ConsumerId);
			if (!Entry || !Entry->Owner.IsValid() || !Entry->Callback || !Impl->QueryConsumers.Contains(ConsumerId)) continue;
			TFunction<void()> Callback = MoveTemp(Entry->Callback);
			Impl->ShutdownCallbacks.Remove(ConsumerId);
			Callback();
		}
	}
	Impl.Reset();
	Super::Deinitialize();
}

void UBox3DAsyncSubsystem::Tick(float DeltaTime)
{
	check(IsInGameThread());
	if (Impl.IsValid() && !Impl->bClosingQueries)
	{
		PumpQueryCallbacks();
		PumpEventSubscriptions();
	}
}

TStatId UBox3DAsyncSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UBox3DAsyncSubsystem, STATGROUP_Tickables);
}

bool UBox3DAsyncSubsystem::TryCreateStaticBox(const FTransform& Transform, const FVector& HalfExtents,
                                              FBox3DAsyncBodyId& OutBodyId)
{
	FBox3DAsyncShapeOptions Options;
	Options.CategoryBits = 1;
	Options.MaskBits = 3;
	Options.Density = b3DefaultShapeDef().density;
	return TryCreateSimpleBox(Transform, HalfExtents, Options, false, OutBodyId);
}

bool UBox3DAsyncSubsystem::TryCreateStaticBox(const FTransform& Transform, const FVector& HalfExtents,
                                              const FBox3DAsyncShapeOptions& Options, FBox3DAsyncBodyId& OutBodyId)
{
	return TryCreateSimpleBox(Transform, HalfExtents, Options, true, OutBodyId);
}

bool UBox3DAsyncSubsystem::TryCreateStaticBox(const FTransform& Transform, const FVector& HalfExtents,
	const FBox3DAsyncShapeOptions& Options, FBox3DAsyncBodyHandle& OutBody)
{
	OutBody = {};
	FBox3DAsyncBodyId Id = 0;
	if (!TryCreateSimpleBox(Transform, HalfExtents, Options, true, Id)) return false;
	OutBody = {Impl->QueryWorld, Id};
	return true;
}

bool UBox3DAsyncSubsystem::TryCreateSimpleBox(const FTransform& Transform, const FVector& HalfExtents,
	const FBox3DAsyncShapeOptions& Options, bool bRequireIdentity, FBox3DAsyncBodyId& OutBodyId)
{
	check(IsInGameThread());
	OutBodyId = 0;
	const b3Vec3 NativeHalfExtents = Box3D::ToB3(HalfExtents);
	if (!Impl.IsValid() || !FMath::IsFinite(Options.Density) || Options.Density < 0.0f || !IsValidTransform(Transform) || !IsFiniteVector(HalfExtents)
		|| HalfExtents.X <= 0.0 || HalfExtents.Y <= 0.0 || HalfExtents.Z <= 0.0
		|| !IsFiniteNativeVector(NativeHalfExtents) || NativeHalfExtents.x <= 0.0f || NativeHalfExtents.y <= 0.0f ||
		NativeHalfExtents.z <= 0.0f)
	{
		return false;
	}
	FScopeLock Lock(&Impl->QueueMutex);
	if (!Impl->bAccepting || Impl->Commands.Num() >= CommandCapacity || Impl->NextBodyId == 0)
	{
		return false;
	}
	if (Impl->OutstandingCreationResults >= OutstandingCreationResultCapacity
		|| (bRequireIdentity && Impl->LiveShapeIdentities + Impl->ReservedShapeIdentities >= 8192))
	{
		return false;
	}
	FImpl::FCommand& Command = Impl->Commands.AddDefaulted_GetRef();
	Command.Type = FImpl::ECommandType::CreateBox;
	Command.ShapeOptions = Options;
	Command.bHasShapeOptions = true;
	Command.ReservedShapeIdentities = bRequireIdentity ? 1 : 0;
	Impl->ReservedShapeIdentities += Command.ReservedShapeIdentities;
	Command.BodyId = Impl->NextBodyId++;
	Command.Transform = Transform;
	Command.Dimensions = HalfExtents;
	Impl->AcceptedBodyIds.Add(Command.BodyId);
	++Impl->OutstandingCreationResults;
	OutBodyId = Command.BodyId;
	Impl->WakeEvent->Trigger();
	return true;
}

bool UBox3DAsyncSubsystem::TryCreateDynamicSphere(const FTransform& Transform, float Radius,
                                                  const FVector& InitialVelocity, FBox3DAsyncBodyId& OutBodyId)
{
	FBox3DAsyncShapeOptions Options;
	Options.CategoryBits = 2;
	Options.MaskBits = 3;
	// Preserve Box3D's configured default density for the legacy creation API.
	Options.Density = b3DefaultShapeDef().density;
	return TryCreateSimpleSphere(Transform, Radius, InitialVelocity, Options, false, OutBodyId);
}

bool UBox3DAsyncSubsystem::TryCreateDynamicSphere(const FTransform& Transform, float Radius,
                                                  const FVector& InitialVelocity, const FBox3DAsyncShapeOptions& Options,
                                                  FBox3DAsyncBodyId& OutBodyId)
{
	return TryCreateSimpleSphere(Transform, Radius, InitialVelocity, Options, true, OutBodyId);
}

bool UBox3DAsyncSubsystem::TryCreateDynamicSphere(const FTransform& Transform, float Radius,
	const FVector& InitialVelocity, const FBox3DAsyncShapeOptions& Options, FBox3DAsyncBodyHandle& OutBody)
{
	OutBody = {};
	FBox3DAsyncBodyId Id = 0;
	if (!TryCreateSimpleSphere(Transform, Radius, InitialVelocity, Options, true, Id)) return false;
	OutBody = {Impl->QueryWorld, Id};
	return true;
}

bool UBox3DAsyncSubsystem::TryCreateSimpleSphere(const FTransform& Transform, float Radius,
	const FVector& InitialVelocity, const FBox3DAsyncShapeOptions& Options, bool bRequireIdentity,
	FBox3DAsyncBodyId& OutBodyId)
{
	check(IsInGameThread());
	OutBodyId = 0;
	const float NativeRadius = Radius * Box3D::UEToMeters;
	const b3Vec3 NativeVelocity = Box3D::ToB3(InitialVelocity);
	if (!Impl.IsValid() || !FMath::IsFinite(Options.Density) || Options.Density < 0.0f || !IsValidTransform(Transform) || !FMath::IsFinite(Radius) || Radius <= 0.0f
		|| !FMath::IsFinite(NativeRadius) || NativeRadius <= 0.0f || !IsFiniteVector(InitialVelocity)
		|| !IsFiniteNativeVector(NativeVelocity))
	{
		return false;
	}
	FScopeLock Lock(&Impl->QueueMutex);
	if (!Impl->bAccepting || Impl->Commands.Num() >= CommandCapacity || Impl->NextBodyId == 0)
	{
		return false;
	}
	if (Impl->OutstandingCreationResults >= OutstandingCreationResultCapacity
		|| (bRequireIdentity && Impl->LiveShapeIdentities + Impl->ReservedShapeIdentities >= 8192))
	{
		return false;
	}
	FImpl::FCommand& Command = Impl->Commands.AddDefaulted_GetRef();
	Command.Type = FImpl::ECommandType::CreateSphere;
	Command.ShapeOptions = Options;
	Command.bHasShapeOptions = true;
	Command.ReservedShapeIdentities = bRequireIdentity ? 1 : 0;
	Impl->ReservedShapeIdentities += Command.ReservedShapeIdentities;
	Command.BodyId = Impl->NextBodyId++;
	Command.Transform = Transform;
	Command.Dimensions.X = Radius;
	Command.InitialVelocity = InitialVelocity;
	Impl->AcceptedBodyIds.Add(Command.BodyId);
	++Impl->OutstandingCreationResults;
	OutBodyId = Command.BodyId;
	Impl->WakeEvent->Trigger();
	return true;
}

bool UBox3DAsyncSubsystem::TryRegisterStaticMeshGeometry(const TArray<FVector>& Vertices,
                                                         const TArray<int32>& Indices,
                                                         EBox3DAsyncMeshWinding Winding,
                                                         FBox3DAsyncGeometryId& OutGeometryId)
{
	check(IsInGameThread());
	OutGeometryId = 0;
	if (!Impl.IsValid())
	{
		return false;
	}
	if (Vertices.Num() < 3 || uint64(Vertices.Num()) > MaxGeometryVertices || Indices.Num() < 3
		|| uint64(Indices.Num()) > MaxGeometryIndices || Indices.Num() % 3 != 0)
	{
		return false;
	}
	const uint64 PreflightSourceBytes = uint64(Vertices.Num()) * sizeof(b3Vec3)
		+ uint64(Indices.Num()) * sizeof(int32);
	const uint64 PreflightTriangleCount = uint64(Indices.Num() / 3);
	const uint64 PreflightVertexCount = uint64(Vertices.Num());
	if (PreflightSourceBytes > MaxMeshGeometryBytes - 4096ull
		|| PreflightTriangleCount > (MaxMeshGeometryBytes - PreflightSourceBytes - 4096ull) / 192ull
		|| PreflightVertexCount > MaxMeshGeometryBytes / 32ull)
	{
		return false;
	}
	FImpl::FCommand GeometryCommand;
	uint64 GeometrySourceBytes = 0;
	FVector GeometryBoundsMin;
	FVector GeometryBoundsMax;
	if (!ValidateAndCopyGeometry(Vertices, Indices, Winding, GeometryCommand.Vertices,
	                             GeometryCommand.Indices, GeometrySourceBytes, GeometryBoundsMin, GeometryBoundsMax))
	{
		return false;
	}
	const uint64 GeometryReservationBytes = GeometrySourceBytes + 4096ull + PreflightTriangleCount * 192ull
		+ PreflightVertexCount * 32ull;

	FScopeLock Lock(&Impl->QueueMutex);
	if (!Impl->bAccepting || Impl->Commands.Num() >= CommandCapacity || Impl->NextGeometryId == 0
		|| Impl->OutstandingCreationResults >= OutstandingCreationResultCapacity
		|| GeometryReservationBytes > MaxMeshGeometryBytes - Impl->ReservedGeometryBytes)
	{
		return false;
	}
	GeometryCommand.Type = FImpl::ECommandType::RegisterGeometry;
	const FBox3DAsyncGeometryId GeometryId = Impl->NextGeometryId++;
	GeometryCommand.GeometryId = GeometryId;
	Impl->Commands.Add(MoveTemp(GeometryCommand));
	Impl->AcceptedGeometryIds.Add(GeometryId);
	Impl->GeometryByteReservations.Add(GeometryId, GeometryReservationBytes);
	FImpl::FGeometryBounds Bounds;
	Bounds.Min = GeometryBoundsMin;
	Bounds.Max = GeometryBoundsMax;
	Impl->AcceptedGeometryBounds.Add(GeometryId, Bounds);
	Impl->ReservedGeometryBytes += GeometryReservationBytes;
	++Impl->OutstandingCreationResults;
	OutGeometryId = GeometryId;
	Impl->WakeEvent->Trigger();
	return true;
}

bool UBox3DAsyncSubsystem::TryRegisterStaticMeshLOD0(UStaticMesh* Mesh, FBox3DAsyncGeometryId& OutGeometryId)
{
	check(IsInGameThread());
	OutGeometryId = 0;
	if (Mesh == nullptr || !Mesh->bAllowCPUAccess)
	{
		return false;
	}
	const FStaticMeshRenderData* RenderData = Mesh->GetRenderData();
	if (RenderData == nullptr || RenderData->LODResources.IsEmpty())
	{
		return false;
	}
	const FStaticMeshLODResources& LOD = RenderData->LODResources[0];
	const FPositionVertexBuffer& PositionBuffer = LOD.VertexBuffers.PositionVertexBuffer;
	if (PositionBuffer.GetNumVertices() < 3 || PositionBuffer.GetNumVertices() > MaxGeometryVertices
		|| LOD.IndexBuffer.GetNumIndices() < 3 || LOD.IndexBuffer.GetNumIndices() > MaxGeometryIndices
		|| LOD.IndexBuffer.GetNumIndices() % 3 != 0)
	{
		return false;
	}
	TArray<FVector> Vertices;
	Vertices.Reserve(PositionBuffer.GetNumVertices());
	for (uint32 VertexIndex = 0; VertexIndex < PositionBuffer.GetNumVertices(); ++VertexIndex)
	{
		Vertices.Emplace(FVector(PositionBuffer.VertexPosition(VertexIndex)));
	}
	TArray<uint32> RawIndices;
	LOD.IndexBuffer.GetCopy(RawIndices);
	if (RawIndices.Num() < 3 || RawIndices.Num() % 3 != 0)
	{
		return false;
	}
	TArray<int32> Indices;
	Indices.Reserve(RawIndices.Num());
	for (int32 Index = 0; Index < RawIndices.Num(); Index += 3)
	{
		if (RawIndices[Index] > uint32(MAX_int32) || RawIndices[Index + 1] > uint32(MAX_int32)
			|| RawIndices[Index + 2] > uint32(MAX_int32))
		{
			return false;
		}
		Indices.Add(static_cast<int32>(RawIndices[Index]));
		Indices.Add(static_cast<int32>(RawIndices[Index + 2]));
		Indices.Add(static_cast<int32>(RawIndices[Index + 1]));
	}
	return TryRegisterStaticMeshGeometry(Vertices, Indices, EBox3DAsyncMeshWinding::Box3D, OutGeometryId);
}

bool UBox3DAsyncSubsystem::TryRegisterBakedMesh(const UBox3DCollisionData* Data, int32 BodyIndex,
                                                int32 ShapeIndex, FBox3DAsyncGeometryId& OutGeometryId)
{
	check(IsInGameThread());
	OutGeometryId = 0;
	if (Data == nullptr || !Data->Bodies.IsValidIndex(BodyIndex)
		|| !Data->Bodies[BodyIndex].Shapes.IsValidIndex(ShapeIndex))
	{
		return false;
	}
	const FBox3DBakedShape& Shape = Data->Bodies[BodyIndex].Shapes[ShapeIndex];
	if (Shape.Kind != EBox3DBakedShapeKind::Mesh)
	{
		return false;
	}
	if (Shape.Points.Num() > MaxGeometryVertices || Shape.Indices.Num() > MaxGeometryIndices)
	{
		return false;
	}
	TArray<FVector> Vertices;
	Vertices.Reserve(Shape.Points.Num());
	for (const FVector3f& Point : Shape.Points)
	{
		Vertices.Emplace(FVector(Point));
	}
	return TryRegisterStaticMeshGeometry(Vertices, Shape.Indices, EBox3DAsyncMeshWinding::Box3D, OutGeometryId);
}

bool UBox3DAsyncSubsystem::TryCreateStaticMeshCollision(UStaticMesh* Mesh, const FTransform& Transform,
                                                        const FVector& Scale, FBox3DAsyncBodyId& OutBodyId)
{
	return TryCreateAuthoredMeshCollision(Mesh, Transform, Scale, FVector::ZeroVector, false, OutBodyId);
}

bool UBox3DAsyncSubsystem::TryCreateDynamicMeshCollision(UStaticMesh* Mesh, const FTransform& Transform,
                                                         const FVector& Scale, const FVector& InitialVelocity,
                                                         FBox3DAsyncBodyId& OutBodyId)
{
	return TryCreateAuthoredMeshCollision(Mesh, Transform, Scale, InitialVelocity, true, OutBodyId);
}

bool UBox3DAsyncSubsystem::TryCreateAuthoredMeshCollision(UStaticMesh* Mesh, const FTransform& Transform,
                                                          const FVector& Scale, const FVector& InitialVelocity,
                                                          bool bDynamic, FBox3DAsyncBodyId& OutBodyId)
{
	check(IsInGameThread());
	OutBodyId = 0;
	if (!Impl.IsValid() || Mesh == nullptr || !IsValidTransform(Transform) || !IsValidPositiveScale(Scale)
		|| !IsFiniteVector(InitialVelocity) || !IsFiniteNativeVector(Box3D::ToB3(InitialVelocity)))
	{
		return false;
	}
	const UBodySetup* BodySetup = Mesh->GetBodySetup();
	if (BodySetup == nullptr)
	{
		return false;
	}
	FImpl::FCommand Command;
	Command.Type = bDynamic
		               ? FImpl::ECommandType::CreateAuthoredDynamicBody
		               : FImpl::ECommandType::CreateAuthoredStaticBody;
	Command.Transform = Transform;
	Command.Scale = Scale;
	Command.InitialVelocity = InitialVelocity;
	FVector BoundsMin;
	FVector BoundsMax;
	uint64 PayloadBytes = 0;
	if (!Box3DAsyncPrivate::CopyAuthoredCollisionShapes(*BodySetup, Scale, Command.StaticShapes,
	                                                    BoundsMin, BoundsMax, PayloadBytes))
	{
		return false;
	}
	if (!IsValidMeshPlacement(Transform, FVector::OneVector, BoundsMin, BoundsMax))
	{
		return false;
	}

	FScopeLock Lock(&Impl->QueueMutex);
	if (!Impl->bAccepting || Impl->Commands.Num() >= CommandCapacity || Impl->NextBodyId == 0
		|| Impl->OutstandingCreationResults >= OutstandingCreationResultCapacity
		|| PayloadBytes > MaxMeshGeometryBytes - Impl->ReservedStaticShapeBytes)
	{
		return false;
	}
	Command.BodyId = Impl->NextBodyId++;
	Command.PayloadBytes = PayloadBytes;
	Impl->Commands.Add(MoveTemp(Command));
	Impl->AcceptedBodyIds.Add(Impl->NextBodyId - 1);
	Impl->ReservedStaticShapeBytes += PayloadBytes;
	++Impl->OutstandingCreationResults;
	OutBodyId = Impl->NextBodyId - 1;
	Impl->WakeEvent->Trigger();
	return true;
}

bool UBox3DAsyncSubsystem::TryCreateStaticMeshInstance(FBox3DAsyncGeometryId GeometryId,
                                                       const FTransform& Transform, const FVector& Scale,
                                                       FBox3DAsyncBodyId& OutBodyId)
{
	check(IsInGameThread());
	OutBodyId = 0;
	if (!Impl.IsValid() || GeometryId == 0 || !IsValidTransform(Transform) || !IsValidPositiveScale(Scale))
	{
		return false;
	}
	FScopeLock Lock(&Impl->QueueMutex);
	if (!Impl->bAccepting || Impl->Commands.Num() >= CommandCapacity || Impl->NextBodyId == 0
		|| !Impl->AcceptedGeometryIds.Contains(GeometryId)
		|| Impl->OutstandingCreationResults >= OutstandingCreationResultCapacity)
	{
		return false;
	}
	const FImpl::FGeometryBounds* Bounds = Impl->AcceptedGeometryBounds.Find(GeometryId);
	if (Bounds == nullptr || !IsValidMeshPlacement(Transform, Scale, Bounds->Min, Bounds->Max))
	{
		return false;
	}
	FImpl::FCommand& Command = Impl->Commands.AddDefaulted_GetRef();
	Command.Type = FImpl::ECommandType::CreateMeshInstance;
	Command.BodyId = Impl->NextBodyId++;
	Command.GeometryId = GeometryId;
	Command.Transform = Transform;
	Command.Scale = Scale;
	Impl->AcceptedBodyIds.Add(Command.BodyId);
	++Impl->OutstandingCreationResults;
	OutBodyId = Command.BodyId;
	Impl->WakeEvent->Trigger();
	return true;
}

bool UBox3DAsyncSubsystem::TryReleaseMeshGeometry(FBox3DAsyncGeometryId GeometryId)
{
	check(IsInGameThread());
	if (!Impl.IsValid() || GeometryId == 0)
	{
		return false;
	}
	FScopeLock Lock(&Impl->QueueMutex);
	if (!Impl->bAccepting || Impl->Commands.Num() >= CommandCapacity
		|| !Impl->AcceptedGeometryIds.Contains(GeometryId)
		|| Impl->OutstandingCreationResults >= OutstandingCreationResultCapacity)
	{
		return false;
	}
	FImpl::FCommand& Command = Impl->Commands.AddDefaulted_GetRef();
	Command.Type = FImpl::ECommandType::ReleaseGeometry;
	Command.GeometryId = GeometryId;
	Impl->AcceptedGeometryIds.Remove(GeometryId);
	Impl->AcceptedGeometryBounds.Remove(GeometryId);
	++Impl->OutstandingCreationResults;
	Impl->WakeEvent->Trigger();
	return true;
}

bool UBox3DAsyncSubsystem::TryRemoveBody(FBox3DAsyncBodyId BodyId)
{
	check(IsInGameThread());
	if (!Impl.IsValid() || BodyId == 0)
	{
		return false;
	}
	FScopeLock Lock(&Impl->QueueMutex);
	if (!Impl->bAccepting || Impl->Commands.Num() >= CommandCapacity || !Impl->AcceptedBodyIds.Contains(BodyId))
	{
		return false;
	}
	FImpl::FCommand& Command = Impl->Commands.AddDefaulted_GetRef();
	Command.Type = FImpl::ECommandType::Remove;
	Command.BodyId = BodyId;
	Impl->AcceptedBodyIds.Remove(BodyId);
	Impl->WakeEvent->Trigger();
	return true;
}

bool UBox3DAsyncSubsystem::TryRemoveBody(FBox3DAsyncBodyHandle Body)
{
	check(IsInGameThread());
	return Impl.IsValid() && Body.World == Impl->QueryWorld && Body.Id != 0 && TryRemoveBody(Body.Id);
}

#if WITH_DEV_AUTOMATION_TESTS
void UBox3DAsyncSubsystem::SetEventRecordLimitForTest(int32 Limit)
{
	check(IsInGameThread());
	if (!Impl.IsValid()) return;
	FScopeLock Lock(&Impl->QueueMutex);
	// -1 forces a dropped batch on every step, including steps without native
	// records, so tests can hold pressure continuously while polling recovery.
	Impl->EventRecordLimitForTest = FMath::Clamp(Limit, -1, 1024);
}
#endif

void UBox3DAsyncSubsystem::DrainRawEvents(TArray<FBox3DAsyncEventBatch>& OutBatches, bool& bOutGap)
{
	check(IsInGameThread());
	OutBatches.Reset();
	bOutGap = true;
	if (!Impl.IsValid()) return;
	FScopeLock Lock(&Impl->QueueMutex);
	Swap(OutBatches, Impl->EventBatches);
	bOutGap = Impl->bEventGap;
}

bool UBox3DAsyncSubsystem::TryRegisterQueryConsumer(FBox3DAsyncQueryConsumer& OutConsumer)
{
	check(IsInGameThread());
	OutConsumer = {};
	if (!Impl.IsValid()) return false;
	FScopeLock Lock(&Impl->QueueMutex);
	if (!Impl->bAccepting || Impl->QueryConsumers.Num() >= 32 || Impl->NextQueryConsumerId == 0) return false;
	OutConsumer.World = Impl->QueryWorld;
	OutConsumer.Id = Impl->NextQueryConsumerId++;
	Impl->QueryConsumers.Add(OutConsumer.Id);
	return true;
}

void UBox3DAsyncSubsystem::UnregisterQueryConsumer(FBox3DAsyncQueryConsumer Consumer)
{
	check(IsInGameThread());
	if (!Impl.IsValid() || Consumer.World != Impl->QueryWorld) return;
	Impl->ShutdownCallbacks.Remove(Consumer.Id);
	TArray<FImpl::FQueryCallback> RetiredCallbacks;
	{
		FScopeLock Lock(&Impl->QueueMutex);
		if (!Impl->QueryConsumers.Remove(Consumer.Id)) return;
		for (auto It = Impl->Queries.CreateIterator(); It; ++It)
		{
			if (It.Value().ConsumerId != Consumer.Id) continue;
			if (FImpl::FQueryCallback* Callback = Impl->QueryCallbacks.Find(It.Key()))
			{
				RetiredCallbacks.Add(MoveTemp(*Callback));
				Impl->QueryCallbacks.Remove(It.Key());
			}
			if (It.Value().bExecuting) It.Value().bAbandoned = true;
			else It.RemoveCurrent();
		}
	}
}

bool UBox3DAsyncSubsystem::TrySubmitQuery(FBox3DAsyncQueryConsumer Consumer, const FBox3DAsyncLineQuery& Query,
                                           FBox3DAsyncQueryHandle& OutRequest)
{
	return SubmitQuery(Consumer, Query, OutRequest) == EBox3DAsyncQueryAdmission::Accepted;
}

EBox3DAsyncQueryAdmission UBox3DAsyncSubsystem::SubmitQuery(FBox3DAsyncQueryConsumer Consumer,
	const FBox3DAsyncLineQuery& Query, FBox3DAsyncQueryHandle& OutRequest)
{
	check(IsInGameThread());
	OutRequest = {};
	if (!Impl.IsValid()) return EBox3DAsyncQueryAdmission::Unavailable;
	FImpl* const AdmissionImpl = Impl.Get();
	bool bAccepted = false;
	ON_SCOPE_EXIT
	{
		if (!bAccepted)
		{
			FScopeLock Lock(&AdmissionImpl->QueueMutex);
			++AdmissionImpl->ExchangeDiagnostics.RejectedQueries;
		}
	};
	if (Consumer.World != Impl->QueryWorld) return EBox3DAsyncQueryAdmission::WrongWorld;
	if (!IsFiniteVector(Query.Start)
		|| !IsFiniteVector(Query.End)
		|| Query.MaxHits < 1 || Query.MaxHits > 64 || Query.IgnoredBodies.Num() + Query.IgnoredBodyHandles.Num() > 64
		|| Query.ConvexPoints.Num() > B3_MAX_SHAPE_CAST_POINTS
		|| (Query.Mode != EBox3DAsyncLineMode::Any && Query.Mode != EBox3DAsyncLineMode::Closest
			&& Query.Mode != EBox3DAsyncLineMode::Multi)
		|| (Query.Kind != EBox3DAsyncQueryKind::Line && Query.Kind != EBox3DAsyncQueryKind::Sweep
			&& Query.Kind != EBox3DAsyncQueryKind::Overlap && Query.Kind != EBox3DAsyncQueryKind::AabbCandidates)) return EBox3DAsyncQueryAdmission::InvalidInput;
	for (const FBox3DAsyncBodyHandle& Handle : Query.IgnoredBodyHandles)
		if (Handle.World != Impl->QueryWorld) return EBox3DAsyncQueryAdmission::WrongWorld;
		else if (Handle.Id == 0) return EBox3DAsyncQueryAdmission::InvalidInput;
	const bool bCast = Query.Kind == EBox3DAsyncQueryKind::Line || Query.Kind == EBox3DAsyncQueryKind::Sweep;
	if (!bCast && Query.Mode == EBox3DAsyncLineMode::Closest) return EBox3DAsyncQueryAdmission::Unsupported;
	if (bCast && Query.Start.Equals(Query.End, KINDA_SMALL_NUMBER)) return EBox3DAsyncQueryAdmission::InvalidInput;
	const b3Vec3 Delta = Box3D::ToB3(bCast ? Query.End - Query.Start : FVector::ZeroVector);
	if (!IsFiniteNativePosition(Box3D::ToB3Pos(Query.Start)) || !IsFiniteNativePosition(Box3D::ToB3Pos(Query.End))
		|| !IsFiniteNativeVector(Delta) || (bCast && Delta.x == 0.0f && Delta.y == 0.0f && Delta.z == 0.0f)) return EBox3DAsyncQueryAdmission::InvalidInput;
	// Keep broad-phase and GJK arithmetic well inside native float range, including
	// the sums and squared distances computed by native traversal. This still
	// admits coordinates and shapes thousands of kilometers across.
	constexpr double MaxQueryCm = 1.0e9;
	auto InQueryRange = [](const FVector& Value)
	{
		return FMath::Abs(Value.X) <= MaxQueryCm && FMath::Abs(Value.Y) <= MaxQueryCm
			&& FMath::Abs(Value.Z) <= MaxQueryCm;
	};
	if (!InQueryRange(Query.Start) || (Query.Kind != EBox3DAsyncQueryKind::Overlap && !InQueryRange(Query.End))) return EBox3DAsyncQueryAdmission::InvalidInput;
	if (Query.Kind == EBox3DAsyncQueryKind::AabbCandidates)
	{
		if (Query.Start.X > Query.End.X || Query.Start.Y > Query.End.Y || Query.Start.Z > Query.End.Z
			|| !IsFiniteNativeVector(Box3D::ToB3(Query.Start)) || !IsFiniteNativeVector(Box3D::ToB3(Query.End))) return EBox3DAsyncQueryAdmission::InvalidInput;
	}
	if (Query.Kind == EBox3DAsyncQueryKind::Sweep || Query.Kind == EBox3DAsyncQueryKind::Overlap)
	{
		if (!Query.Rotation.IsNormalized() || Query.Rotation.ContainsNaN()) return EBox3DAsyncQueryAdmission::InvalidInput;
		const auto ValidOffset = [&Query, &InQueryRange](const FVector& Offset)
		{
			if (!IsFiniteVector(Offset)) return false;
			const FVector Rotated = Query.Rotation.RotateVector(Offset);
			return IsFiniteVector(Rotated) && InQueryRange(Rotated)
				&& IsFiniteNativeVector(Box3D::ToB3(Rotated))
				&& IsFiniteNativePosition(Box3D::ToB3Pos(Query.Start + Rotated))
				&& (Query.Kind != EBox3DAsyncQueryKind::Sweep
					|| IsFiniteNativePosition(Box3D::ToB3Pos(Query.End + Rotated)));
		};
		const float Radius = float(double(Query.Radius) * Box3D::UEToMeters);
		switch (Query.Shape)
		{
		case EBox3DAsyncQueryShape::Sphere:
			if (!FMath::IsFinite(Radius) || Radius <= 0.0f || Query.Radius > MaxQueryCm) return EBox3DAsyncQueryAdmission::InvalidInput;
			break;
		case EBox3DAsyncQueryShape::Capsule:
			if (!FMath::IsFinite(Radius) || Radius <= 0.0f || Query.Radius > MaxQueryCm || !FMath::IsFinite(Query.HalfHeight)
				|| Query.HalfHeight < Query.Radius || !ValidOffset(FVector(0, 0, Query.HalfHeight - Query.Radius))) return EBox3DAsyncQueryAdmission::InvalidInput;
			break;
		case EBox3DAsyncQueryShape::Box:
			if (!IsFiniteVector(Query.HalfExtents) || Query.HalfExtents.GetMin() <= 0.0) return EBox3DAsyncQueryAdmission::InvalidInput;
			for (int32 Corner = 0; Corner < 8; ++Corner)
				if (!ValidOffset(FVector((Corner & 1 ? 1.0 : -1.0) * Query.HalfExtents.X,
				                         (Corner & 2 ? 1.0 : -1.0) * Query.HalfExtents.Y,
				                         (Corner & 4 ? 1.0 : -1.0) * Query.HalfExtents.Z))) return EBox3DAsyncQueryAdmission::InvalidInput;
			break;
		case EBox3DAsyncQueryShape::Convex:
		{
			if (Query.ConvexPoints.Num() < 4 || Query.ConvexPoints.Num() > B3_MAX_SHAPE_CAST_POINTS) return EBox3DAsyncQueryAdmission::InvalidInput;
			for (const FVector& Point : Query.ConvexPoints) if (!ValidOffset(Point)) return EBox3DAsyncQueryAdmission::InvalidInput;
			// A nonzero tetrahedron is required; coplanar point clouds do not define a 3D solid.
			const FVector Base = Query.ConvexPoints[0];
			bool bVolume = false;
			for (int32 I = 1; I < Query.ConvexPoints.Num() && !bVolume; ++I)
				for (int32 J = I + 1; J < Query.ConvexPoints.Num() && !bVolume; ++J)
					for (int32 K = J + 1; K < Query.ConvexPoints.Num() && !bVolume; ++K)
					{
						const double Volume = FVector::DotProduct(FVector::CrossProduct(Query.ConvexPoints[I] - Base,
							Query.ConvexPoints[J] - Base), Query.ConvexPoints[K] - Base);
						bVolume = FMath::IsFinite(Volume) && FMath::Abs(Volume) > UE_SMALL_NUMBER;
					}
			if (!bVolume) return EBox3DAsyncQueryAdmission::InvalidInput;
			break;
		}
		default: return EBox3DAsyncQueryAdmission::InvalidInput;
		}
	}
	FScopeLock Lock(&Impl->QueueMutex);
	if (!Impl->bAccepting) return EBox3DAsyncQueryAdmission::Unavailable;
	if (!Impl->QueryConsumers.Contains(Consumer.Id)) return EBox3DAsyncQueryAdmission::InvalidConsumer;
	if (Impl->Queries.Num() >= QueryCapacity || Impl->NextQueryId == 0) return EBox3DAsyncQueryAdmission::Capacity;
	// Charge worst-case copied inputs and retained/worker hit storage for each
	// accepted request until it is consumed, abandoned, or fully released. The
	// reservation includes fixed per-record overhead and an in-flight copy.
	auto ReservedBytes = [](const FBox3DAsyncLineQuery& Desc) -> uint64
	{
		return 2ull * sizeof(FImpl::FQueryRecord) + 256ull
			+ 2ull * uint64(Desc.IgnoredBodies.Num()) * sizeof(FBox3DAsyncBodyId)
			+ 2ull * uint64(Desc.IgnoredBodyHandles.Num()) * sizeof(FBox3DAsyncBodyHandle)
			+ 2ull * uint64(Desc.ConvexPoints.Num()) * sizeof(FVector)
			+ uint64(Desc.MaxHits) * (3ull * sizeof(FBox3DAsyncLineHit) + 64ull);
	};
	uint64 WorldBytes = 0;
	uint64 ConsumerBytes = 0;
	int32 ConsumerCount = 0;
	for (const TPair<uint64, FImpl::FQueryRecord>& Pair : Impl->Queries)
	{
		const uint64 Bytes = Pair.Value.ReservedBytes;
		WorldBytes += Bytes;
		if (Pair.Value.ConsumerId == Consumer.Id)
		{
			ConsumerBytes += Bytes;
			++ConsumerCount;
		}
	}
	const uint64 RequestedBytes = ReservedBytes(Query);
	if (ConsumerCount >= QueryCapacityPerConsumer || WorldBytes > QueryReservedBytes
		|| ConsumerBytes > QueryReservedBytesPerConsumer || RequestedBytes > QueryReservedBytes - WorldBytes
		|| RequestedBytes > QueryReservedBytesPerConsumer - ConsumerBytes) return EBox3DAsyncQueryAdmission::Capacity;
	OutRequest.World = Impl->QueryWorld;
	OutRequest.Id = Impl->NextQueryId++;
	FImpl::FQueryRecord& Record = Impl->Queries.Add(OutRequest.Id);
	Record.ConsumerId = Consumer.Id;
	Record.Query = Query;
	Record.ReservedBytes = RequestedBytes;
	Record.SubmittedSeconds = FPlatformTime::Seconds();
	Record.Result.Request = OutRequest;
	Record.Result.UserTag = Query.UserTag;
	Record.Result.Kind = Query.Kind;
	++Impl->ExchangeDiagnostics.AcceptedQueries;
	Impl->ExchangeDiagnostics.PeakOutstandingQueries = FMath::Max(Impl->ExchangeDiagnostics.PeakOutstandingQueries, Impl->Queries.Num());
	Impl->ExchangeDiagnostics.PeakQueryReservedBytes = FMath::Max(Impl->ExchangeDiagnostics.PeakQueryReservedBytes, WorldBytes + RequestedBytes);
	bAccepted = true;
	Impl->WakeEvent->Trigger();
	return EBox3DAsyncQueryAdmission::Accepted;
}

bool UBox3DAsyncSubsystem::TryCancelQuery(FBox3DAsyncQueryConsumer Consumer, FBox3DAsyncQueryHandle Request)
{
	check(IsInGameThread());
	if (!Impl.IsValid() || Consumer.World != Impl->QueryWorld || Request.World != Impl->QueryWorld) return false;
	FScopeLock Lock(&Impl->QueueMutex);
	FImpl::FQueryRecord* Record = Impl->Queries.Find(Request.Id);
	if (!Record || Record->ConsumerId != Consumer.Id || Record->bCommitted || Record->bCancelled
		|| !Impl->QueryConsumers.Contains(Consumer.Id)) return false;
	Record->bCancelled = true;
	Record->bCommitted = true;
	if (!Record->bExecuting)
	{
		Record->Result.Status = EBox3DAsyncQueryStatus::Cancelled;
		Record->Result.Hits.Reset();
		Record->bReady = true;
	}
	return true;
}

bool UBox3DAsyncSubsystem::TryTakeQueryResult(FBox3DAsyncQueryConsumer Consumer, FBox3DAsyncQueryHandle Request,
                                               FBox3DAsyncQueryResult& OutResult)
{
	check(IsInGameThread());
	if (!Impl.IsValid() || Consumer.World != Impl->QueryWorld || Request.World != Impl->QueryWorld) return false;
	FScopeLock Lock(&Impl->QueueMutex);
	FImpl::FQueryRecord* Record = Impl->Queries.Find(Request.Id);
	if (!Record || Record->ConsumerId != Consumer.Id || !Record->bReady || Record->bExecuting
		|| Impl->QueryCallbacks.Contains(Request.Id) || !Impl->QueryConsumers.Contains(Consumer.Id)) return false;
	Record->bCommitted = true;
	Impl->RecordQueryDelivery(*Record, FPlatformTime::Seconds());
	OutResult = MoveTemp(Record->Result);
	Impl->Queries.Remove(Request.Id);
	return true;
}

bool UBox3DAsyncSubsystem::TrySetQueryCallback(FBox3DAsyncQueryConsumer Consumer, FBox3DAsyncQueryHandle Request,
                                                UObject* Owner, TFunction<void(const FBox3DAsyncQueryResult&)> Callback)
{
	check(IsInGameThread());
	if (!Impl.IsValid() || Impl->bClosingQueries || !IsValid(Owner) || !Callback
		|| Consumer.World != Impl->QueryWorld || Request.World != Impl->QueryWorld) return false;
	FScopeLock Lock(&Impl->QueueMutex);
	FImpl::FQueryRecord* Record = Impl->Queries.Find(Request.Id);
	if (!Record || Record->ConsumerId != Consumer.Id || Record->bCommitted
		|| !Impl->QueryConsumers.Contains(Consumer.Id) || Impl->QueryCallbacks.Contains(Request.Id)) return false;
	FImpl::FQueryCallback& Entry = Impl->QueryCallbacks.Add(Request.Id);
	Entry.Owner = Owner;
	Entry.Callback = MoveTemp(Callback);
	return true;
}

bool UBox3DAsyncSubsystem::SetQueryShutdownCallback(FBox3DAsyncQueryConsumer Consumer, UObject* Owner,
                                                     TFunction<void()> Callback)
{
	check(IsInGameThread());
	if (!Impl.IsValid() || Impl->bClosingQueries || Consumer.World != Impl->QueryWorld
		|| !IsValid(Owner) || !Callback || !Impl->QueryConsumers.Contains(Consumer.Id)) return false;
	FImpl::FShutdownCallback& Entry = Impl->ShutdownCallbacks.FindOrAdd(Consumer.Id);
	Entry.Owner = Owner;
	Entry.Callback = MoveTemp(Callback);
	return true;
}

void UBox3DAsyncSubsystem::PumpQueryCallbacks()
{
	DispatchQueryCallbacks(false);
}

#if WITH_DEV_AUTOMATION_TESTS
void UBox3DAsyncSubsystem::SetQueryTestGate(int32 Gate)
{
	check(IsInGameThread());
	if (Impl.IsValid())
	{
		Impl->QueryTestGate.Store(Gate);
		if (Impl->WakeEvent) Impl->WakeEvent->Trigger();
	}
}

int32 UBox3DAsyncSubsystem::GetQueryTestGateReached() const
{
	check(IsInGameThread());
	return Impl.IsValid() ? Impl->QueryTestGateReached.Load() : 0;
}

int32 UBox3DAsyncSubsystem::GetQueryTestPhase(FBox3DAsyncQueryHandle Request) const
{
	check(IsInGameThread());
	if (!Impl.IsValid() || Request.World != Impl->QueryWorld) return -1;
	FScopeLock Lock(&Impl->QueueMutex);
	const FImpl::FQueryRecord* Record = Impl->Queries.Find(Request.Id);
	return !Record ? -1 : Record->bExecuting ? 1 : Record->bReady ? 2 : 0;
}
#endif

void UBox3DAsyncSubsystem::DispatchQueryCallbacks(bool bShutdown)
{
	check(IsInGameThread());
	if (!Impl.IsValid() || (Impl->bDispatchingQueries && !bShutdown)) return;
	FImpl* const DispatchImpl = Impl.Get();
	const bool bWasDispatching = DispatchImpl->bDispatchingQueries;
	DispatchImpl->bDispatchingQueries = true;
	TArray<uint64> CallbackRequests;
	{
		FScopeLock Lock(&Impl->QueueMutex);
		for (auto& Pair : Impl->Queries)
		{
			FImpl::FQueryRecord& Record = Pair.Value;
			if (!Record.bReady || Record.bExecuting) continue;
			Record.bCommitted = true;
			if (Impl->QueryCallbacks.Contains(Pair.Key)) CallbackRequests.Add(Pair.Key);
		}
	}
	CallbackRequests.Sort();
	for (uint64 RequestId : CallbackRequests)
	{
		FBox3DAsyncQueryResult Result;
		FImpl::FQueryCallback Entry;
		{
			FScopeLock Lock(&Impl->QueueMutex);
			FImpl::FQueryRecord* Record = Impl->Queries.Find(RequestId);
			FImpl::FQueryCallback* Registered = Impl->QueryCallbacks.Find(RequestId);
			if (!Record || !Registered || !Record->bReady || Record->bExecuting
				|| !Impl->QueryConsumers.Contains(Record->ConsumerId)) continue;
			if (Registered->Owner.IsValid()) Impl->RecordQueryDelivery(*Record, FPlatformTime::Seconds());
			Result = MoveTemp(Record->Result);
			Entry = MoveTemp(*Registered);
			Impl->QueryCallbacks.Remove(RequestId);
			Impl->Queries.Remove(RequestId);
		}
		if (Entry.Owner.IsValid() && Entry.Callback) Entry.Callback(Result);
		// A handler may tear down the world and destroy the subsystem state.
		if (Impl.Get() != DispatchImpl) return;
	}
	DispatchImpl->bDispatchingQueries = bWasDispatching;
}

bool UBox3DAsyncSubsystem::TrySubmitLineTrace(const FVector& Start, const FVector& End,
                                              FBox3DAsyncTraceRequestId& OutRequestId)
{
	check(IsInGameThread());
	OutRequestId = 0;
	const b3Vec3 NativeDelta = Box3D::ToB3(End - Start);
	const b3Pos NativeStart = Box3D::ToB3Pos(Start);
	const b3Pos NativeEnd = Box3D::ToB3Pos(End);
	if (!Impl.IsValid() || !IsFiniteVector(Start) || !IsFiniteVector(End) || Start.Equals(End, KINDA_SMALL_NUMBER))
	{
		return false;
	}
	if (!IsFiniteNativePosition(NativeStart) || !IsFiniteNativePosition(NativeEnd) || !IsFiniteNativeVector(NativeDelta)
		|| (NativeDelta.x == 0.0f && NativeDelta.y == 0.0f && NativeDelta.z == 0.0f))
	{
		return false;
	}
	FScopeLock Lock(&Impl->QueueMutex);
	const int32 Outstanding = Impl->TraceRequests.Num() + Impl->InFlightTraces + Impl->TraceResults.Num();
	if (!Impl->bAccepting || Outstanding >= OutstandingTraceCapacity || Impl->NextRequestId == 0)
	{
		return false;
	}
	FImpl::FTraceRequest& Request = Impl->TraceRequests.AddDefaulted_GetRef();
	Request.RequestId = Impl->NextRequestId++;
	Request.Start = Start;
	Request.End = End;
	OutRequestId = Request.RequestId;
	Impl->WakeEvent->Trigger();
	return true;
}

void UBox3DAsyncSubsystem::DrainTraceResults(TArray<FBox3DAsyncTraceResult>& OutResults)
{
	check(IsInGameThread());
	OutResults.Reset();
	if (!Impl.IsValid())
	{
		return;
	}
	FScopeLock Lock(&Impl->QueueMutex);
	Swap(OutResults, Impl->TraceResults);
}

void UBox3DAsyncSubsystem::DrainCreationResults(TArray<FBox3DAsyncCreationResult>& OutResults)
{
	check(IsInGameThread());
	OutResults.Reset();
	if (!Impl.IsValid())
	{
		return;
	}
	FScopeLock Lock(&Impl->QueueMutex);
	Swap(OutResults, Impl->CreationResults);
	Impl->OutstandingCreationResults = FMath::Max(0, Impl->OutstandingCreationResults - OutResults.Num());
}

void UBox3DAsyncSubsystem::SamplePoses(TArray<FBox3DAsyncPose>& OutPoses, double* OutSampleTimeSeconds) const
{
	check(IsInGameThread());
	OutPoses.Reset();
	if (!Impl.IsValid())
	{
		if (OutSampleTimeSeconds != nullptr)
		{
			*OutSampleTimeSeconds = 0.0;
		}
		return;
	}
	TArray<TSharedPtr<const FImpl::FSnapshot, ESPMode::ThreadSafe>> LocalSnapshots;
	{
		FScopeLock Lock(&Impl->SnapshotMutex);
		LocalSnapshots = Impl->Snapshots;
	}
	if (LocalSnapshots.IsEmpty())
	{
		if (OutSampleTimeSeconds != nullptr)
		{
			*OutSampleTimeSeconds = FPlatformTime::Seconds() - 2.0 * Impl->FixedTimeStep;
		}
		return;
	}
	const double TargetTime = FPlatformTime::Seconds() - 2.0 * Impl->FixedTimeStep;
	if (OutSampleTimeSeconds != nullptr)
	{
		*OutSampleTimeSeconds = TargetTime;
	}
	const FImpl::FSnapshot* Before = LocalSnapshots[0].Get();
	const FImpl::FSnapshot* After = nullptr;
	for (const TSharedPtr<const FImpl::FSnapshot, ESPMode::ThreadSafe>& SnapshotPtr : LocalSnapshots)
	{
		const FImpl::FSnapshot& Snapshot = *SnapshotPtr;
		if (Snapshot.ScheduledTime <= TargetTime)
		{
			Before = &Snapshot;
		}
		else
		{
			After = &Snapshot;
			break;
		}
	}
	if (After == nullptr || TargetTime <= LocalSnapshots[0]->ScheduledTime)
	{
		OutPoses = Before->Poses;
		return;
	}
	const double Duration = After->ScheduledTime - Before->ScheduledTime;
	const float Alpha = Duration > 0.0
		                    ? float(FMath::Clamp((TargetTime - Before->ScheduledTime) / Duration, 0.0, 1.0))
		                    : 0.0f;
	int32 EarlierIndex = 0;
	int32 LaterIndex = 0;
	while (EarlierIndex < Before->Poses.Num() && LaterIndex < After->Poses.Num())
	{
		const FBox3DAsyncPose& Earlier = Before->Poses[EarlierIndex];
		const FBox3DAsyncPose& Later = After->Poses[LaterIndex];
		if (Earlier.BodyId == Later.BodyId)
		{
			FBox3DAsyncPose& Pose = OutPoses.Add_GetRef(Earlier);
			Pose.Transform.SetLocation(FMath::Lerp(Earlier.Transform.GetLocation(), Later.Transform.GetLocation(),
			                                       Alpha));
			Pose.Transform.SetRotation(
				FQuat::Slerp(Earlier.Transform.GetRotation(), Later.Transform.GetRotation(), Alpha).GetNormalized());
			++EarlierIndex;
			++LaterIndex;
		}
		else if (Earlier.BodyId < Later.BodyId)
		{
			if (Alpha < 1.0f)
			{
				OutPoses.Add(Earlier);
			}
			++EarlierIndex;
		}
		else
		{
			if (Alpha >= 1.0f)
			{
				OutPoses.Add(Later);
			}
			++LaterIndex;
		}
	}
	for (; EarlierIndex < Before->Poses.Num(); ++EarlierIndex)
	{
		if (Alpha < 1.0f)
		{
			OutPoses.Add(Before->Poses[EarlierIndex]);
		}
	}
	for (; LaterIndex < After->Poses.Num(); ++LaterIndex)
	{
		if (Alpha >= 1.0f)
		{
			OutPoses.Add(After->Poses[LaterIndex]);
		}
	}
}

uint64 UBox3DAsyncSubsystem::GetCompletedStepCount() const
{
	check(IsInGameThread());
	if (!Impl.IsValid())
	{
		return 0;
	}
	FScopeLock Lock(&Impl->DiagnosticsMutex);
	return Impl->CompletedSteps;
}

FBox3DAsyncDiagnostics UBox3DAsyncSubsystem::GetDiagnostics() const
{
	check(IsInGameThread());
	FBox3DAsyncDiagnostics Result;
	if (!Impl.IsValid())
	{
		return Result;
	}
	{
		FScopeLock Lock(&Impl->DiagnosticsMutex);
		Result.CompletedSteps = Impl->CompletedSteps;
		Result.NativeCreationFailures = Impl->NativeCreationFailures;
		Result.DeadlineMisses = Impl->DeadlineMisses;
		Result.ClockRebases = Impl->ClockRebases;
		Result.LatestScheduledTimeSeconds = Impl->LatestScheduledTime;
		Result.LatestCompletionTimeSeconds = Impl->LatestCompletionTime;
	}
	FScopeLock Lock(&Impl->QueueMutex);
	Result.QueuedCommands = Impl->Commands.Num();
	Result.OutstandingTraces = Impl->TraceRequests.Num() + Impl->InFlightTraces + Impl->TraceResults.Num();
	Result.OutstandingCreationResults = Impl->OutstandingCreationResults;
	Result.ReservedMeshGeometryBytes = Impl->ReservedGeometryBytes;
	Result.RegisteredMeshGeometries = Impl->AcceptedGeometryIds.Num();
	const FBox3DAsyncDiagnostics& Exchange = Impl->ExchangeDiagnostics;
	Result.AcceptedQueries = Exchange.AcceptedQueries;
	Result.RejectedQueries = Exchange.RejectedQueries;
	Result.TruncatedQueries = Exchange.TruncatedQueries;
	Result.OutstandingQueries = Impl->Queries.Num();
	Result.PeakOutstandingQueries = Exchange.PeakOutstandingQueries;
	for (const auto& Pair : Impl->Queries) Result.QueryReservedBytes += Pair.Value.ReservedBytes;
	Result.PeakQueryReservedBytes = Exchange.PeakQueryReservedBytes;
	Result.ExecutedQueries = Exchange.ExecutedQueries;
	Result.TotalQueryExecutionSeconds = Exchange.TotalQueryExecutionSeconds;
	Result.MaxQueryExecutionSeconds = Exchange.MaxQueryExecutionSeconds;
	Result.DeliveredQueryResults = Exchange.DeliveredQueryResults;
	Result.TotalQueryLatencySeconds = Exchange.TotalQueryLatencySeconds;
	Result.MaxQueryLatencySeconds = Exchange.MaxQueryLatencySeconds;
	Result.EventIngressBatches = Impl->EventIngress.Num();
	Result.PeakEventIngressBatches = Exchange.PeakEventIngressBatches;
	Result.EventIngressBytes = FImpl::EventArrayBytes(Impl->EventIngress);
	Result.PeakEventIngressBytes = Exchange.PeakEventIngressBytes;
	Result.EventHistoryBatches = Impl->EventHistory.Num();
	Result.PeakEventHistoryBatches = Exchange.PeakEventHistoryBatches;
	Result.EventHistoryBytes = FImpl::EventArrayBytes(Impl->EventHistory);
	Result.PeakEventHistoryBytes = Exchange.PeakEventHistoryBytes;
	Result.EventBaselineBytes = Impl->EventBaseline.Events.GetAllocatedSize();
	Result.DroppedEventBatches = Exchange.DroppedEventBatches;
	Result.DroppedEventRecords = Exchange.DroppedEventRecords;
	Result.EventResyncs = Exchange.EventResyncs;
	Result.EventBaselineUnavailable = Exchange.EventBaselineUnavailable;
	return Result;
}
