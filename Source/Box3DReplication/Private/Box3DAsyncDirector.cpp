#include "Box3DAsyncDirector.h"

#include "Box3DAsyncPresentationActor.h"
#include "Box3DIrisReplicationSubsystem.h"
#include "Components/StaticMeshComponent.h"
#include "World/Box3DAsyncSubsystem.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Containers/Ticker.h"

namespace
{
	TMap<const UWorld*, TWeakObjectPtr<ABox3DAsyncDirector>> GActiveBox3DAsyncDirectors;
	DEFINE_LOG_CATEGORY_STATIC(LogBox3DAsyncExample, Log, All);

	struct FPendingRemovalCleanupState
	{
		const UWorld* WorldKey = nullptr;
		TWeakObjectPtr<UWorld> World;
		TWeakObjectPtr<UBox3DAsyncSubsystem> Subsystem;
		TSet<uint64> BodyIds;
		TSet<uint64> AwaitingCreationResultIds;
		TArray<FBox3DAsyncCreationResult> DeferredResults;
		FTSTicker::FDelegateHandle TickerHandle;
		bool bAdoptedByDirector = false;
	};
	TMap<const UWorld*, TSharedPtr<FPendingRemovalCleanupState>> GPendingRemovalCleanup;

	void StartPendingRemovalCleanup(UWorld* World, UBox3DAsyncSubsystem* Subsystem,
		TSet<uint64> PendingRemovalIds, TSet<uint64> AwaitingCreationResultIds,
		TArray<FBox3DAsyncCreationResult> DeferredResults)
	{
		if (World == nullptr || Subsystem == nullptr)
		{
			return;
		}
		const bool bHasOutstandingResults = Subsystem->GetDiagnostics().OutstandingCreationResults > 0;
		if (PendingRemovalIds.IsEmpty() && AwaitingCreationResultIds.IsEmpty() && !bHasOutstandingResults
			&& DeferredResults.IsEmpty()) return;
		TSharedPtr<FPendingRemovalCleanupState> State = MakeShared<FPendingRemovalCleanupState>();
		State->WorldKey = World;
		State->World = World;
		State->Subsystem = Subsystem;
		State->BodyIds = MoveTemp(PendingRemovalIds);
		State->AwaitingCreationResultIds = MoveTemp(AwaitingCreationResultIds);
		State->DeferredResults = MoveTemp(DeferredResults);
		GPendingRemovalCleanup.Add(World, State);
		State->TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
			[State](float)
			{
				if (State->bAdoptedByDirector)
				{
					return false;
				}
				UWorld* CleanupWorld = State->World.Get();
				UBox3DAsyncSubsystem* CleanupSubsystem = State->Subsystem.Get();
				if (!IsValid(CleanupWorld) || !IsValid(CleanupSubsystem))
				{
					GPendingRemovalCleanup.Remove(State->WorldKey);
					return false;
				}
				TArray<FBox3DAsyncCreationResult> Results;
				CleanupSubsystem->DrainCreationResults(Results);
				for (const FBox3DAsyncCreationResult& Result : Results)
				{
					const bool bOwnedBodyResult = Result.Kind == EBox3DAsyncCreationKind::Body
						&& State->AwaitingCreationResultIds.Remove(Result.ResourceId) > 0;
					if (bOwnedBodyResult)
					{
						if (!Result.bSucceeded) State->BodyIds.Remove(Result.ResourceId);
					}
					else
					{
						State->DeferredResults.Add(Result);
					}
				}
				TArray<uint64> CompletedRemovals;
				for (const uint64 BodyId : State->BodyIds)
				{
					if (CleanupSubsystem->TryRemoveBody(BodyId))
					{
						CompletedRemovals.Add(BodyId);
					}
				}
				for (const uint64 BodyId : CompletedRemovals)
				{
					State->BodyIds.Remove(BodyId);
				}
				const bool bHasOutstandingCreationResults = CleanupSubsystem->GetDiagnostics().OutstandingCreationResults > 0;
				if (State->BodyIds.IsEmpty() && State->AwaitingCreationResultIds.IsEmpty()
					&& !bHasOutstandingCreationResults && State->DeferredResults.IsEmpty())
				{
					GPendingRemovalCleanup.Remove(State->WorldKey);
					return false;
				}
				return !State->BodyIds.IsEmpty() || !State->AwaitingCreationResultIds.IsEmpty()
					|| bHasOutstandingCreationResults || !State->DeferredResults.IsEmpty();
			}), 0.1f);
	}
}

