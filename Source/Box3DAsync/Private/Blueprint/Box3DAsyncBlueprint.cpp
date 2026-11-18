#include "Blueprint/Box3DAsyncBlueprint.h"

#include "Engine/Engine.h"
#include "Engine/World.h"

namespace
{
	int64 SignedBits(uint64 Value) { int64 Result; FMemory::Memcpy(&Result, &Value, sizeof(Result)); return Result; }
	uint64 UnsignedBits(int64 Value) { uint64 Result; FMemory::Memcpy(&Result, &Value, sizeof(Result)); return Result; }
	FBox3DBPIdentity Identity(const FGuid& World, uint64 Id)
	{
		FBox3DBPIdentity Result;
		Result.World = World; Result.Id = SignedBits(Id);
		return Result;
	}
	FBox3DBPSample Sample(const FBox3DAsyncSampleStamp& Value)
	{
		FBox3DBPSample Result;
		Result.Step = SignedBits(Value.Step);
		Result.ScheduledMonotonicSeconds = Value.ScheduledMonotonicSeconds;
		Result.ClockEpoch = SignedBits(Value.ClockEpoch);
		return Result;
	}
	UBox3DAsyncSubsystem* ResolveAsync(UObject* Context)
	{
		UWorld* World = GEngine && IsValid(Context)
			? GEngine->GetWorldFromContextObject(Context, EGetWorldErrorMode::ReturnNull) : nullptr;
		return World ? World->GetSubsystem<UBox3DAsyncSubsystem>() : nullptr;
	}
	FBox3DAsyncQueryDesc QueryDesc(const FBox3DBPQuery& Source)
	{
		FBox3DAsyncQueryDesc Result;
		Result.Kind = static_cast<EBox3DAsyncQueryKind>(Source.Kind);
		Result.Shape = static_cast<EBox3DAsyncQueryShape>(Source.Shape);
		Result.Mode = static_cast<EBox3DAsyncLineMode>(Source.Mode);
		Result.Start = Source.Start; Result.End = Source.End;
		Result.Rotation = Source.Rotation.Quaternion();
		Result.Radius = Source.Radius; Result.HalfHeight = Source.HalfHeight;
		Result.HalfExtents = Source.HalfExtents; Result.ConvexPoints = Source.ConvexPoints;
		Result.CategoryBits = UnsignedBits(Source.CategoryBits); Result.MaskBits = UnsignedBits(Source.MaskBits);
		Result.bStatic = Source.bStatic; Result.bDynamic = Source.bDynamic;
		Result.bSensors = Source.bSensors; Result.bSolids = Source.bSolids;
		Result.MaxHits = Source.MaxHits; Result.UserTag = UnsignedBits(Source.UserTag);
		for (const FBox3DBPIdentity& Ignored : Source.IgnoredBodies)
			Result.IgnoredBodyHandles.Add({Ignored.World, UnsignedBits(Ignored.Id)});
		return Result;
	}
	FBox3DBPQueryResult QueryResult(const FBox3DAsyncQueryResult& Source)
	{
		FBox3DBPQueryResult Result;
		Result.Status = static_cast<EBox3DBPQueryStatus>(Source.Status);
		Result.Failure = static_cast<EBox3DBPQueryFailure>(Source.Failure);
		Result.Kind = static_cast<EBox3DBPQueryKind>(Source.Kind);
		Result.UserTag = SignedBits(Source.UserTag); Result.Sample = Sample(Source.Sample);
		Result.Hits.Reserve(Source.Hits.Num());
		for (const FBox3DAsyncLineHit& Hit : Source.Hits)
		{
			FBox3DBPHit& Output = Result.Hits.AddDefaulted_GetRef();
			Output.Body = Identity(Hit.BodyHandle.World, Hit.BodyHandle.Id);
			Output.Shape = Identity(Hit.ShapeHandle.World, Hit.ShapeHandle.Id);
			Output.ShapeType = Hit.ShapeType; Output.Fraction = Hit.Fraction; Output.Distance = Hit.Distance;
			Output.Point = Hit.Point; Output.Normal = Hit.Normal; Output.MaterialId = SignedBits(Hit.MaterialId);
			Output.TriangleIndex = Hit.TriangleIndex; Output.ChildIndex = Hit.ChildIndex;
		}
		return Result;
	}
	FBox3DBPEventDelivery EventDelivery(const FBox3DAsyncEventDelivery& Source)
	{
		FBox3DBPEventDelivery Result;
		Result.Status = static_cast<EBox3DBPEventStatus>(Source.Status);
		Result.FirstMissedSequence = SignedBits(Source.FirstMissedSequence);
		Result.LastMissedSequence = SignedBits(Source.LastMissedSequence);
		Result.Sequence = SignedBits(Source.Batch.Sequence); Result.Sample = Sample(Source.Batch.Sample);
		Result.Events.Reserve(Source.Batch.Events.Num());
		for (const FBox3DAsyncEvent& Event : Source.Batch.Events)
		{
			FBox3DBPEvent& Out = Result.Events.AddDefaulted_GetRef();
			Out.Kind = static_cast<EBox3DBPEventKind>(Event.Kind);
			Out.EndCause = static_cast<EBox3DBPEndCause>(Event.EndCause);
			Out.BodyA = Identity(Event.BodyHandleA.World, Event.BodyHandleA.Id);
			Out.BodyB = Identity(Event.BodyHandleB.World, Event.BodyHandleB.Id);
			Out.ShapeA = Identity(Event.ShapeHandleA.World, Event.ShapeHandleA.Id);
			Out.ShapeB = Identity(Event.ShapeHandleB.World, Event.ShapeHandleB.Id);
			Out.Point = Event.Point; Out.Normal = Event.Normal; Out.ApproachSpeed = Event.ApproachSpeed;
			Out.MaterialA = SignedBits(Event.MaterialA); Out.MaterialB = SignedBits(Event.MaterialB);
		}
		return Result;
	}
}

