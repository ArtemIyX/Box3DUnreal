#include "Box3DAsyncSubsystemImpl.h"
#include "Simulation/Box3DConversion.h"

namespace
{
	constexpr double MaxActorlessCoordinate = 1.0e9;

	bool ValidPoint(const FVector& P)
	{
		return FMath::IsFinite(P.X) && FMath::IsFinite(P.Y) && FMath::IsFinite(P.Z)
			&& P.GetAbsMax() <= MaxActorlessCoordinate;
	}

	bool ValidTransform(const FTransform& T)
	{
		return ValidPoint(T.GetLocation()) && T.GetRotation().IsNormalized()
			&& T.GetScale3D().Equals(FVector::OneVector, KINDA_SMALL_NUMBER);
	}

	bool CopyBodyShapes(const FBox3DAsyncBodyDesc& Desc, TArray<Box3DAsyncPrivate::FCopiedBodyShape>& Out, uint64& Bytes)
	{
		if (!ValidTransform(Desc.Transform) || !ValidPoint(Desc.InitialVelocity)
			|| (Desc.Motion != EBox3DAsyncBodyMotion::Static && Desc.Motion != EBox3DAsyncBodyMotion::Dynamic)
			|| Desc.Shapes.IsEmpty() || Desc.Shapes.Num() > 256) return false;
		Bytes = 0;
		Out.Reserve(Desc.Shapes.Num());
		for (const FBox3DAsyncBodyShapeDesc& Input : Desc.Shapes)
		{
			if (!ValidTransform(Input.LocalTransform) || !FMath::IsFinite(Input.Options.Density)
				|| Input.Options.Density < 0 || Input.ConvexPoints.Num() > 64) return false;
			Box3DAsyncPrivate::FCopiedBodyShape Shape;
			Shape.Options = Input.Options;
			Shape.SubshapeKey = Input.SubshapeKey;
			const FVector Center = Input.LocalTransform.GetLocation();
			auto ValidWorldPoint = [&Desc](const FVector& P, double Radius)
			{
				const FVector World = Desc.Transform.TransformPositionNoScale(P);
				return ValidPoint(P) && ValidPoint(World) && FMath::IsFinite(Radius)
					&& World.GetAbsMax() + Radius <= MaxActorlessCoordinate;
			};
			if (Input.Kind == EBox3DAsyncBodyShapeKind::Sphere || Input.Kind == EBox3DAsyncBodyShapeKind::Capsule)
			{
				if (!FMath::IsFinite(Input.Radius) || Input.Radius <= 0.0f
					|| !FMath::IsFinite(Input.HalfHeight) || (Input.Kind == EBox3DAsyncBodyShapeKind::Capsule && Input.HalfHeight < Input.Radius)) return false;
				const float Radius = float(Input.Radius * Box3D::UEToMeters);
				if (!FMath::IsFinite(Radius) || Radius <= 0.0f) return false;
				const double HalfSegment = Input.Kind == EBox3DAsyncBodyShapeKind::Capsule ? Input.HalfHeight - Input.Radius : 0.0;
				const FVector Axis = Input.LocalTransform.GetRotation().RotateVector(FVector(0, 0, HalfSegment));
				if (!ValidWorldPoint(Center - Axis, Input.Radius) || !ValidWorldPoint(Center + Axis, Input.Radius)) return false;
				if (HalfSegment == 0.0)
				{
					Shape.Type = Box3DAsyncPrivate::EStaticShapeType::Sphere;
					Shape.Sphere = {Box3D::ToB3(Center), Radius};
				}
				else
				{
					Shape.Type = Box3DAsyncPrivate::EStaticShapeType::Capsule;
					Shape.Capsule = {Box3D::ToB3(Center - Axis), Box3D::ToB3(Center + Axis), Radius};
				}
			}
			else if (Input.Kind == EBox3DAsyncBodyShapeKind::Box || Input.Kind == EBox3DAsyncBodyShapeKind::Convex)
			{
				if (Input.Kind == EBox3DAsyncBodyShapeKind::Box && (!ValidPoint(Input.HalfExtents)
					|| Input.HalfExtents.X <= 0 || Input.HalfExtents.Y <= 0 || Input.HalfExtents.Z <= 0)) return false;
				const int32 Count = Input.Kind == EBox3DAsyncBodyShapeKind::Box ? 8 : Input.ConvexPoints.Num();
				if (Count < 4 || Count > 64) return false;
				Shape.Type = Box3DAsyncPrivate::EStaticShapeType::Hull;
				Shape.HullPoints.Reserve(Count);
				for (int32 Index = 0; Index < Count; ++Index)
				{
					const FVector Point = Input.Kind == EBox3DAsyncBodyShapeKind::Box
						? FVector((Index & 1) ? Input.HalfExtents.X : -Input.HalfExtents.X,
						          (Index & 2) ? Input.HalfExtents.Y : -Input.HalfExtents.Y,
						          (Index & 4) ? Input.HalfExtents.Z : -Input.HalfExtents.Z)
						: Input.ConvexPoints[Index];
					if (!ValidPoint(Point)) return false;
					const FVector Local = Input.LocalTransform.TransformPositionNoScale(Point);
					if (!ValidWorldPoint(Local, 0)) return false;
					Shape.HullPoints.Add(Box3D::ToB3(Local));
				}
			}
			else return false;
			Bytes += sizeof(Box3DAsyncPrivate::FCopiedBodyShape) + uint64(Shape.HullPoints.Num()) * sizeof(b3Vec3);
			Out.Add(MoveTemp(Shape));
		}
		return true;
	}
}

