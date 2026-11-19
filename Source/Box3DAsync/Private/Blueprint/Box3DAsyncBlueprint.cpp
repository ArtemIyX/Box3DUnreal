#include "Blueprint/Box3DAsyncBlueprint.h"

#include "Engine/Engine.h"
#include "Containers/Ticker.h"
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
		Result.bStatic = Source.bStatic; Result.bDynamic = Source.bDynamic; Result.bKinematic = Source.bKinematic;
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

bool UBox3DBPAsyncLibrary::SubmitBodyCommand(UObject* WorldContextObject, const FBox3DBPIdentity& Body,
	const FBox3DBPBodyCommand& Command)
{
	UBox3DAsyncSubsystem* Subsystem = ResolveAsync(WorldContextObject);
	if (!Subsystem) return false;
	FBox3DAsyncBodyCommand Native;
	Native.Type = Command.Type;
	Native.Value = Command.Value;
	Native.Point = Command.Point;
	Native.Target = Command.Target;
	Native.TargetDuration = Command.TargetDuration;
	Native.bWake = Command.bWake;
	Native.bEnabled = Command.bEnabled;
	return Subsystem->TrySubmitBodyCommand({Body.World, UnsignedBits(Body.Id)}, Native);
}

bool UBox3DBPAsyncLibrary::GetLatestBodyState(UObject* WorldContextObject, const FBox3DBPIdentity& Body,
	FBox3DBPBodyState& OutState)
{
	OutState = {};
	UBox3DAsyncSubsystem* Subsystem = ResolveAsync(WorldContextObject);
	FBox3DAsyncBodyState State;
	if (!Subsystem || !Subsystem->TryGetLatestBodyState({Body.World, UnsignedBits(Body.Id)}, State)) return false;
	OutState.Body = Identity(State.World, State.BodyId);
	OutState.CaptureSerial = SignedBits(State.CaptureSerial);
	OutState.SimulationStep = SignedBits(State.SimulationStep);
	OutState.CaptureMonotonicSeconds = State.CaptureMonotonicSeconds;
	OutState.Transform = State.Transform;
	OutState.LinearVelocity = State.LinearVelocity;
	OutState.AngularVelocity = State.AngularVelocity;
	OutState.Mass = State.Mass;
	OutState.bAwake = State.bAwake;
	OutState.bEnabled = State.bEnabled;
	return true;
}

namespace
{
	FBox3DAsyncFieldDesc FieldDesc(const FBox3DBPFieldDesc& In)
	{
		FBox3DAsyncFieldDesc Out;
		Out.Mode = In.Mode;
		Out.Center = In.Center;
		Out.Radius = In.Radius;
		Out.Vector = In.Vector;
		Out.WindDrag = In.WindDrag;
		Out.FalloffExponent = In.FalloffExponent;
		Out.bWakeBodies = In.bWakeBodies;
		return Out;
	}
}

bool UBox3DBPAsyncLibrary::CreateField(UObject* WorldContextObject, const FBox3DBPFieldDesc& Desc, FBox3DBPIdentity& OutField)
{
	OutField = {};
	UBox3DAsyncSubsystem* Subsystem = ResolveAsync(WorldContextObject);
	FBox3DAsyncFieldHandle Field;
	if (!Subsystem || !Subsystem->TryCreateField(FieldDesc(Desc), Field)) return false;
	OutField = Identity(Field.World, Field.Id);
	return true;
}

bool UBox3DBPAsyncLibrary::UpdateField(UObject* WorldContextObject, const FBox3DBPIdentity& Field, const FBox3DBPFieldDesc& Desc)
{
	UBox3DAsyncSubsystem* Subsystem = ResolveAsync(WorldContextObject);
	return Subsystem && Subsystem->TryUpdateField({Field.World, UnsignedBits(Field.Id)}, FieldDesc(Desc));
}

bool UBox3DBPAsyncLibrary::RemoveField(UObject* WorldContextObject, const FBox3DBPIdentity& Field)
{
	UBox3DAsyncSubsystem* Subsystem = ResolveAsync(WorldContextObject);
	return Subsystem && Subsystem->TryRemoveField({Field.World, UnsignedBits(Field.Id)});
}