ABox3DAsyncDirector::ABox3DAsyncDirector()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
}

ABox3DAsyncDirector* ABox3DAsyncDirector::FindForWorld(const UWorld* World)
{
	if (World == nullptr)
	{
		return nullptr;
	}
	if (TWeakObjectPtr<ABox3DAsyncDirector>* Found = GActiveBox3DAsyncDirectors.Find(World))
	{
		ABox3DAsyncDirector* Director = Found->Get();
		return IsValid(Director) && Director->bIsActiveDirector ? Director : nullptr;
	}
	return nullptr;
}

void ABox3DAsyncDirector::BeginPlay()
{
	Super::BeginPlay();
	ABox3DAsyncDirector* Existing = FindForWorld(GetWorld());
	if (IsValid(Existing) && Existing != this && Existing->bIsActiveDirector)
	{
		SetActorTickEnabled(false);
		UE_LOG(LogBox3DAsyncExample, Warning, TEXT("Ignoring duplicate Box3D async director %s; %s is active in this world."),
			*GetName(), *Existing->GetName());
		return;
	}
	GActiveBox3DAsyncDirectors.Add(GetWorld(), this);
	ActivateDirector();
}

void ABox3DAsyncDirector::ActivateDirector()
{
	UWorld* World = GetWorld();
	if (World == nullptr || bIsActiveDirector)
	{
		return;
	}
	AsyncSubsystem = World->GetSubsystem<UBox3DAsyncSubsystem>();
	if (!AsyncSubsystem.IsValid())
	{
		UE_LOG(LogBox3DAsyncExample, Error, TEXT("Box3DAsync subsystem is unavailable in world %s."), *World->GetName());
		return;
	}
	bIsActiveDirector = true;
	SetActorTickEnabled(true);
	if (TSharedPtr<FPendingRemovalCleanupState>* Cleanup = GPendingRemovalCleanup.Find(World))
	{
		TSharedPtr<FPendingRemovalCleanupState> State = *Cleanup;
		State->bAdoptedByDirector = true;
		if (State->TickerHandle.IsValid())
		{
			FTSTicker::GetCoreTicker().RemoveTicker(State->TickerHandle);
		}
		for (const uint64 BodyId : State->BodyIds)
		{
			PendingRemovalIds.Add(BodyId);
		}
		for (const uint64 BodyId : State->AwaitingCreationResultIds)
		{
			OwnedBodyCreationIds.Add(BodyId);
		}
		UnclaimedCreationResults.Append(MoveTemp(State->DeferredResults));
		GPendingRemovalCleanup.Remove(World);
	}
	ActorSpawnedHandle = World->AddOnActorSpawnedHandler(
		FOnActorSpawned::FDelegate::CreateUObject(this, &ABox3DAsyncDirector::OnActorSpawned));
	for (TActorIterator<ABox3DAsyncPresentationActor> It(World); It; ++It)
	{
		RegisterPresentationActor(*It);
	}
}

void ABox3DAsyncDirector::OnActorSpawned(AActor* SpawnedActor)
{
	if (ABox3DAsyncPresentationActor* PresentationActor = Cast<ABox3DAsyncPresentationActor>(SpawnedActor))
	{
		RegisterPresentationActor(PresentationActor);
	}
}