UBox3DBPAsyncQuery* UBox3DBPAsyncQuery::QueryBox3DAsync(UObject* WorldContextObject, UObject* Owner,
                                                        const FBox3DBPQuery& Query)
{
	UBox3DBPAsyncQuery* Action = NewObject<UBox3DBPAsyncQuery>();
	Action->WeakOwner = Owner;
	Action->Physics = ResolveAsync(WorldContextObject);
	Action->Input = Query;
	if (IsValid(WorldContextObject)) Action->RegisterWithGameInstance(WorldContextObject);
	return Action;
}

void UBox3DBPAsyncQuery::Activate()
{
	Super::Activate();
	UBox3DAsyncSubsystem* Subsystem = Physics.Get();
	if (!WeakOwner.IsValid() || !Subsystem || !Subsystem->TryRegisterQueryConsumer(Consumer))
	{
		FBox3DAsyncQueryResult Failed; Failed.Kind = static_cast<EBox3DAsyncQueryKind>(Input.Kind);
		Failed.UserTag = UnsignedBits(Input.UserTag); Failed.Status = EBox3DAsyncQueryStatus::Failed;
		Complete(Failed, EBox3DAsyncQueryAdmission::Unavailable); return;
	}
	const EBox3DAsyncQueryAdmission Admission = Subsystem->SubmitQuery(Consumer, QueryDesc(Input), Request);
	if (Admission != EBox3DAsyncQueryAdmission::Accepted)
	{
		FBox3DAsyncQueryResult Failed; Failed.Kind = static_cast<EBox3DAsyncQueryKind>(Input.Kind);
		Failed.UserTag = UnsignedBits(Input.UserTag); Failed.Status = EBox3DAsyncQueryStatus::Failed;
		Complete(Failed, Admission); return;
	}
	if (!Subsystem->TrySetQueryCallback(Consumer, Request, this,
			[WeakThis = TWeakObjectPtr<UBox3DBPAsyncQuery>(this)](const FBox3DAsyncQueryResult& Result)
			{
				if (UBox3DBPAsyncQuery* Action = WeakThis.Get()) Action->Complete(Result);
			}))
	{
		FBox3DAsyncQueryResult Failed; Failed.Kind = static_cast<EBox3DAsyncQueryKind>(Input.Kind);
		Failed.UserTag = UnsignedBits(Input.UserTag); Failed.Status = EBox3DAsyncQueryStatus::Failed;
		Failed.Failure = EBox3DAsyncQueryFailure::WorkerUnavailable;
		Complete(Failed);
	}
}