namespace
{
	FBox3DAsyncJointDesc MakeAsyncJointDesc(const FBox3DBPJointDesc& Desc)
	{
		FBox3DAsyncJointDesc Native;
		Native.BodyA = {Desc.BodyA.World, UnsignedBits(Desc.BodyA.Id)};
		Native.BodyB = {Desc.BodyB.World, UnsignedBits(Desc.BodyB.Id)};
		Native.Type = Desc.Type;
		Native.LocalFrameA = Desc.LocalFrameA;
		Native.LocalFrameB = Desc.LocalFrameB;
		Native.bCollideConnected = Desc.bCollideConnected;
		Native.BreakForce = Desc.BreakForce;
		Native.BreakTorque = Desc.BreakTorque;
		Native.ConstraintHertz = Desc.ConstraintHertz;
		Native.ConstraintDampingRatio = Desc.ConstraintDampingRatio;
		Native.bEnableSpring = Desc.bEnableSpring;
		Native.SpringHertz = Desc.SpringHertz;
		Native.SpringDampingRatio = Desc.SpringDampingRatio;
		Native.bEnableLimit = Desc.bEnableLimit;
		Native.LowerLimit = Desc.LowerLimit;
		Native.UpperLimit = Desc.UpperLimit;
		Native.bEnableMotor = Desc.bEnableMotor;
		Native.MotorSpeed = Desc.MotorSpeed;
		Native.MaxMotorForce = Desc.MaxMotorForce;
		Native.MaxMotorTorque = Desc.MaxMotorTorque;
		Native.DistanceLength = Desc.DistanceLength;
		Native.Target = Desc.Target;
		Native.AngularHertz = Desc.AngularHertz;
		Native.AngularDampingRatio = Desc.AngularDampingRatio;
		Native.LinearMotorVelocity = Desc.LinearMotorVelocity;
		Native.AngularMotorVelocity = Desc.AngularMotorVelocity;
		Native.MaxSpringForce = Desc.MaxSpringForce;
		Native.MaxSpringTorque = Desc.MaxSpringTorque;
		Native.TargetRotation = Desc.TargetRotation.Quaternion();
		Native.bEnableSteering = Desc.bEnableSteering;
		Native.SteeringHertz = Desc.SteeringHertz;
		Native.SteeringDampingRatio = Desc.SteeringDampingRatio;
		Native.TargetSteeringAngle = Desc.TargetSteeringAngle;
		Native.MaxSteeringTorque = Desc.MaxSteeringTorque;
		Native.bEnableSteeringLimit = Desc.bEnableSteeringLimit;
		Native.LowerSteeringLimit = Desc.LowerSteeringLimit;
		Native.UpperSteeringLimit = Desc.UpperSteeringLimit;
		Native.bEnableConeLimit = Desc.bEnableConeLimit;
		Native.ConeAngle = Desc.ConeAngle;
		Native.bEnableTwistLimit = Desc.bEnableTwistLimit;
		Native.LowerTwist = Desc.LowerTwist;
		Native.UpperTwist = Desc.UpperTwist;
		return Native;
	}
}

bool UBox3DBPAsyncLibrary::CreateJoint(UObject* WorldContextObject, const FBox3DBPJointDesc& Desc, FBox3DBPIdentity& OutJoint)
{
	OutJoint = {};
	UBox3DAsyncSubsystem* Subsystem = ResolveAsync(WorldContextObject);
	if (!Subsystem) return false;
	FBox3DAsyncJointDesc Native = MakeAsyncJointDesc(Desc);
	FBox3DAsyncJointHandle Joint;
	if (!Subsystem->TryCreateJoint(Native, Joint)) return false;
	OutJoint = Identity(Joint.World, Joint.Id);
	return true;
}

EBox3DAsyncJointStatus UBox3DBPAsyncLibrary::GetJointStatus(UObject* WorldContextObject, const FBox3DBPIdentity& Joint)
{
	UBox3DAsyncSubsystem* Subsystem = ResolveAsync(WorldContextObject);
	return Subsystem ? Subsystem->GetJointStatus({Joint.World, UnsignedBits(Joint.Id)}) : EBox3DAsyncJointStatus::Unavailable;
}