bool ABox3DAsyncDirector::RegisterPresentationActor(ABox3DAsyncPresentationActor* Actor)
{
	if (!bIsActiveDirector || !IsValid(Actor) || Actor->GetWorld() != GetWorld() || !AsyncSubsystem.IsValid())
	{
		return false;
	}
	if (Box3DAsyncPresentationCollision::IsSharedDynamic(Actor->ReplicationPolicy, Actor->CollisionMode))
	{
		UBox3DIrisReplicationSubsystem* Replication = GetWorld()->GetSubsystem<UBox3DIrisReplicationSubsystem>();
		if (Replication == nullptr || !Replication->RegisterAuthoredPresentationActor(Actor)) return false;
	}
	if (Actor->IsNetworkReplica() || (Box3DAsyncPresentationCollision::IsSharedDynamic(
		Actor->ReplicationPolicy, Actor->CollisionMode)
		&& GetWorld()->GetNetMode() == NM_Client))
	{
		return true;
	}
	TrackedActors.Add(Actor);
	Actor->RegisteredDirector = this;
	if (!Actor->HasActorBegunPlay())
	{
		return true;
	}
	if (Actor->RegistrationState == ABox3DAsyncPresentationActor::ERegistrationState::Queued
		|| Actor->RegistrationState == ABox3DAsyncPresentationActor::ERegistrationState::Active)
	{
		return true;
	}
	if (Actor->bWaitingForCapacity && GetWorld()->GetTimeSeconds() < Actor->NextRegistrationAttemptTimeSeconds)
	{
		return false;
	}
	if (Actor->RegistrationState == ABox3DAsyncPresentationActor::ERegistrationState::Failed
		&& !Actor->bWaitingForCapacity)
	{
		return false;
	}
	return TryQueueActor(Actor);
}

bool ABox3DAsyncDirector::TryQueueActor(ABox3DAsyncPresentationActor* Actor)
{
	UBox3DAsyncSubsystem* Subsystem = AsyncSubsystem.Get();
	if (Subsystem == nullptr || !IsValid(Actor) || !Actor->HasActorBegunPlay())
	{
		return false;
	}
	const FTransform BodyTransform(Actor->GetActorQuat(), Actor->GetActorLocation());
	FBox3DAsyncBodyId BodyId = 0;
	bool bAccepted = false;
	FBox3DAsyncShapeOptions ShapeOptions;
	const Box3DAsyncPresentationCollision::FFilter CollisionFilter =
		Box3DAsyncPresentationCollision::GetFilter(Actor->ReplicationPolicy, Actor->CollisionMode);
	ShapeOptions.CategoryBits = CollisionFilter.CategoryBits;
	ShapeOptions.MaskBits = CollisionFilter.MaskBits;
	switch (Actor->CollisionMode)
	{
	case EBox3DAsyncPresentationMode::AuthoredStaticCollision:
		bAccepted = Actor->PresentationMesh != nullptr
			&& Subsystem->TryCreateStaticMeshCollision(Actor->PresentationMesh->GetStaticMesh(), BodyTransform,
				Actor->GetActorScale3D(), ShapeOptions, BodyId);
		break;
	case EBox3DAsyncPresentationMode::AuthoredDynamicCollision:
		bAccepted = Actor->PresentationMesh != nullptr
			&& Subsystem->TryCreateDynamicMeshCollision(Actor->PresentationMesh->GetStaticMesh(), BodyTransform,
				Actor->GetActorScale3D(), Actor->InitialVelocity, ShapeOptions, BodyId);
		break;
	}
	if (bAccepted)
	{
		Actor->Box3DBodyId = BodyId;
		Actor->RegistrationState = ABox3DAsyncPresentationActor::ERegistrationState::Queued;
		Actor->bWaitingForCapacity = false;
		Actor->StaticCollisionAdmissionRetries = 0;
		Actor->NextRegistrationAttemptTimeSeconds = 0.0;
		BodyActors.Add(BodyId, Actor);
		OwnedBodyCreationIds.Add(BodyId);
		return true;
	}
	if (IsCapacitySaturated())
	{
		Actor->bWaitingForCapacity = true;
		Actor->NextRegistrationAttemptTimeSeconds = GetWorld()->GetTimeSeconds() + 0.1;
		return false;
	}
	if (++Actor->StaticCollisionAdmissionRetries < 8)
	{
		Actor->bWaitingForCapacity = true;
		const double BackoffSeconds = FMath::Min(0.25 * Actor->StaticCollisionAdmissionRetries, 2.0);
		Actor->NextRegistrationAttemptTimeSeconds = GetWorld()->GetTimeSeconds() + BackoffSeconds;
		return false;
	}
	Actor->RegistrationState = ABox3DAsyncPresentationActor::ERegistrationState::Failed;
	Actor->bWaitingForCapacity = false;
	UE_LOG(LogBox3DAsyncExample, Warning,
		TEXT("Box3D registration failed for %s. Check its authored mesh collision and scale, then call RetryBox3DRegistration."),
		*Actor->GetName());
	return false;
}