bool UBox3DAsyncSubsystem::TryRegisterCreationConsumer(FBox3DAsyncCreationConsumer& OutConsumer)
{
	check(IsInGameThread());
	OutConsumer = {};
	if (!Impl.IsValid()) return false;
	FScopeLock Lock(&Impl->QueueMutex);
	if (!Impl->bAccepting || Impl->NextCreationConsumerId == 0 || Impl->OwnedCreationResults.Num() >= 32) return false;
	OutConsumer = {Impl->QueryWorld, Impl->NextCreationConsumerId++};
	Impl->OwnedCreationResults.Add(OutConsumer.Id);
	return true;
}

void UBox3DAsyncSubsystem::UnregisterCreationConsumer(FBox3DAsyncCreationConsumer Consumer)
{
	check(IsInGameThread());
	if (!Impl.IsValid() || Consumer.World != Impl->QueryWorld) return;
	FScopeLock Lock(&Impl->QueueMutex);
	if (auto* Results = Impl->OwnedCreationResults.Find(Consumer.Id))
	{
		Impl->OutstandingCreationResults -= Results->Num();
		Impl->OwnedCreationResults.Remove(Consumer.Id);
	}
}

bool UBox3DAsyncSubsystem::TryCreateBody(FBox3DAsyncCreationConsumer Consumer, const FBox3DAsyncBodyDesc& Desc,
	FBox3DAsyncBodyHandle& OutBody)
{
	check(IsInGameThread());
	OutBody = {};
	if (!Impl.IsValid() || Consumer.World != Impl->QueryWorld) return false;
	FImpl::FCommand Command;
	uint64 PayloadBytes = 0;
	if (!CopyBodyShapes(Desc, Command.BodyShapes, PayloadBytes)) return false;
	Command.Type = FImpl::ECommandType::CreateActorlessBody;
	Command.bDynamic = Desc.Motion == EBox3DAsyncBodyMotion::Dynamic;
	Command.Transform = Desc.Transform;
	Command.InitialVelocity = Desc.InitialVelocity;
	Command.PayloadBytes = PayloadBytes;
	Command.ReservedShapeIdentities = Command.BodyShapes.Num();
	FScopeLock Lock(&Impl->QueueMutex);
	const auto* Results = Impl->OwnedCreationResults.Find(Consumer.Id);
	if (!Impl->bAccepting || !Results || Impl->Commands.Num() >= CommandCapacity || Impl->NextBodyId == 0
		|| Impl->OutstandingCreationResults >= OutstandingCreationResultCapacity
		|| Command.ReservedShapeIdentities > 8192 - Impl->LiveShapeIdentities - Impl->ReservedShapeIdentities
		|| PayloadBytes > MaxMeshGeometryBytes - Impl->ReservedStaticShapeBytes) return false;
	int32 Pending = Results->Num();
	for (const auto& Pair : Impl->CreationOwners) if (Pair.Value == Consumer.Id) ++Pending;
	if (Pending >= 16) return false;
	Command.BodyId = Impl->NextBodyId++;
	OutBody = {Impl->QueryWorld, Command.BodyId};
	Impl->AcceptedBodyIds.Add(Command.BodyId);
	Impl->CreationOwners.Add(Command.BodyId, Consumer.Id);
	Impl->ReservedShapeIdentities += Command.ReservedShapeIdentities;
	Impl->ReservedStaticShapeBytes += PayloadBytes;
	++Impl->OutstandingCreationResults;
	Impl->Commands.Add(MoveTemp(Command));
	Impl->WakeEvent->Trigger();
	return true;
}

bool UBox3DAsyncSubsystem::TryTakeCreationResult(FBox3DAsyncCreationConsumer Consumer,
	FBox3DAsyncBodyHandle Body, FBox3DAsyncCreationResult& OutResult)
{
	check(IsInGameThread());
	if (!Impl.IsValid() || Consumer.World != Impl->QueryWorld || Body.World != Impl->QueryWorld) return false;
	FScopeLock Lock(&Impl->QueueMutex);
	auto* Results = Impl->OwnedCreationResults.Find(Consumer.Id);
	if (!Results) return false;
	FBox3DAsyncCreationResult* Result = Results->Find(Body.Id);
	if (!Result) return false;
	OutResult = MoveTemp(*Result);
	Results->Remove(Body.Id);
	--Impl->OutstandingCreationResults;
	return true;
}