bool UBox3DBPAsyncLibrary::UpdateJoint(UObject* WorldContextObject, const FBox3DBPIdentity& Joint, const FBox3DBPJointDesc& Settings)
{
	UBox3DAsyncSubsystem* Subsystem = ResolveAsync(WorldContextObject);
	return Subsystem && Subsystem->TryUpdateJoint({Joint.World, UnsignedBits(Joint.Id)}, MakeAsyncJointDesc(Settings));
}

bool UBox3DBPAsyncLibrary::RemoveJoint(UObject* WorldContextObject, const FBox3DBPIdentity& Joint)
{
	UBox3DAsyncSubsystem* Subsystem = ResolveAsync(WorldContextObject);
	return Subsystem && Subsystem->TryRemoveJoint({Joint.World, UnsignedBits(Joint.Id)});
}

bool UBox3DBPAsyncLibrary::SubmitJointCommand(UObject* WorldContextObject, const FBox3DBPIdentity& Joint, const FBox3DAsyncJointCommand& Command)
{
	UBox3DAsyncSubsystem* Subsystem = ResolveAsync(WorldContextObject);
	return Subsystem && Subsystem->TrySubmitJointCommand({Joint.World, UnsignedBits(Joint.Id)}, Command);
}

bool UBox3DBPAsyncLibrary::GetLatestJointState(UObject* WorldContextObject, const FBox3DBPIdentity& Joint, FBox3DBPJointState& OutState)
{
	OutState = {};
	UBox3DAsyncSubsystem* Subsystem = ResolveAsync(WorldContextObject);
	FBox3DAsyncJointState State;
	if (!Subsystem || !Subsystem->TryGetLatestJointState({Joint.World, UnsignedBits(Joint.Id)}, State)) return false;
	OutState.Joint = Identity(State.World, State.JointId);
	OutState.CaptureSerial = SignedBits(State.CaptureSerial);
	OutState.SimulationStep = SignedBits(State.SimulationStep);
	OutState.ConstraintForce = State.ConstraintForce;
	OutState.ConstraintTorque = State.ConstraintTorque;
	return true;
}

namespace
{
	FBox3DAsyncBodyDesc BodyDesc(const FBox3DBPBodyDesc& Source)
	{
		FBox3DAsyncBodyDesc Result;
		Result.Motion = Source.Motion;
		Result.Transform = Source.Transform;
		Result.InitialVelocity = Source.InitialVelocity;
		Result.InitialAngularVelocity = Source.InitialAngularVelocity;
		Result.GravityScale = Source.GravityScale;
		Result.LinearDamping = Source.LinearDamping;
		Result.AngularDamping = Source.AngularDamping;
		Result.MassOverride = Source.MassOverride;
		Result.bEnableSleep = Source.bEnableSleep;
		Result.bStartAwake = Source.bStartAwake;
		Result.bStartEnabled = Source.bStartEnabled;
		Result.bBullet = Source.bBullet;
		Result.Shapes.Reserve(Source.Shapes.Num());
		for (const FBox3DBPBodyShape& Shape : Source.Shapes)
		{
			FBox3DAsyncBodyShapeDesc& Out = Result.Shapes.AddDefaulted_GetRef();
			Out.Kind = Shape.Kind; Out.LocalTransform = Shape.LocalTransform;
			Out.HalfExtents = Shape.HalfExtents; Out.Radius = Shape.Radius; Out.HalfHeight = Shape.HalfHeight;
			Out.ConvexPoints = Shape.ConvexPoints; Out.Friction = Shape.Friction; Out.Restitution = Shape.Restitution;
			Out.Options.bSensor = Shape.bSensor; Out.Options.bDetectableBySensors = Shape.bDetectableBySensors;
			Out.Options.bContactEvents = Shape.bContactEvents; Out.Options.bImpactEvents = Shape.bImpactEvents;
			Out.Options.CategoryBits = UnsignedBits(Shape.CategoryBits); Out.Options.MaskBits = UnsignedBits(Shape.MaskBits);
			Out.Options.GroupIndex = Shape.GroupIndex; Out.Options.Density = Shape.Density;
			Out.Options.MaterialId = UnsignedBits(Shape.MaterialId); Out.SubshapeKey = UnsignedBits(Shape.SubshapeKey);
		}
		return Result;
	}
}