bool ABox3DAsyncDirector::IsCapacitySaturated() const
{
	const UBox3DAsyncSubsystem* Subsystem = AsyncSubsystem.Get();
	if (Subsystem == nullptr)
	{
		return false;
	}
	const FBox3DAsyncDiagnostics Diagnostics = Subsystem->GetDiagnostics();
	return Diagnostics.QueuedCommands >= UBox3DAsyncSubsystem::CommandCapacity
		|| Diagnostics.OutstandingCreationResults >= UBox3DAsyncSubsystem::OutstandingCreationResultCapacity;
}

void ABox3DAsyncDirector::UnregisterPresentationActor(ABox3DAsyncPresentationActor* Actor)
{
	if (Actor == nullptr)
	{
		return;
	}
	TrackedActors.Remove(Actor);
	Actor->RegisteredDirector.Reset();
	const uint64 BodyId = Actor->Box3DBodyId;
	Actor->Box3DBodyId = 0;
	Actor->bWaitingForCapacity = false;
	Actor->RegistrationState = ABox3DAsyncPresentationActor::ERegistrationState::Waiting;
	if (BodyId != 0)
	{
		if (Box3DAsyncPresentationCollision::IsSharedDynamic(Actor->ReplicationPolicy, Actor->CollisionMode))
		{
			if (UBox3DIrisReplicationSubsystem* Replication = GetWorld()->GetSubsystem<UBox3DIrisReplicationSubsystem>())
			{
				Replication->UnregisterSharedBody(Actor->ReplicationKey, BodyId);
			}
		}
		BodyActors.Remove(BodyId);
		if (AsyncSubsystem.IsValid() && !AsyncSubsystem->TryRemoveBody(BodyId))
		{
			PendingRemovalIds.Add(BodyId);
		}
	}
}

void ABox3DAsyncDirector::DrainCreationResults()
{
	// The director owns the subsystem result queue for this world; see the public class contract.
	UBox3DAsyncSubsystem* Subsystem = AsyncSubsystem.Get();
	if (Subsystem == nullptr)
	{
		return;
	}
	TArray<FBox3DAsyncCreationResult> Results;
	Subsystem->DrainCreationResults(Results);
	for (const FBox3DAsyncCreationResult& Result : Results)
	{
		if (Result.Kind != EBox3DAsyncCreationKind::Body || !OwnedBodyCreationIds.Contains(Result.ResourceId))
		{
			UnclaimedCreationResults.Add(Result);
			continue;
		}
		OwnedBodyCreationIds.Remove(Result.ResourceId);
		TWeakObjectPtr<ABox3DAsyncPresentationActor>* FoundActor = BodyActors.Find(Result.ResourceId);
		ABox3DAsyncPresentationActor* Actor = FoundActor != nullptr ? FoundActor->Get() : nullptr;
		if (!Result.bSucceeded)
		{
			const bool bFixtureFailure = ExampleFixtureBodies.Remove(Result.ResourceId) > 0;
			PendingRemovalIds.Remove(Result.ResourceId);
			if (bFixtureFailure)
			{
				RemoveExampleEventFixture();
				bExampleFixtureAttempted = true;
			}
			if (IsValid(Actor) && Actor->Box3DBodyId == Result.ResourceId)
			{
				Actor->Box3DBodyId = 0;
				Actor->RegistrationState = ABox3DAsyncPresentationActor::ERegistrationState::Failed;
			}
			BodyActors.Remove(Result.ResourceId);
			UE_LOG(LogBox3DAsyncExample, Warning, TEXT("Box3D body creation failed for ID %llu."),
				static_cast<unsigned long long>(Result.ResourceId));
			continue;
		}
		if (IsValid(Actor) && Actor->Box3DBodyId == Result.ResourceId)
		{
			Actor->RegistrationState = ABox3DAsyncPresentationActor::ERegistrationState::Active;
			if (Box3DAsyncPresentationCollision::IsSharedDynamic(Actor->ReplicationPolicy, Actor->CollisionMode)
				&& (GetWorld()->GetNetMode() == NM_ListenServer || GetWorld()->GetNetMode() == NM_DedicatedServer))
			{
				if (UBox3DIrisReplicationSubsystem* Replication = GetWorld()->GetSubsystem<UBox3DIrisReplicationSubsystem>())
				{
					Replication->RegisterSharedBody(Result.BodyHandle, Actor);
				}
			}
		}
	}
}

