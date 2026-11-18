#include "Box3DAsyncPresentationActor.h"

#include "Box3DAsyncDirector.h"
#include "Box3DIrisReplicationSubsystem.h"
#include "Components/StaticMeshComponent.h"

ABox3DAsyncPresentationActor::ABox3DAsyncPresentationActor()
{
	PrimaryActorTick.bCanEverTick = false;
	PresentationMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PresentationMesh"));
	SetRootComponent(PresentationMesh);
	PresentationMesh->SetMobility(EComponentMobility::Movable);
	PresentationMesh->SetComponentTickEnabled(false);
	SetCollisionDisabled();
}

void ABox3DAsyncPresentationActor::SetCollisionDisabled()
{
	if (PresentationMesh != nullptr)
	{
		PresentationMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		PresentationMesh->SetGenerateOverlapEvents(false);
		PresentationMesh->SetSimulatePhysics(false);
	}
}

void ABox3DAsyncPresentationActor::BeginPlay()
{
	Super::BeginPlay();
	SetCollisionDisabled();
	if (ABox3DAsyncDirector* Director = ABox3DAsyncDirector::FindForWorld(GetWorld()))
	{
		Director->RegisterPresentationActor(this);
	}
}

void ABox3DAsyncPresentationActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
#if WITH_EDITOR
	if (ReplicationPolicy == EBox3DReplicationPolicy::Shared && !ReplicationKey.IsValid())
	{
		ReplicationKey = FGuid::NewGuid();
	}
#endif
}

void ABox3DAsyncPresentationActor::RetryBox3DRegistration()
{
	if (RegistrationState == ERegistrationState::Failed)
	{
		RegistrationState = ERegistrationState::Waiting;
	}
	bWaitingForCapacity = false;
	StaticCollisionAdmissionRetries = 0;
	NextRegistrationAttemptTimeSeconds = 0.0;
	ABox3DAsyncDirector* Director = RegisteredDirector.Get();
	if (Director == nullptr)
	{
		Director = ABox3DAsyncDirector::FindForWorld(GetWorld());
	}
	if (Director != nullptr) Director->RegisterPresentationActor(this);
}

void ABox3DAsyncPresentationActor::SetActorLocRot_Optimized(const FVector& InLoc, const FRotator& InRotator)
{
	PresentationMesh->SetWorldLocationAndRotationNoPhysics(InLoc, InRotator);
}

void ABox3DAsyncPresentationActor::SetNetworkReplica(bool bInNetworkReplica)
{
	bNetworkReplica = bInNetworkReplica;
	if (bNetworkReplica) SetCollisionDisabled();
}

void ABox3DAsyncPresentationActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (ABox3DAsyncDirector* Director = RegisteredDirector.Get())
	{
		Director->UnregisterPresentationActor(this);
	}
	if (GetWorld() != nullptr)
	{
		if (UBox3DIrisReplicationSubsystem* Replication = GetWorld()->GetSubsystem<UBox3DIrisReplicationSubsystem>())
		{
			Replication->UnregisterAuthoredPresentationActor(this);
		}
	}
	RegisteredDirector.Reset();
	Super::EndPlay(EndPlayReason);
}