UBox3DBPCreateBody* UBox3DBPCreateBody::CreateBox3DAsyncBody(UObject* WorldContextObject, UObject* Owner, const FBox3DBPBodyDesc& Desc)
{
	UBox3DBPCreateBody* Action = NewObject<UBox3DBPCreateBody>();
	Action->WeakOwner = Owner;
	Action->Input = Desc;
	Action->Physics = ResolveAsync(WorldContextObject);
	if (UBox3DAsyncSubsystem* Subsystem = Action->Physics.Get()) Action->WeakWorld = Subsystem->GetWorld();
	if (IsValid(WorldContextObject)) Action->RegisterWithGameInstance(WorldContextObject);
	return Action;
}

void UBox3DBPCreateBody::Activate()
{
	UBox3DAsyncSubsystem* Subsystem = Physics.Get();
	if (!Subsystem || !WeakWorld.IsValid() || !WeakWorld->GetGameInstance() || !WeakOwner.IsValid()
		|| !Subsystem->TryRegisterCreationConsumer(Consumer)
		|| !Subsystem->TryCreateBody(Consumer, BodyDesc(Input), Body))
	{
		Finish(false);
		return;
	}
	Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UBox3DBPCreateBody::Poll));
}

void UBox3DBPCreateBody::Cancel()
{
	bCancel = true;
}

bool UBox3DBPCreateBody::Poll(float DeltaTime)
{
	UBox3DAsyncSubsystem* Subsystem = Physics.Get();
	UWorld* World = WeakWorld.Get();
	if (!Subsystem || !World || World->bIsTearingDown) { Finish(false); return false; }
	if (!bHaveResult)
	{
		if (Subsystem->TryTakeCreationResult(Consumer, Body, CreationResult))
		{
			bHaveResult = true;
			bSucceeded = CreationResult.bSucceeded;
		}
	}
	if (bCancel || !WeakOwner.IsValid())
	{
		// A queued removal after pending creation is ordered on the same worker queue.
		if ((!bHaveResult || bSucceeded) && !Subsystem->TryRemoveBody(Body)) return true;
		Finish(false);
		return false;
	}
	if (bHaveResult) { Finish(bSucceeded); return false; }
	return true;
}

void UBox3DBPCreateBody::Finish(bool bSuccess)
{
	if (UBox3DAsyncSubsystem* Subsystem = Physics.Get()) Subsystem->UnregisterCreationConsumer(Consumer);
	Consumer = {};
	Ticker.Reset(); // Poll returns false, so the ticker removes its delegate.
	if (WeakOwner.IsValid() && !bCancel)
	{
		FBox3DBPBodyCreationResult Output;
		Output.bSucceeded = bSuccess;
		if (bSuccess)
		{
			Output.Body = Identity(Body.World, Body.Id);
			for (const FBox3DAsyncShapeHandle& Shape : CreationResult.ShapeHandles)
				Output.Shapes.Add(Identity(Shape.World, Shape.Id));
			for (uint64 Key : CreationResult.SubshapeKeys) Output.SubshapeKeys.Add(SignedBits(Key));
		}
		Completed.Broadcast(Output);
	}
	SetReadyToDestroy();
}

bool UBox3DBPAsyncLibrary::RemoveBody(UObject* WorldContextObject, const FBox3DBPIdentity& Body)
{
	UBox3DAsyncSubsystem* Subsystem = ResolveAsync(WorldContextObject);
	return Subsystem && Subsystem->TryRemoveBody({Body.World, UnsignedBits(Body.Id)});
}

void UBox3DBPAsyncLibrary::DrainJointBreakEvents(UObject* WorldContextObject, TArray<FBox3DBPJointBreakEvent>& OutEvents, bool& bGap)
{
	OutEvents.Reset();
	bGap = true;
	UBox3DAsyncSubsystem* Subsystem = ResolveAsync(WorldContextObject);
	if (!Subsystem) return;
	TArray<FBox3DAsyncJointBreakEvent> Events;
	Subsystem->DrainJointBreakEvents(Events, bGap);
	for (const FBox3DAsyncJointBreakEvent& Event : Events)
	{
		FBox3DBPJointBreakEvent& Output = OutEvents.AddDefaulted_GetRef();
		Output.Joint = Identity(Event.Joint.World, Event.Joint.Id);
		Output.SimulationStep = SignedBits(Event.SimulationStep);
	}
}