void ABox3DAsyncDirector::DrainUnclaimedCreationResults(TArray<FBox3DAsyncCreationResult>& OutResults)
{
	check(IsInGameThread());
	OutResults.Reset();
	Swap(OutResults, UnclaimedCreationResults);
}

void ABox3DAsyncDirector::RetryPendingRemovals()
{
	if (!AsyncSubsystem.IsValid())
	{
		return;
	}
	TArray<uint64> CompletedRemovals;
	for (const uint64 BodyId : PendingRemovalIds)
	{
		if (AsyncSubsystem->TryRemoveBody(BodyId))
		{
			CompletedRemovals.Add(BodyId);
		}
	}
	for (const uint64 BodyId : CompletedRemovals)
	{
		PendingRemovalIds.Remove(BodyId);
	}
}

void ABox3DAsyncDirector::ApplySampledPoses()
{
	UBox3DAsyncSubsystem* Subsystem = AsyncSubsystem.Get();
	if (Subsystem == nullptr)
	{
		return;
	}
	TArray<FBox3DAsyncPose> Poses;
	Subsystem->SamplePoses(Poses);
	for (const FBox3DAsyncPose& Pose : Poses)
	{
		const TWeakObjectPtr<ABox3DAsyncPresentationActor>* FoundActor = BodyActors.Find(Pose.BodyId);
		ABox3DAsyncPresentationActor* Actor = FoundActor != nullptr ? FoundActor->Get() : nullptr;
		if (IsValid(Actor) && Actor->Box3DBodyId == Pose.BodyId
			&& Actor->RegistrationState == ABox3DAsyncPresentationActor::ERegistrationState::Active)
		{
			Actor->SetActorLocRot_Optimized(Pose.Transform.GetLocation(), Pose.Transform.Rotator());
		}
	}
}