void UBox3DBPAsyncQuery::Cancel()
{
	if (!bFinished) if (UBox3DAsyncSubsystem* Subsystem = Physics.Get()) Subsystem->TryCancelQuery(Consumer, Request);
}

void UBox3DBPAsyncQuery::Complete(const FBox3DAsyncQueryResult& Result, EBox3DAsyncQueryAdmission Admission)
{
	if (bFinished) return;
	bFinished = true;
	if (WeakOwner.IsValid())
	{
		FBox3DBPQueryResult Output = QueryResult(Result);
		Output.Admission = static_cast<EBox3DBPQueryAdmission>(Admission);
		Completed.Broadcast(Output);
	}
	SetReadyToDestroy();
}

void UBox3DBPAsyncQuery::SetReadyToDestroy()
{
	if (UBox3DAsyncSubsystem* Subsystem = Physics.Get()) Subsystem->UnregisterQueryConsumer(Consumer);
	Consumer = {}; Request = {}; Physics.Reset();
	Super::SetReadyToDestroy();
}

bool UBox3DBPEventSubscription::Start(UBox3DAsyncSubsystem* InPhysics, UObject* Owner,
                                      const FBox3DBPEventFilter& Filter)
{
	if (!InPhysics || !IsValid(Owner)) return false;
	FBox3DAsyncEventFilter Native;
	Native.BodyHandle = {Filter.Body.World, UnsignedBits(Filter.Body.Id)};
	Native.ShapeHandle = {Filter.Shape.World, UnsignedBits(Filter.Shape.Id)};
	Native.bContacts = Filter.bContacts; Native.bSensors = Filter.bSensors; Native.bImpacts = Filter.bImpacts;
	Native.bBodyPairs = Filter.bBodyPairs; Native.MinimumImpactSpeed = Filter.MinimumImpactSpeed;
	Physics = InPhysics;
	const bool bAccepted = InPhysics->SubscribeEvents(Native, Owner,
		[WeakThis = TWeakObjectPtr<UBox3DBPEventSubscription>(this)](const FBox3DAsyncEventDelivery& Delivery)
		{
			if (UBox3DBPEventSubscription* Self = WeakThis.Get())
			{
				Self->Delivered.Broadcast(EventDelivery(Delivery));
				if (Delivery.Status == EBox3DAsyncEventPollStatus::Closed) Self->Stop();
			}
		}, true, Subscription);
	if (!bAccepted) Physics.Reset();
	return bAccepted;
}

void UBox3DBPEventSubscription::Stop()
{
	if (UBox3DAsyncSubsystem* Subsystem = Physics.Get()) Subsystem->UnsubscribeEvents(Subscription);
	Physics.Reset(); Subscription = {};
}

void UBox3DBPEventSubscription::BeginDestroy()
{
	if (IsInGameThread()) Stop();
	Super::BeginDestroy();
}

UBox3DBPEventSubscription* UBox3DBPAsyncLibrary::SubscribeBox3DAsyncEvents(UObject* WorldContextObject, UObject* Owner,
                                                                          const FBox3DBPEventFilter& Filter)
{
	UBox3DAsyncSubsystem* Subsystem = ResolveAsync(WorldContextObject);
	if (!Subsystem || !IsValid(Owner)) return nullptr;
	UBox3DBPEventSubscription* Subscription = NewObject<UBox3DBPEventSubscription>();
	return Subscription->Start(Subsystem, Owner, Filter) ? Subscription : nullptr;
}