void ABox3DAsyncDirector::TickQueryEventExample()
{
	UBox3DAsyncSubsystem* Subsystem = AsyncSubsystem.Get();
	UWorld* World = GetWorld();
	if (Subsystem == nullptr || World == nullptr) return;
	if (!bEnableQueryEventExample)
	{
		StopQueryEventExample();
		return;
	}

	// Each system owns its own query completions and event cursor. Neither uses
	// the legacy director creation drain or the single raw event drain.
	if (ExampleQueryConsumer.Id == 0) Subsystem->TryRegisterQueryConsumer(ExampleQueryConsumer);
	if (ExampleEventSubscription.Id == 0)
	{
		FBox3DAsyncEventFilter Filter;
		Subsystem->SubscribeEvents(Filter, ExampleEventSubscription);
	}
	if (!bCreateExampleEventFixture)
	{
		RemoveExampleEventFixture();
	}
	else if (!bExampleFixtureAttempted && ExampleEventSubscription.Id != 0)
	{
		bExampleFixtureAttempted = true;
		const FVector Center = GetActorLocation() + GetActorForwardVector() * 300.0;
		FBox3DAsyncShapeOptions SensorOptions;
		SensorOptions.bSensor = true;
		SensorOptions.Density = 0.0f;
		FBox3DAsyncBodyId Sensor = 0;
		if (Subsystem->TryCreateStaticBox(FTransform(FQuat::Identity, Center), FVector(100.0, 100.0, 50.0), SensorOptions, Sensor))
		{
			ExampleFixtureBodies.Add(Sensor);
			OwnedBodyCreationIds.Add(Sensor);
		}
		FBox3DAsyncShapeOptions VisitorOptions;
		VisitorOptions.bDetectableBySensors = true;
		FBox3DAsyncBodyId Visitor = 0;
		if (Subsystem->TryCreateDynamicSphere(FTransform(FQuat::Identity, Center + FVector(0.0, 0.0, 180.0)),
			30.0f, FVector(0.0, 0.0, -200.0), VisitorOptions, Visitor))
		{
			ExampleFixtureBodies.Add(Visitor);
			OwnedBodyCreationIds.Add(Visitor);
		}
		if (Sensor == 0 || Visitor == 0)
		{
			UE_LOG(LogBox3DAsyncExample, Warning, TEXT("Async example event fixture could not be queued; disable and re-enable it to retry."));
			RemoveExampleEventFixture();
			bExampleFixtureAttempted = true;
		}
	}

	if (ExampleQueryRequest.Id != 0)
	{
		FBox3DAsyncQueryResult Result;
		if (Subsystem->TryTakeQueryResult(ExampleQueryConsumer, ExampleQueryRequest, Result))
		{
			UE_LOG(LogBox3DAsyncExample, Log, TEXT("Async example trace: status=%d hits=%d physics step=%llu"),
				int32(Result.Status), Result.Hits.Num(), static_cast<unsigned long long>(Result.Sample.Step));
			ExampleQueryRequest = {};
		}
	}
	const double Now = World->GetTimeSeconds();
	if (ExampleQueryConsumer.Id != 0 && ExampleQueryRequest.Id == 0 && Now >= NextExampleTraceTime
		&& FMath::IsFinite(ExampleTraceDistance) && ExampleTraceDistance > 0.0f)
	{
		FBox3DAsyncQueryDesc Query;
		Query.Start = GetActorLocation();
		Query.End = Query.Start + GetActorForwardVector() * ExampleTraceDistance;
		Query.Mode = EBox3DAsyncLineMode::Closest;
		Subsystem->TrySubmitQuery(ExampleQueryConsumer, Query, ExampleQueryRequest);
		const float Interval = FMath::IsFinite(ExampleTraceInterval) ? FMath::Max(ExampleTraceInterval, 0.1f) : 1.0f;
		NextExampleTraceTime = Now + Interval;
	}

	// Bound game-thread work per frame. A baseline replaces a subscriber's
	// relationship state; a gap means transient events may have been lost.
	for (int32 Index = 0; ExampleEventSubscription.Id != 0 && Index < 16; ++Index)
	{
		FBox3DAsyncEventDelivery Delivery;
		if (!Subsystem->PollEvents(ExampleEventSubscription, Delivery)
			|| Delivery.Status == EBox3DAsyncEventPollStatus::Pending
			|| Delivery.Status == EBox3DAsyncEventPollStatus::Unavailable) break;
		if (Delivery.Status == EBox3DAsyncEventPollStatus::NeedsResync)
		{
			UE_LOG(LogBox3DAsyncExample, Warning, TEXT("Async example event gap: sequences %llu to %llu; awaiting a replacement baseline."),
				static_cast<unsigned long long>(Delivery.FirstMissedSequence), static_cast<unsigned long long>(Delivery.LastMissedSequence));
		}
		else if (Delivery.Status == EBox3DAsyncEventPollStatus::Baseline || Delivery.Status == EBox3DAsyncEventPollStatus::Events)
		{
			UE_LOG(LogBox3DAsyncExample, Log, TEXT("Async example event %s: %d relationships/events at physics step %llu"),
				Delivery.Status == EBox3DAsyncEventPollStatus::Baseline ? TEXT("baseline") : TEXT("delta"),
				Delivery.Batch.Events.Num(), static_cast<unsigned long long>(Delivery.Batch.Sample.Step));
		}
		else break;
	}
}

void ABox3DAsyncDirector::StopQueryEventExample()
{
	RemoveExampleEventFixture();
	if (UBox3DAsyncSubsystem* Subsystem = AsyncSubsystem.Get())
	{
		if (ExampleEventSubscription.Id != 0) Subsystem->UnsubscribeEvents(ExampleEventSubscription);
		if (ExampleQueryConsumer.Id != 0) Subsystem->UnregisterQueryConsumer(ExampleQueryConsumer);
	}
	ExampleEventSubscription = {};
	ExampleQueryRequest = {};
	ExampleQueryConsumer = {};
	NextExampleTraceTime = 0.0;
}

void ABox3DAsyncDirector::RemoveExampleEventFixture()
{
	if (UBox3DAsyncSubsystem* Subsystem = AsyncSubsystem.Get())
	{
		for (const FBox3DAsyncBodyId Body : ExampleFixtureBodies)
		{
			if (!Subsystem->TryRemoveBody(Body)) PendingRemovalIds.Add(Body);
		}
	}
	ExampleFixtureBodies.Reset();
	bExampleFixtureAttempted = false;
}

void ABox3DAsyncDirector::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bIsActiveDirector)
	{
		return;
	}
	RetryPendingRemovals();
	DrainCreationResults();
	TArray<TWeakObjectPtr<ABox3DAsyncPresentationActor>> StaleActors;
	for (const TWeakObjectPtr<ABox3DAsyncPresentationActor>& WeakActor : TrackedActors)
	{
		ABox3DAsyncPresentationActor* Actor = WeakActor.Get();
		if (!IsValid(Actor))
		{
			StaleActors.Add(WeakActor);
			continue;
		}
		if (Actor->RegistrationState == ABox3DAsyncPresentationActor::ERegistrationState::Waiting
			&& Actor->HasActorBegunPlay())
		{
			RegisterPresentationActor(Actor);
		}
	}
	for (const TWeakObjectPtr<ABox3DAsyncPresentationActor>& WeakActor : StaleActors)
	{
		TrackedActors.Remove(WeakActor);
	}
	ApplySampledPoses();
	TickQueryEventExample();
}

void ABox3DAsyncDirector::RemoveAllRegisteredActors()
{
	TArray<TWeakObjectPtr<ABox3DAsyncPresentationActor>> Actors;
	Actors.Reserve(TrackedActors.Num());
	for (const TWeakObjectPtr<ABox3DAsyncPresentationActor>& Actor : TrackedActors)
	{
		Actors.Add(Actor);
	}
	for (const TWeakObjectPtr<ABox3DAsyncPresentationActor>& WeakActor : Actors)
	{
		if (ABox3DAsyncPresentationActor* Actor = WeakActor.Get())
		{
			UnregisterPresentationActor(Actor);
		}
	}
	UWorld* World = GetWorld();
	for (const TPair<uint64, TWeakObjectPtr<ABox3DAsyncPresentationActor>>& Body : BodyActors)
	{
		if (ABox3DAsyncPresentationActor* Actor = Body.Value.Get(); IsValid(Actor)
			&& Box3DAsyncPresentationCollision::IsSharedDynamic(Actor->ReplicationPolicy, Actor->CollisionMode)
			&& World != nullptr)
		{
			if (UBox3DIrisReplicationSubsystem* Replication = World->GetSubsystem<UBox3DIrisReplicationSubsystem>())
			{
				Replication->UnregisterSharedBody(Actor->ReplicationKey, Body.Key);
			}
		}
		if (AsyncSubsystem.IsValid() && !AsyncSubsystem->TryRemoveBody(Body.Key))
		{
			PendingRemovalIds.Add(Body.Key);
		}
	}
	TrackedActors.Reset();
	BodyActors.Reset();
}

void ABox3DAsyncDirector::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UWorld* World = GetWorld();
	if (bIsActiveDirector)
	{
		StopQueryEventExample();
		if (World != nullptr)
		{
			if (ActorSpawnedHandle.IsValid())
			{
				World->RemoveOnActorSpawnedHandler(ActorSpawnedHandle);
			}
		}
		RemoveAllRegisteredActors();
		bIsActiveDirector = false;
		GActiveBox3DAsyncDirectors.Remove(World);
		bool bPromotedReplacement = false;
		if (World != nullptr)
		{
			for (TActorIterator<ABox3DAsyncDirector> It(World); It; ++It)
			{
				if (IsValid(*It) && *It != this && !It->bIsActiveDirector)
				{
					GActiveBox3DAsyncDirectors.Add(World, *It);
					for (const uint64 BodyId : PendingRemovalIds)
					{
						(*It)->PendingRemovalIds.Add(BodyId);
					}
					for (const uint64 BodyId : OwnedBodyCreationIds)
					{
						(*It)->OwnedBodyCreationIds.Add(BodyId);
					}
					(*It)->UnclaimedCreationResults.Append(MoveTemp(UnclaimedCreationResults));
					(*It)->ActivateDirector();
					bPromotedReplacement = true;
					break;
				}
			}
		}
		if (!bPromotedReplacement)
		{
			StartPendingRemovalCleanup(World, AsyncSubsystem.Get(), MoveTemp(PendingRemovalIds),
				MoveTemp(OwnedBodyCreationIds), MoveTemp(UnclaimedCreationResults));
		}
	}
	Super::EndPlay(EndPlayReason);
}
