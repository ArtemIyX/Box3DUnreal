#include "Box3DIrisReplicationSubsystem.h"

#include "Box3DAsyncPresentationActor.h"
#include "Box3DReplicationSettings.h"
#include "Engine/AssetManager.h"
#include "Engine/NetConnection.h"
#include "Engine/NetDriver.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "HAL/PlatformMisc.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "HAL/PlatformTime.h"
#include "Iris/ReplicationSystem/Filtering/NetObjectFilter.h"
#include "Iris/ReplicationSystem/ObjectReplicationBridge.h"
#include "Iris/ReplicationSystem/ReplicationSystem.h"
#include "Iris/ReplicationSystem/ReplicationSystemTypes.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Net/Core/Connection/ConnectionHandle.h"
#include "Net/UnrealNetwork.h"

namespace
{
	constexpr int32 MaxStagedItemsPerConnectionPerPass = 128;
	constexpr int32 MaxCandidatesPerConnectionPerPass = 512;
	constexpr int32 MaxResidentRowsPerConnection = 512;
	constexpr int32 MaxNewRowsPerConnectionPerPass = 32;
	constexpr int32 MaxLifecycleStagesPerConnectionPerPass = 96;
	constexpr int32 MaxPoseHistory = 8;
	constexpr double MaxInterpolationDelaySeconds = 0.75;
	constexpr uint64 SharedCollisionBit = 1ull;
	constexpr uint64 LocalCollisionBit = 2ull;

	APlayerController* FindController(const UNetConnection* Connection)
	{
		return Connection != nullptr ? Cast<APlayerController>(Connection->OwningActor) : nullptr;
	}
}

void UBox3DIrisReplicationSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	WorldEpoch = FGuid::NewGuid();
	SessionStartSeconds = FPlatformTime::Seconds();
	ServerWorldOrigin = GetWorld() != nullptr && GetWorld()->GetNetMode() != NM_Client
		? FVector(GetWorld()->OriginLocation) : FVector::ZeroVector;
	NetworkOrigin = ServerWorldOrigin;
	PostActorTickHandle = FWorldDelegates::OnWorldPostActorTick.AddUObject(this,
		&UBox3DIrisReplicationSubsystem::OnWorldPostActorTick);
	bSmokeEnabled = FParse::Param(FCommandLine::Get(), TEXT("Box3DNetSmoke"));
	if (bSmokeEnabled)
	{
		FParse::Value(FCommandLine::Get(), TEXT("Box3DNetSmokeBodies="), SmokeBodyTarget);
		FParse::Value(FCommandLine::Get(), TEXT("Box3DNetDeltaCompression="), SmokeDeltaCompression);
		FParse::Value(FCommandLine::Get(), TEXT("Box3DNetSmokeResidentCap="), SmokeResidentCap);
		FParse::Value(FCommandLine::Get(), TEXT("Box3DNetSmokeWitnessSpeed="), SmokeWitnessSpeed);
		FParse::Value(FCommandLine::Get(), TEXT("Box3DNetSmokeMovingBodies="), SmokeMovingBodies);
		if (SmokeBodyTarget <= 0) SmokeBodyTarget = 32;
		SmokeBodyTarget = FMath::Clamp(SmokeBodyTarget, 2, 128);
		SmokeResidentCap = FMath::Clamp(SmokeResidentCap, 1, MaxResidentRowsPerConnection);
		ResidentRowCap = SmokeResidentCap;
		SmokeWitnessSpeed = FMath::Clamp(SmokeWitnessSpeed, 1.0f, 10000.0f);
		SmokeMovingBodies = FMath::Clamp(SmokeMovingBodies, 1, SmokeBodyTarget);
		bSmokeSweepView = FParse::Param(FCommandLine::Get(), TEXT("Box3DNetSmokeSweepView"));
		float Duration = 30.0f;
		FParse::Value(FCommandLine::Get(), TEXT("Box3DNetSmokeDuration="), Duration);
		bSmokeLifecycle = FParse::Param(FCommandLine::Get(), TEXT("Box3DNetSmokeLifecycle"));
		SmokeStartSeconds = SessionStartSeconds;
		SmokeEndSeconds = SmokeStartSeconds + FMath::Max(5.0f, double(Duration));
	}
}

void UBox3DIrisReplicationSubsystem::Deinitialize()
{
	if (PostActorTickHandle.IsValid())
	{
		FWorldDelegates::OnWorldPostActorTick.Remove(PostActorTickHandle);
		PostActorTickHandle.Reset();
	}
	for (FConnectionState& State : Connections)
	{
		RemoveRoot(State.Root);
	}
	Connections.Reset();
	for (TPair<uint64, FAuthorityBody>& Pair : AuthorityBodies) RetireHostVisual(Pair.Value);
	OrderedAuthorityBodyIds.Reset();
	for (const TWeakObjectPtr<UBox3DIrisReplicationRoot>& WeakRoot : ClientRoots)
	{
		UBox3DIrisReplicationRoot* Root = WeakRoot.Get();
		if (IsValid(Root)) Root->DetachAndClear();
	}
	ClientRoots.Reset();
	ClientRootStates.Reset();
	for (TObjectPtr<ABox3DAsyncPresentationActor>& Actor : SpawnedReplicaVisuals)
	{
		if (IsValid(Actor)) Actor->Destroy();
	}
	SpawnedReplicaVisuals.Reset();
	for (TPair<FGuid, FClientVisualState>& Pair : ClientVisuals)
	{
		if (ABox3DAsyncPresentationActor* Actor = Pair.Value.Actor.Get(); IsValid(Actor) && !Actor->IsNetworkReplica())
			Actor->SetActorHiddenInGame(true);
	}
	ClientVisuals.Reset();
	PendingAssetLoads.Reset();
	if (UBox3DAsyncSubsystem* Physics = GetWorld()->GetSubsystem<UBox3DAsyncSubsystem>())
	{
		if (SmokeCreationConsumer.Id != 0) Physics->UnregisterCreationConsumer(SmokeCreationConsumer);
	}
	Super::Deinitialize();
}

void UBox3DIrisReplicationSubsystem::Tick(float DeltaSeconds)
{
	const double Now = FPlatformTime::Seconds();
	const bool bAuthorityOriginStable = CheckAuthorityWorldOrigin();
	if (bSmokeEnabled && SmokeRole.IsEmpty() && GetWorld() != nullptr && GetWorld()->GetNetMode() == NM_Client)
	{
		if (const APlayerController* Controller = GetWorld()->GetFirstPlayerController(); Controller != nullptr
			&& Controller->PlayerState != nullptr)
		{
			SmokeRole = Controller->PlayerState->GetPlayerName();
		}
	}
	if (bSmokeEnabled && !bSmokeStarted) StartSmokeFixture();
	if (bSmokeEnabled && GetWorld() != nullptr && GetWorld()->GetNetMode() == NM_Client && !bClientByteBaselineSet)
	{
		UNetDriver* Driver = GetWorld()->GetNetDriver();
		if (Driver != nullptr && Driver->ServerConnection != nullptr)
		{
			ClientInBytesAtStart = uint64(FMath::Max(0, Driver->ServerConnection->InTotalBytes));
			ClientOutBytesAtStart = uint64(FMath::Max(0, Driver->ServerConnection->OutTotalBytes));
			bClientByteBaselineSet = true;
		}
	}
	PumpSmokeCreationResults();
	if (bAuthorityOriginStable)
	{
		UpdateAuthorityFrames();
		UpdateHostVisuals();
	}
	ApplyClientRoots();
	ApplyVisualHistory(Now);
	if (bSmokeLifecycle && !bSmokeLifecycleDespawned && SmokeLifecycleBodyId != 0
		&& Now - SmokeStartSeconds >= 12.0)
	{
		if (const uint64* NetworkId = NetworkBodyIdsByPhysicsId.Find(SmokeLifecycleBodyId))
		{
			if (FAuthorityBody* Body = AuthorityBodies.Find(*NetworkId))
			{
				const FGuid Key = Body->BodyKey;
				const FBox3DAsyncBodyId PhysicsId = Body->PhysicsHandle.Id;
				UnregisterSharedBody(Key, PhysicsId);
				if (UBox3DAsyncSubsystem* Physics = GetWorld()->GetSubsystem<UBox3DAsyncSubsystem>())
					Physics->TryRemoveBody(PhysicsId);
				bSmokeLifecycleDespawned = true;
				UE_LOG(LogTemp, Display, TEXT("BOX3D_NET_SMOKE_LIFECYCLE despawn body=%llu"),
					static_cast<unsigned long long>(PhysicsId));
			}
		}
	}
	if (bSmokeEnabled && !bSmokeFinished && Now >= SmokeEndSeconds) FinishSmokeRun();
}

TStatId UBox3DIrisReplicationSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UBox3DIrisReplicationSubsystem, STATGROUP_Tickables);
}

bool UBox3DIrisReplicationSubsystem::RegisterAuthoredPresentationActor(ABox3DAsyncPresentationActor* Actor)
{
	check(IsInGameThread());
	if (!IsValid(Actor) || Actor->ReplicationPolicy != EBox3DReplicationPolicy::Shared
		|| Actor->CollisionMode != EBox3DAsyncPresentationMode::AuthoredDynamicCollision
		|| !Actor->ReplicationKey.IsValid()) return false;
	if (const TWeakObjectPtr<ABox3DAsyncPresentationActor>* Existing = AuthoredActorsByKey.Find(Actor->ReplicationKey))
	{
		if (Existing->IsValid() && Existing->Get() != Actor)
		{
			UE_LOG(LogTemp, Error, TEXT("BOX3D_IRIS duplicate authored key=%s actors=%s,%s"),
				*Actor->ReplicationKey.ToString(), *GetNameSafe(Existing->Get()), *GetNameSafe(Actor));
			return false;
		}
	}
	AuthoredActorsByKey.Add(Actor->ReplicationKey, Actor);
	for (TPair<TWeakObjectPtr<UBox3DIrisReplicationRoot>, FClientRootState>& Pair : ClientRootStates)
		Pair.Value.ReceiveRevision = MAX_uint64;
	if (GetWorld() != nullptr && GetWorld()->GetNetMode() == NM_Client)
	{
		Actor->SetActorHiddenInGame(true);
	}
	return true;
}

void UBox3DIrisReplicationSubsystem::UnregisterAuthoredPresentationActor(ABox3DAsyncPresentationActor* Actor)
{
	check(IsInGameThread());
	if (!IsValid(Actor)) return;
	if (const TWeakObjectPtr<ABox3DAsyncPresentationActor>* Existing = AuthoredActorsByKey.Find(Actor->ReplicationKey))
	{
		if (Existing->Get() == Actor) AuthoredActorsByKey.Remove(Actor->ReplicationKey);
	}
}

void UBox3DIrisReplicationSubsystem::RegisterSharedBody(FBox3DAsyncBodyHandle Body,
	ABox3DAsyncPresentationActor* Actor)
{
	check(IsInGameThread());
	if (!Body.World.IsValid() || Body.Id == 0 || !IsValid(Actor) || !Actor->ReplicationKey.IsValid()) return;
	if (Actor->CollisionMode != EBox3DAsyncPresentationMode::AuthoredDynamicCollision) return;
	RegisterActorlessSharedBody(Body, Actor->ReplicationKey,
		Actor->PresentationMesh != nullptr ? FSoftObjectPath(Actor->PresentationMesh->GetStaticMesh()) : FSoftObjectPath(),
		Actor->GetActorScale3D(), true);
}

void UBox3DIrisReplicationSubsystem::RegisterActorlessSharedBody(FBox3DAsyncBodyHandle Body, FGuid BodyKey,
	const FSoftObjectPath& VisualAsset, const FVector& Scale, bool bDynamic)
{
	check(IsInGameThread());
	if (!CheckAuthorityWorldOrigin()) return;
	if (!bDynamic || !Body.World.IsValid() || Body.Id == 0 || !BodyKey.IsValid() || AuthorityBodies.Num() >= 65536) return;
	if (PhysicsBodyIdsByKey.Contains(BodyKey))
	{
		UE_LOG(LogTemp, Error, TEXT("BOX3D_IRIS duplicate shared body key=%s"), *BodyKey.ToString());
		return;
	}
	FAuthorityBody& Record = AuthorityBodies.Add(NextNetworkBodyId);
	Record.NetworkBodyId = NextNetworkBodyId++;
	Record.PhysicsHandle = Body;
	Record.BodyKey = BodyKey;
	Record.Actor = AuthoredActorsByKey.FindRef(BodyKey);
	Record.bAuthoredVisual = Record.Actor.IsValid();
	Record.VisualAsset = VisualAsset;
	Record.Scale = FVector3f(Scale);
	OrderedAuthorityBodyIds.Add(Record.NetworkBodyId);
	PhysicsBodyIdsByKey.Add(BodyKey, Body.Id);
	NetworkBodyIdsByPhysicsId.Add(Body.Id, Record.NetworkBodyId);
}

void UBox3DIrisReplicationSubsystem::UnregisterSharedBody(FGuid BodyKey, FBox3DAsyncBodyId PhysicsBodyId)
{
	check(IsInGameThread());
	const uint64* NetworkBodyId = NetworkBodyIdsByPhysicsId.Find(PhysicsBodyId);
	if (NetworkBodyId == nullptr) return;
	if (FAuthorityBody* Body = AuthorityBodies.Find(*NetworkBodyId))
	{
		RetireHostVisual(*Body);
		OrderedAuthorityBodyIds.RemoveSingle(*NetworkBodyId);
		for (FConnectionState& State : Connections) RemoveBodyFromConnection(*Body, State, true);
		AuthorityBodies.Remove(*NetworkBodyId);
	}
	NetworkBodyIdsByPhysicsId.Remove(PhysicsBodyId);
	PhysicsBodyIdsByKey.Remove(BodyKey);
}

void UBox3DIrisReplicationSubsystem::NotifyBodyDiscontinuity(FGuid BodyKey, FBox3DAsyncBodyId PhysicsBodyId)
{
	check(IsInGameThread());
	const uint64* NetworkBodyId = NetworkBodyIdsByPhysicsId.Find(PhysicsBodyId);
	FAuthorityBody* Body = NetworkBodyId != nullptr ? AuthorityBodies.Find(*NetworkBodyId) : nullptr;
	if (Body != nullptr && Body->BodyKey == BodyKey) ++Body->DiscontinuityRevision;
}

void UBox3DIrisReplicationSubsystem::AddClientRoot(UBox3DIrisReplicationRoot* Root)
{
	check(IsInGameThread());
	if (IsValid(Root))
	{
		if (!ClientRoots.IsEmpty() && !ClientRoots.Contains(Root))
		{
			for (TPair<FGuid, FClientVisualState>& Pair : ClientVisuals)
			{
				if (ABox3DAsyncPresentationActor* Actor = Pair.Value.Actor.Get()) Actor->SetActorHiddenInGame(true);
				Pair.Value.History.Reset();
			}
			ClockOffsetSeconds = 0.0;
			bHasClockOffset = false;
		}
		if (!ClientRoots.Contains(Root)) ClientRoots.Add(Root);
		if (!StrongRoots.Contains(Root)) StrongRoots.Add(Root);
		UE_LOG(LogTemp, Display, TEXT("BOX3D_IRIS_ROOT_ATTACH root=%s epoch=%s origin=(%.3f,%.3f,%.3f) revision=%u"),
			*GetNameSafe(Root), *Root->GetWorldEpoch().ToString(), Root->GetNetworkOrigin().X,
			Root->GetNetworkOrigin().Y, Root->GetNetworkOrigin().Z, Root->GetOriginRevision());
	}
}

void UBox3DIrisReplicationSubsystem::RemoveClientRoot(UBox3DIrisReplicationRoot* Root)
{
	check(IsInGameThread());
	ClientRoots.Remove(Root);
	ClientRootStates.Remove(Root);
	StrongRoots.Remove(Root);
	UE_LOG(LogTemp, Display, TEXT("BOX3D_IRIS_ROOT_DETACH root=%s"), *GetNameSafe(Root));
	ApplyClientRoots();
}

void UBox3DIrisReplicationSubsystem::OnWorldPostActorTick(UWorld* World, ELevelTick TickType, float DeltaSeconds)
{
	if (World != GetWorld()) return;
	if (CheckAuthorityWorldOrigin()) StageAuthorityForConnections();
}

bool UBox3DIrisReplicationSubsystem::CheckAuthorityWorldOrigin()
{
	UWorld* World = GetWorld();
	if (World == nullptr || World->GetNetMode() == NM_Client) return true;
	const FVector CurrentOrigin(World->OriginLocation);
	if (!Box3DIrisReplication::ShouldSuspendForAuthorityOriginShift(ServerWorldOrigin,
		CurrentOrigin, bAuthorityOriginMismatch)) return true;
	if (!bAuthorityOriginMismatch)
	{
		bAuthorityOriginMismatch = true;
		if (!bOriginMismatchLogged)
		{
			UE_LOG(LogTemp, Error,
				TEXT("BOX3D_IRIS authority world origin shifted from the frozen worker frame; retiring roots until world reload"));
			bOriginMismatchLogged = true;
		}
		SuspendAuthorityReplicationForOriginShift();
	}
	return false;
}

void UBox3DIrisReplicationSubsystem::SuspendAuthorityReplicationForOriginShift()
{
	for (FConnectionState& State : Connections) RemoveRoot(State.Root);
	Connections.Reset();
	for (TPair<uint64, FAuthorityBody>& Pair : AuthorityBodies) RetireHostVisual(Pair.Value);
}

void UBox3DIrisReplicationSubsystem::UpdateAuthorityFrames()
{
	if (!CheckAuthorityWorldOrigin()) return;
	UWorld* World = GetWorld();
	UBox3DAsyncSubsystem* Physics = World ? World->GetSubsystem<UBox3DAsyncSubsystem>() : nullptr;
	if (Physics == nullptr || AuthorityBodies.IsEmpty()) return;
	FBox3DAsyncPoseFrame Frame;
	if (!Physics->TryGetLatestPoseFrame(Frame)) return;
	for (const FBox3DAsyncPose& Pose : Frame.Poses)
	{
		const uint64* NetworkId = NetworkBodyIdsByPhysicsId.Find(Pose.BodyId);
		FAuthorityBody* Body = NetworkId ? AuthorityBodies.Find(*NetworkId) : nullptr;
		if (Body == nullptr || Body->PhysicsHandle.World != Frame.World) continue;
		if (Body->bHasPose && FVector::Distance(Body->LatestPose.Transform.GetLocation(),
			Pose.Transform.GetLocation()) > 500.0)
		{
			++Body->DiscontinuityRevision;
		}
		Body->LatestPose = Pose;
		Body->CaptureSerial = Frame.CaptureSerial;
		Body->CaptureMonotonicSeconds = Frame.CaptureMonotonicSeconds;
		Body->bAwake = Pose.bAwake;
		Body->bHasPose = true;
	}
}


void UBox3DIrisReplicationSubsystem::UpdateHostVisuals()
{
	UWorld* World = GetWorld();
	if (World == nullptr || World->GetNetMode() != NM_ListenServer) return;
	for (TPair<uint64, FAuthorityBody>& Pair : AuthorityBodies)
	{
		FAuthorityBody& Body = Pair.Value;
		if (Body.bAuthoredVisual || !Body.bHasPose) continue;
		FTransform Transform = Body.LatestPose.Transform;
		Transform.SetScale3D(FVector(Body.Scale));
		ABox3DAsyncPresentationActor* Actor = Body.HostVisual.Get();
		if (!IsValid(Actor))
		{
			Actor = SpawnVisual(Body.BodyKey, Body.VisualAsset, Transform);
			Body.HostVisual = Actor;
		}
		if (IsValid(Actor)) Actor->SetActorTransform(Transform);
	}
}

void UBox3DIrisReplicationSubsystem::RetireHostVisual(FAuthorityBody& Body)
{
	PendingAssetLoads.Remove(Body.BodyKey);
	if (ABox3DAsyncPresentationActor* Actor = Body.HostVisual.Get())
	{
		SpawnedReplicaVisuals.Remove(Actor);
		Actor->Destroy();
	}
	Body.HostVisual.Reset();
}

bool UBox3DIrisReplicationSubsystem::TryStartRoot(UNetConnection* Connection, FConnectionState& State)
{
	if (bAuthorityOriginMismatch || !IsValid(Connection) || Connection->GetConnectionState() != USOCK_Open || Connection->Driver == nullptr
		|| FindController(Connection) == nullptr) return false;
	UNetDriver* Driver = Connection->Driver;
	UReplicationSystem* ReplicationSystem = Driver->GetReplicationSystem();
	UObjectReplicationBridge* Bridge = ReplicationSystem
		? ReplicationSystem->GetReplicationBridgeAs<UObjectReplicationBridge>() : nullptr;
	const UE::Net::FNetObjectFactoryId FactoryId = UE::Net::FNetObjectFactoryRegistry::GetFactoryIdFromName(
		Box3DIrisReplication::FactoryName);
	if (ReplicationSystem == nullptr || !ReplicationSystem->IsServer() || Bridge == nullptr
		|| !UE::Net::FNetObjectFactoryRegistry::IsValidFactoryId(FactoryId))
	{
		return false;
	}
	const UE::Net::FConnectionHandle ConnectionHandle = Connection->GetConnectionHandle();
	const uint32 ConnectionId = ConnectionHandle.GetParentConnectionId();
	if (!ConnectionHandle.IsParentConnection() || !ReplicationSystem->IsValidConnection(ConnectionId)) return false;
	UBox3DIrisReplicationRoot* Root = NewObject<UBox3DIrisReplicationRoot>(GetTransientPackage());
	Root->SetReplicationWorld(GetWorld());
	Root->SetNetworkMetadata(WorldEpoch, NetworkOrigin, OriginRevision);
	UObjectReplicationBridge::FRootObjectReplicationParams Params;
	Params.bNeedsWorldLocationUpdate = false;
	Params.bUseClassConfigDynamicFilter = false;
	Params.bUseExplicitDynamicFilter = false;
	Params.StaticPriority = 1.0f;
	Params.PollFrequency = 0.0f;
	const UE::Net::FNetRefHandle Handle = Bridge->StartReplicatingRootObject(Root, Params, FactoryId);
	if (!Handle.IsValid()) return false;
	if (ConnectionId == 0 || !ReplicationSystem->SetFilter(Handle, UE::Net::ToOwnerFilterHandle))
	{
		Bridge->StopReplicatingNetObject(Root);
		return false;
	}
	ReplicationSystem->SetOwningNetConnection(Handle, ConnectionId);
	if (bSmokeEnabled)
	{
		ReplicationSystem->SetDeltaCompressionStatus(Handle, SmokeDeltaCompression != 0
			? UE::Net::ENetObjectDeltaCompressionStatus::Allow
			: UE::Net::ENetObjectDeltaCompressionStatus::Disallow);
	}
	if (ReplicationSystem->GetOwningNetConnection(Handle) != ConnectionId)
	{
		Bridge->StopReplicatingNetObject(Root);
		return false;
	}
	State.Connection = Connection;
	State.Driver = Driver;
	State.Root = Root;
	State.RootHandle = Handle;
	if (const APlayerController* Controller = FindController(Connection); Controller != nullptr
		&& Controller->PlayerState != nullptr) State.SmokeRole = Controller->PlayerState->GetPlayerName();
	++SmokeRootsObserved;
	State.InitialOutBytes = uint64(FMath::Max<int64>(0, Connection->OutTotalBytes));
	State.LastOutBytes = State.InitialOutBytes;
	State.ConnectionStartSeconds = FPlatformTime::Seconds();
	StrongRoots.Add(Root);
	FVector ViewLocation = FVector::ZeroVector;
	GetConnectionViewLocation(Connection, ViewLocation);
	UE_LOG(LogTemp, Display,
		TEXT("BOX3D_IRIS_ROOT_STARTED owner=%u role=%s view=(%.1f,%.1f,%.1f) handle=%u factory=%u delta_compression=%d"),
		ConnectionId, *State.SmokeRole, ViewLocation.X, ViewLocation.Y, ViewLocation.Z,
		Handle.GetId(), uint32(FactoryId), bSmokeEnabled ? SmokeDeltaCompression : -1);
	return true;
}

bool UBox3DIrisReplicationSubsystem::GetConnectionViewLocation(const UNetConnection* Connection,
	FVector& OutLocation) const
{
	if (bSmokeEnabled)
	{
		const APlayerController* Controller = FindController(Connection);
		const FString PlayerName = Controller && Controller->PlayerState
			? Controller->PlayerState->GetPlayerName() : FString();
		if (PlayerName.Contains(TEXT("Box3DFar"), ESearchCase::IgnoreCase)
			|| SmokeRole.Equals(TEXT("Far"), ESearchCase::IgnoreCase))
		{
			const double Elapsed = FPlatformTime::Seconds() - SmokeStartSeconds;
			OutLocation = bSmokeLifecycle && Elapsed >= 4.0 && Elapsed < 7.0
				? FVector(25000.0, 0.0, 0.0) : FVector(7000.0, 0.0, 0.0);
			return true;
		}
		if (PlayerName.Contains(TEXT("Box3DNear"), ESearchCase::IgnoreCase)
			|| SmokeRole.Equals(TEXT("Near"), ESearchCase::IgnoreCase))
		{
			const double Elapsed = FPlatformTime::Seconds() - SmokeStartSeconds;
			if (bSmokeSweepView)
			{
				const double Phase = FMath::Fmod(Elapsed, 24.0);
				const double SweepDistance = Phase < 6.0 ? Phase * 500.0
					: (Phase < 12.0 ? 3000.0 + (Phase - 6.0) * 416.6666667
						: (Phase < 18.0 ? 5500.0 - (Phase - 12.0) * 916.6666667 : 0.0));
				OutLocation = FVector(SweepDistance, 0.0, 0.0);
				return true;
			}
			OutLocation = bSmokeLifecycle && Elapsed >= 4.0 && Elapsed < 7.0
				? FVector(25000.0, 0.0, 0.0) : FVector::ZeroVector;
			return true;
		}
		if (bSmokeEnabled) return false;
	}
	if (!IsValid(Connection)) return false;
	if (const APlayerController* Controller = FindController(Connection))
	{
		if (Controller->PlayerCameraManager == nullptr && Controller->GetViewTarget() == nullptr) return false;
		FRotator Rotation;
		Controller->GetPlayerViewPoint(OutLocation, Rotation);
		return FMath::IsFinite(OutLocation.X) && FMath::IsFinite(OutLocation.Y) && FMath::IsFinite(OutLocation.Z);
	}
	return false;
}

void UBox3DIrisReplicationSubsystem::StageAuthorityForConnections()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Box3DIris_StageAuthorityForConnections);
	UWorld* World = GetWorld();
	RemoveDisconnectedConnections();
	UNetDriver* Driver = World ? World->GetNetDriver() : nullptr;
	if (World == nullptr || Driver == nullptr || World->GetNetMode() == NM_Client || !CheckAuthorityWorldOrigin()) return;
	const UBox3DReplicationSettings* Settings = GetDefault<UBox3DReplicationSettings>();
	const TArray<Box3DIrisReplication::FDistanceTier> Tiers = Box3DIrisReplication::GetDistanceTiers();
	const double CullDistance = Tiers.Last().MaxDistance;
	const double CullHysteresis = FMath::IsFinite(Settings->CullHysteresis) ? FMath::Max(0.0, double(Settings->CullHysteresis)) : 0.0;
	const double NearTierDistance = Tiers[0].MaxDistance;
	const double MidTierDistance = Tiers[FMath::Min(1, Tiers.Num() - 1)].MaxDistance;
	for (UNetConnection* Connection : Driver->ClientConnections)
	{
		if (!IsValid(Connection)) continue;
		FVector ConnectionView;
		if (Connection->GetConnectionState() != USOCK_Open || FindController(Connection) == nullptr
			|| !GetConnectionViewLocation(Connection, ConnectionView)) continue;
		ConnectionView = Box3DIrisReplication::ToAbsoluteWorldPosition(ConnectionView, ServerWorldOrigin);
		const UE::Net::FConnectionHandle ConnectionHandle = Connection->GetConnectionHandle();
		if (!ConnectionHandle.IsParentConnection() || ConnectionHandle.GetParentConnectionId() == 0) continue;
		FConnectionState* State = Connections.FindByPredicate([Connection](const FConnectionState& Entry)
		{
			return Entry.Connection.Get() == Connection && Entry.Driver.Get() == Connection->Driver;
		});
		if (State == nullptr)
		{
			State = &Connections.AddDefaulted_GetRef();
			if (!TryStartRoot(Connection, *State))
			{
				Connections.Pop(EAllowShrinking::No);
				continue;
			}
		}
		if (!IsValid(State->Root)) continue;
		FBox3DIrisBodyArray& Array = State->Root->GetBodyArray();
		const TArray<uint64>& BodyIds = OrderedAuthorityBodyIds;
		TArray<FVector> Views;
		Views.Add(ConnectionView);
		for (const UChildConnection* Child : Connection->Children)
		{
			FVector ChildView;
			if (Child != nullptr && GetConnectionViewLocation(Child, ChildView))
				Views.Add(Box3DIrisReplication::ToAbsoluteWorldPosition(ChildView, ServerWorldOrigin));
		}
		const double Now = FPlatformTime::Seconds();
		State->LastOutBytes = uint64(FMath::Max<int64>(0, Connection->OutTotalBytes));
		if (bSmokeEnabled && State->SteadyOutBytes == 0 && Now - SmokeStartSeconds >= 2.0)
			State->SteadyOutBytes = uint64(FMath::Max(0, Connection->OutTotalBytes));

		struct FScheduleCandidate
		{
			uint64 BodyId = 0;
			double Distance = 0.0;
			double Interval = 0.0;
			double Deadline = 0.0;
			bool bNewRow = false;
		};
		TArray<FScheduleCandidate> LifecycleCandidates;
		TArray<FScheduleCandidate> RoutineCandidates;
		TArray<uint64> RemovalIds;

		// Inspect all bounded residents first, so culls and lifecycle transitions are not
		// delayed by the rotating discovery window. Compact rows and rebuild indices once.
		for (const FBox3DIrisBodyItem& Item : Array.Items)
		{
			const uint64 NetworkBodyId = Item.BodyId;
			FAuthorityBody* Body = AuthorityBodies.Find(NetworkBodyId);
			if (Body == nullptr)
			{
				RemovalIds.Add(NetworkBodyId);
				continue;
			}
			if (!Body->bHasPose) continue;
			if (Body->bAuthoredVisual)
			{
				ABox3DAsyncPresentationActor* AuthoredActor = Body->Actor.Get();
				if (!IsValid(AuthoredActor) || !AuthoredActor->HasActorBegunPlay()
					|| !Driver->IsLevelInitializedForActor(AuthoredActor, Connection))
				{
					RemovalIds.Add(NetworkBodyId);
					continue;
				}
			}
			const FVector AbsolutePosition = Box3DIrisReplication::ToAbsoluteWorldPosition(
				Body->LatestPose.Transform.GetLocation(), ServerWorldOrigin);
			double Distance = TNumericLimits<double>::Max();
			for (const FVector& View : Views) Distance = FMath::Min(Distance, FVector::Distance(View, AbsolutePosition));
			if (Distance > CullDistance + CullHysteresis)
			{
				RemovalIds.Add(NetworkBodyId);
				continue;
			}
			const double PreviousInterval = State->Intervals.FindRef(NetworkBodyId);
			const double Interval = Box3DIrisReplication::ComputeTierInterval(Distance, PreviousInterval, Tiers, Settings->TierHysteresis);
			double& NextDeadline = State->NextDeadlines.FindOrAdd(NetworkBodyId, Now);
			if (PreviousInterval <= 0.0 || Interval < PreviousInterval) NextDeadline = Now;
			State->Intervals.Add(NetworkBodyId, Interval);
			State->Distances.Add(NetworkBodyId, Distance);
			const bool bDiscontinuity = Item.DiscontinuityRevision != Body->DiscontinuityRevision;
			const bool bAwakeChanged = Item.bAwake != Body->bAwake;
			const bool bOriginChanged = Item.OriginRevision != OriginRevision;
			if (bDiscontinuity || bAwakeChanged || bOriginChanged)
				LifecycleCandidates.Add({NetworkBodyId, Distance, Interval, NextDeadline, false});
			else if (Box3DIrisReplication::IsDeadlineDue(Now, NextDeadline))
			{
				++State->DueSamples;
				RoutineCandidates.Add({NetworkBodyId, Distance, Interval, NextDeadline, false});
			}
		}
		RemoveBodiesFromConnection(RemovalIds, *State, false);

		// Discovery is bounded and rotates independently of resident service. If full, only
		// a nearer candidate (with body ID as a stable tie-breaker) can evict a resident.
		TArray<FScheduleCandidate> AdmissionCandidates;
		const int32 DiscoveryStart = BodyIds.IsEmpty() ? 0 : State->DiscoveryCursor % BodyIds.Num();
		const int32 DiscoveryCount = FMath::Min(BodyIds.Num(), MaxCandidatesPerConnectionPerPass);
		for (int32 CandidateIndex = 0; CandidateIndex < DiscoveryCount; ++CandidateIndex)
		{
			const uint64 NetworkBodyId = BodyIds[(DiscoveryStart + CandidateIndex) % BodyIds.Num()];
			if (State->ItemIndices.Contains(NetworkBodyId)) continue;
			FAuthorityBody* Body = AuthorityBodies.Find(NetworkBodyId);
			if (Body == nullptr || !Body->bHasPose) continue;
			if (Body->bAuthoredVisual)
			{
				ABox3DAsyncPresentationActor* AuthoredActor = Body->Actor.Get();
				if (!IsValid(AuthoredActor) || !AuthoredActor->HasActorBegunPlay()
					|| !Driver->IsLevelInitializedForActor(AuthoredActor, Connection)) continue;
			}
			const FVector AbsolutePosition = Box3DIrisReplication::ToAbsoluteWorldPosition(
				Body->LatestPose.Transform.GetLocation(), ServerWorldOrigin);
			double Distance = TNumericLimits<double>::Max();
			for (const FVector& View : Views) Distance = FMath::Min(Distance, FVector::Distance(View, AbsolutePosition));
			if (Distance > CullDistance) continue;
			const double Interval = Box3DIrisReplication::ComputeTierInterval(Distance, 0.0, Tiers, Settings->TierHysteresis);
			AdmissionCandidates.Add({NetworkBodyId, Distance, Interval, Now + Interval, true});
		}
		if (!AdmissionCandidates.IsEmpty())
		{
			AdmissionCandidates.Sort([](const FScheduleCandidate& A, const FScheduleCandidate& B)
			{
				return Box3DIrisReplication::IsAdmissionPreferred(A.Distance, A.BodyId, B.Distance, B.BodyId);
			});
		}
		if (!BodyIds.IsEmpty())
			State->DiscoveryCursor = Box3DIrisReplication::ComputeNextCandidateCursor(
				DiscoveryStart, DiscoveryCount, BodyIds.Num(), INDEX_NONE);

		int32 PendingAdmissions = 0;
		for (const FScheduleCandidate& Candidate : AdmissionCandidates)
		{
			if (PendingAdmissions >= MaxNewRowsPerConnectionPerPass) break;
			if (Array.Items.Num() >= ResidentRowCap && !State->ItemIndices.IsEmpty())
			{
				uint64 FarthestBodyId = 0;
				double FarthestDistance = -1.0;
				for (const TPair<uint64, int32>& Resident : State->ItemIndices)
				{
					const double ResidentDistance = State->Distances.FindRef(Resident.Key);
					if (FarthestBodyId == 0 || ResidentDistance > FarthestDistance
						|| (ResidentDistance == FarthestDistance && Resident.Key > FarthestBodyId))
					{
						FarthestBodyId = Resident.Key;
						FarthestDistance = ResidentDistance;
					}
				}
				if (FarthestBodyId == 0 || !Box3DIrisReplication::IsAdmissionPreferred(
					Candidate.Distance, Candidate.BodyId, FarthestDistance, FarthestBodyId)) continue;
			}
			LifecycleCandidates.Add(Candidate);
			++PendingAdmissions;
		}

		if (!LifecycleCandidates.IsEmpty())
		{
			LifecycleCandidates.Sort([](const FScheduleCandidate& A, const FScheduleCandidate& B)
			{
				return A.BodyId < B.BodyId;
			});
		}
		if (!RoutineCandidates.IsEmpty())
		{
			RoutineCandidates.Sort([](const FScheduleCandidate& A, const FScheduleCandidate& B)
			{
				if (A.Deadline != B.Deadline) return A.Deadline < B.Deadline;
				return A.BodyId < B.BodyId;
			});
		}
		const Box3DIrisReplication::FStageBudget WorkBudget = Box3DIrisReplication::ComputeStageBudget(
			LifecycleCandidates.Num(), RoutineCandidates.Num(), MaxStagedItemsPerConnectionPerPass,
			MaxLifecycleStagesPerConnectionPerPass);

		auto StageCandidate = [&](const FScheduleCandidate& Candidate)
		{
			FAuthorityBody* Body = AuthorityBodies.Find(Candidate.BodyId);
			if (Body == nullptr || !Body->bHasPose) return false;
			FVector Position;
			const FVector AbsolutePosition = Box3DIrisReplication::ToAbsoluteWorldPosition(
				Body->LatestPose.Transform.GetLocation(), ServerWorldOrigin);
			if (!Box3DIrisReplication::TryQuantizePositionToCentimeter(AbsolutePosition - NetworkOrigin, Position)) return false;
			const FVector_NetQuantize QuantizedPosition(Position);
			const FQuat Q = Body->LatestPose.Transform.GetRotation().GetNormalized();
			const FQuat4f QuantizedRotation = FQuat4f(float(Q.X), float(Q.Y), float(Q.Z), float(Q.W)).GetNormalized();
			int32* ExistingIndex = State->ItemIndices.Find(Candidate.BodyId);
			FBox3DIrisBodyItem* Item = nullptr;
			if (Candidate.bNewRow)
			{
				if (ExistingIndex != nullptr) return false;
				if (Array.Items.Num() >= ResidentRowCap)
				{
					uint64 FarthestBodyId = 0;
					double FarthestDistance = -1.0;
					for (const TPair<uint64, int32>& Resident : State->ItemIndices)
					{
						const double ResidentDistance = State->Distances.FindRef(Resident.Key);
						if (FarthestBodyId == 0 || ResidentDistance > FarthestDistance
							|| (ResidentDistance == FarthestDistance && Resident.Key > FarthestBodyId))
						{
							FarthestBodyId = Resident.Key;
							FarthestDistance = ResidentDistance;
						}
					}
					if (FarthestBodyId == 0 || !Box3DIrisReplication::IsAdmissionPreferred(
						Candidate.Distance, Candidate.BodyId, FarthestDistance, FarthestBodyId)) return false;
					TArray<uint64> CapacityEviction;
					CapacityEviction.Add(FarthestBodyId);
					RemoveBodiesFromConnection(CapacityEviction, *State, false, true);
					++State->CapacityAdmissions;
				}
				Item = &Array.Items.AddDefaulted_GetRef();
				Item->BodyId = Candidate.BodyId;
				Item->BodyKey = Body->BodyKey;
				Item->RelevanceRevision = ++State->NextRelevanceRevision;
				Item->bAuthoredVisual = Body->bAuthoredVisual;
				Item->VisualAsset = Body->VisualAsset;
				Item->VisualScale = Body->Scale;
				State->ItemIndices.Add(Candidate.BodyId, Array.Items.Num() - 1);
				if (bSmokeEnabled)
				{
					if (State->EverRelevantBodyIds.Contains(Candidate.BodyId)) ++State->Reentries;
					else ++State->InitialItems;
					State->EverRelevantBodyIds.Add(Candidate.BodyId);
				}
				else ++State->InitialItems;
			}
			else
			{
				if (ExistingIndex == nullptr || !Array.Items.IsValidIndex(*ExistingIndex)) return false;
				Item = &Array.Items[*ExistingIndex];
			}
			const bool bPoseChanged = Candidate.bNewRow || Item->Position != QuantizedPosition
				|| !Item->Rotation.Equals(QuantizedRotation, 0.0001f)
				|| Item->DiscontinuityRevision != Body->DiscontinuityRevision
				|| Item->bAwake != Body->bAwake || Item->OriginRevision != OriginRevision
				|| Item->ReplicationIntervalSeconds != float(Candidate.Interval);
			if (!Candidate.bNewRow)
			{
				double& NextDeadline = State->NextDeadlines.FindOrAdd(Candidate.BodyId, Now);
				NextDeadline = Box3DIrisReplication::AdvanceDeadline(Now, Candidate.Interval, NextDeadline);
			}
			else State->NextDeadlines.Add(Candidate.BodyId, Now + Candidate.Interval);
			State->Intervals.Add(Candidate.BodyId, Candidate.Interval);
			State->Distances.Add(Candidate.BodyId, Candidate.Distance);
			if (!bPoseChanged)
			{
				++State->UnchangedSamples;
				return true;
			}
			Item->Position = QuantizedPosition;
			Item->Rotation = QuantizedRotation;
			Item->DiscontinuityRevision = Body->DiscontinuityRevision;
			Item->OriginRevision = OriginRevision;
			Item->CaptureSequence = uint32(Body->CaptureSerial);
			Item->CaptureSeconds = Body->CaptureMonotonicSeconds - SessionStartSeconds;
			Item->ReplicationIntervalSeconds = float(Candidate.Interval);
			Item->bAwake = Body->bAwake;
			Array.MarkItemDirty(*Item);
			++State->StagedSamples;
			if (Candidate.BodyId == SmokeWitnessNetworkBodyId) ++State->WitnessSamples;
			if (bSmokeEnabled && Candidate.BodyId == SmokeWitnessNetworkBodyId)
				UE_LOG(LogTemp, Display, TEXT("BOX3D_NET_WITNESS_SENT epoch=%s body=%llu seq=%u x=%.3f y=%.3f z=%.3f"),
					*WorldEpoch.ToString(EGuidFormats::Digits), static_cast<unsigned long long>(Candidate.BodyId),
					Item->CaptureSequence, double(Item->Position.X), double(Item->Position.Y), double(Item->Position.Z));
			if (Candidate.Distance <= NearTierDistance) ++State->NearSamples;
			else if (Candidate.Distance <= MidTierDistance) ++State->MidSamples;
			else ++State->FarSamples;
			return true;
		};

		int32 LifecycleStart = 0;
		for (int32 Index = 0; Index < LifecycleCandidates.Num(); ++Index)
		{
			if (LifecycleCandidates[Index].BodyId > State->LifecycleCursorBodyId)
			{
				LifecycleStart = Index;
				break;
			}
		}
		for (int32 Index = 0; Index < WorkBudget.Lifecycle; ++Index)
		{
			const FScheduleCandidate& Candidate = LifecycleCandidates[(LifecycleStart + Index) % LifecycleCandidates.Num()];
			if (StageCandidate(Candidate)) State->LifecycleCursorBodyId = Candidate.BodyId;
		}
		for (int32 Index = 0; Index < WorkBudget.Routine; ++Index) StageCandidate(RoutineCandidates[Index]);
		State->DeferredSamples += uint64(LifecycleCandidates.Num() - WorkBudget.Lifecycle
			+ RoutineCandidates.Num() - WorkBudget.Routine);
	}
}
void UBox3DIrisReplicationSubsystem::RemoveDisconnectedConnections()
{
	UWorld* World = GetWorld();
	UNetDriver* CurrentDriver = World ? World->GetNetDriver() : nullptr;
	for (int32 Index = Connections.Num() - 1; Index >= 0; --Index)
	{
		FConnectionState& State = Connections[Index];
		UNetConnection* Connection = State.Connection.Get();
		const bool bStillConnected = CurrentDriver != nullptr && CurrentDriver == State.Driver.Get()
			&& CurrentDriver->ClientConnections.Contains(Connection);
		if (bStillConnected) continue;
		DisconnectedStagedSamples += State.StagedSamples;
		DisconnectedInitialItems += State.InitialItems;
		DisconnectedNearSamples += State.NearSamples;
		DisconnectedMidSamples += State.MidSamples;
		DisconnectedFarSamples += State.FarSamples;
		DisconnectedCullRemovals += State.CullRemovals;
		DisconnectedReentries += State.Reentries;
		DisconnectedDespawnRemovals += State.DespawnRemovals;
		DisconnectedDueSamples += State.DueSamples;
		DisconnectedUnchangedSamples += State.UnchangedSamples;
		DisconnectedDeferredSamples += State.DeferredSamples;
		DisconnectedCapacityAdmissions += State.CapacityAdmissions;
		if (Connection != nullptr)
		{
			const uint64 CurrentBytes = uint64(FMath::Max<int64>(0, Connection->OutTotalBytes));
			const uint64 SteadyMark = State.SteadyOutBytes != 0 ? State.SteadyOutBytes : State.InitialOutBytes;
			DisconnectedOutBytes += CurrentBytes >= State.InitialOutBytes ? CurrentBytes - State.InitialOutBytes : 0;
			DisconnectedBaselineOutBytes += SteadyMark >= State.InitialOutBytes ? SteadyMark - State.InitialOutBytes : 0;
			DisconnectedSteadyOutBytes += CurrentBytes >= SteadyMark ? CurrentBytes - SteadyMark : 0;
		}
		else
		{
			const uint64 CurrentBytes = State.LastOutBytes;
			const uint64 SteadyMark = State.SteadyOutBytes != 0 ? State.SteadyOutBytes : State.InitialOutBytes;
			DisconnectedOutBytes += CurrentBytes >= State.InitialOutBytes ? CurrentBytes - State.InitialOutBytes : 0;
			DisconnectedBaselineOutBytes += SteadyMark >= State.InitialOutBytes ? SteadyMark - State.InitialOutBytes : 0;
			DisconnectedSteadyOutBytes += CurrentBytes >= SteadyMark ? CurrentBytes - SteadyMark : 0;
		}
		if (bSmokeEnabled)
		{
			const double Seconds = FMath::Max(0.1, FPlatformTime::Seconds() - State.ConnectionStartSeconds);
			const double WitnessHz = double(State.WitnessSamples > 0 ? State.WitnessSamples - 1 : 0) / Seconds;
			UE_LOG(LogTemp, Display,
				TEXT("BOX3D_NET_SMOKE_ROOT_END role=%s initial=%llu samples=%llu witness_samples=%llu witness_hz=%.2f near=%llu mid=%llu far=%llu culls=%llu reentries=%llu despawns=%llu"),
				*State.SmokeRole, static_cast<unsigned long long>(State.InitialItems),
				static_cast<unsigned long long>(State.StagedSamples),
				static_cast<unsigned long long>(State.WitnessSamples), WitnessHz,
				static_cast<unsigned long long>(State.NearSamples), static_cast<unsigned long long>(State.MidSamples),
				static_cast<unsigned long long>(State.FarSamples), static_cast<unsigned long long>(State.CullRemovals),
				static_cast<unsigned long long>(State.Reentries), static_cast<unsigned long long>(State.DespawnRemovals));
		}
		RemoveRoot(State.Root);
		Connections.RemoveAtSwap(Index, 1, EAllowShrinking::No);
	}
}

void UBox3DIrisReplicationSubsystem::RemoveRoot(UBox3DIrisReplicationRoot* Root)
{
	if (!IsValid(Root)) return;
	UWorld* World = Root->GetWorld();
	UNetDriver* Driver = nullptr;
	for (const FConnectionState& State : Connections)
	{
		if (State.Root == Root) { Driver = State.Driver.Get(); break; }
	}
	if (Driver == nullptr) Driver = World ? World->GetNetDriver() : nullptr;
	UReplicationSystem* ReplicationSystem = Driver ? Driver->GetReplicationSystem() : nullptr;
	UObjectReplicationBridge* Bridge = ReplicationSystem
		? ReplicationSystem->GetReplicationBridgeAs<UObjectReplicationBridge>() : nullptr;
	if (Bridge != nullptr) Bridge->StopReplicatingNetObject(Root);
	Root->DetachAndClear();
	StrongRoots.Remove(Root);
}

void UBox3DIrisReplicationSubsystem::RemoveBodyFromConnection(FAuthorityBody& Body, FConnectionState& State,
	bool bDespawn)
{
	TArray<uint64> BodyIds;
	BodyIds.Add(Body.NetworkBodyId);
	RemoveBodiesFromConnection(BodyIds, State, bDespawn);
}

void UBox3DIrisReplicationSubsystem::RemoveBodiesFromConnection(const TArray<uint64>& BodyIds,
	FConnectionState& State, bool bDespawn, bool bCapacity)
{
	if (BodyIds.Num() == 1)
	{
		const uint64 BodyId = BodyIds[0];
		int32 Index = INDEX_NONE;
		const bool bWasResident = State.ItemIndices.RemoveAndCopyValue(BodyId, Index);
		if (bWasResident)
		{
			if (bDespawn) ++State.DespawnRemovals;
			else if (bCapacity) ++State.CapacityRemovals;
			else ++State.CullRemovals;
		}
		State.NextDeadlines.Remove(BodyId);
		State.Intervals.Remove(BodyId);
		State.Distances.Remove(BodyId);
		if (bDespawn) State.EverRelevantBodyIds.Remove(BodyId);
		if (bWasResident && State.Root != nullptr)
		{
			FBox3DIrisBodyArray& Array = State.Root->GetBodyArray();
			if (Array.Items.IsValidIndex(Index))
			{
				Array.Items.RemoveAtSwap(Index, 1, EAllowShrinking::No);
				Array.MarkArrayDirty();
				if (Array.Items.IsValidIndex(Index)) State.ItemIndices.Add(Array.Items[Index].BodyId, Index);
			}
		}
		return;
	}
	TSet<uint64> UniqueBodyIds;
	for (const uint64 BodyId : BodyIds)
	{
		if (UniqueBodyIds.Contains(BodyId)) continue;
		UniqueBodyIds.Add(BodyId);
		const bool bWasResident = State.ItemIndices.Contains(BodyId);
		if (bWasResident)
		{
			if (bDespawn) ++State.DespawnRemovals;
			else if (bCapacity) ++State.CapacityRemovals;
			else ++State.CullRemovals;
		}
		State.NextDeadlines.Remove(BodyId);
		State.Intervals.Remove(BodyId);
		State.Distances.Remove(BodyId);
		if (bDespawn)
		{
			State.EverRelevantBodyIds.Remove(BodyId);
					}
	}
	if (State.Root == nullptr || UniqueBodyIds.IsEmpty()) return;
	FBox3DIrisBodyArray& Array = State.Root->GetBodyArray();
	const int32 RemovedCount = Array.Items.RemoveAll([&UniqueBodyIds](const FBox3DIrisBodyItem& Item)
	{
		return UniqueBodyIds.Contains(Item.BodyId);
	});
	if (RemovedCount > 0) Array.MarkArrayDirty();
	State.ItemIndices.Reset();
	for (int32 ItemIndex = 0; ItemIndex < Array.Items.Num(); ++ItemIndex)
	{
		State.ItemIndices.Add(Array.Items[ItemIndex].BodyId, ItemIndex);
	}
}

void UBox3DIrisReplicationSubsystem::ApplyClientRoots()
{
	if (GetWorld() == nullptr || GetWorld()->GetNetMode() != NM_Client) return;
	bool bRetryCompletedAssetLoads = false;
	for (const TPair<FGuid, TSharedPtr<FStreamableHandle>>& Pending : PendingAssetLoads)
	{
		if (Pending.Value.IsValid() && Pending.Value->HasLoadCompleted())
		{
			bRetryCompletedAssetLoads = true;
			break;
		}
	}
	if (bRetryCompletedAssetLoads)
	{
		for (TPair<TWeakObjectPtr<UBox3DIrisReplicationRoot>, FClientRootState>& Pair : ClientRootStates)
			Pair.Value.ReceiveRevision = MAX_uint64;
	}
	TSet<FGuid> AllPresentKeys;
	for (int32 RootIndex = ClientRoots.Num() - 1; RootIndex >= 0; --RootIndex)
	{
		UBox3DIrisReplicationRoot* Root = ClientRoots[RootIndex].Get();
		if (!IsValid(Root))
		{
			ClientRootStates.Remove(ClientRoots[RootIndex]);
			ClientRoots.RemoveAtSwap(RootIndex, 1, EAllowShrinking::No);
			continue;
		}
		FClientRootState& RootState = ClientRootStates.FindOrAdd(Root);
		const bool bMetadataChanged = RootState.WorldEpoch != Root->GetWorldEpoch()
			|| RootState.OriginRevision != Root->GetOriginRevision();
		if (bMetadataChanged)
		{
			RootState.WorldEpoch = Root->GetWorldEpoch();
			RootState.NetworkOrigin = Root->GetNetworkOrigin();
			RootState.OriginRevision = Root->GetOriginRevision();
			WorldEpoch = RootState.WorldEpoch;
			NetworkOrigin = RootState.NetworkOrigin;
			OriginRevision = RootState.OriginRevision;
			ClockOffsetSeconds = 0.0;
			bHasClockOffset = false;
			for (TPair<FGuid, FClientVisualState>& Pair : ClientVisuals)
			{
				Pair.Value.History.Reset();
				Pair.Value.LastPlaybackSeconds = 0.0;
				if (ABox3DAsyncPresentationActor* Actor = Pair.Value.Actor.Get()) Actor->SetActorHiddenInGame(true);
			}
		}
		if (bMetadataChanged || RootState.ReceiveRevision != Root->GetReceiveRevision()
			|| !RootState.UnboundBodyKeys.IsEmpty())
		{
			TSet<FGuid> PresentKeys;
			TSet<FGuid> UnboundKeys;
			ReconcileRoot(Root, PresentKeys, UnboundKeys);
			RootState.BodyKeys = MoveTemp(PresentKeys);
			RootState.UnboundBodyKeys = MoveTemp(UnboundKeys);
			RootState.ReceiveRevision = Root->GetReceiveRevision();
		}
		AllPresentKeys.Append(RootState.BodyKeys);
	}
	for (auto It = ClientVisuals.CreateIterator(); It; ++It)
	{
		if (!AllPresentKeys.Contains(It.Key()))
		{
			if (ABox3DAsyncPresentationActor* Actor = It.Value().Actor.Get())
			{
				if (Actor->IsNetworkReplica())
				{
					SpawnedReplicaVisuals.Remove(Actor);
					Actor->Destroy();
				}
				else
				{
					Actor->SetActorHiddenInGame(true);
				}
			}
			PendingAssetLoads.Remove(It.Key());
			It.RemoveCurrent();
		}
	}
}

void UBox3DIrisReplicationSubsystem::ReconcileRoot(UBox3DIrisReplicationRoot* Root, TSet<FGuid>& PresentKeys,
	TSet<FGuid>& UnboundKeys)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Box3DIris_ReconcileRoot);
	const double Now = FPlatformTime::Seconds();
	for (const FBox3DIrisBodyItem& Item : Root->GetBodyArray().Items)
	{
		if (!Item.BodyKey.IsValid()) continue;
		PresentKeys.Add(Item.BodyKey);
		if (Item.OriginRevision != Root->GetOriginRevision()) continue;
		FClientVisualState& Visual = ClientVisuals.FindOrAdd(Item.BodyKey);
		if (Box3DIrisReplication::ShouldResetPresentation(Item.BodyId, Item.RelevanceRevision,
			Item.DiscontinuityRevision, Visual.BodyId, Visual.RelevanceRevision,
			Visual.DiscontinuityRevision, !Visual.History.IsEmpty()))
		{
			Visual.History.Reset();
			Visual.LastPlaybackSeconds = 0.0;
			Visual.RelevanceRevision = Item.RelevanceRevision;
			Visual.DiscontinuityRevision = Item.DiscontinuityRevision;
			Visual.BodyId = Item.BodyId;
		}
		const bool bHasNewSequence = Visual.History.IsEmpty()
			|| Visual.History.Last().OriginRevision != Root->GetOriginRevision()
			|| Box3DIrisReplication::IsCaptureSequenceNewer(Item.CaptureSequence, Visual.History.Last().Sequence);
		ABox3DAsyncPresentationActor* Actor = Visual.Actor.Get();
		if (!IsValid(Actor))
		{
			Actor = ResolveVisual(Item);
			if (Actor != nullptr)
			{
				Visual.Actor = Actor;
				Actor->SetActorHiddenInGame(true);
			}
		}
		if (Actor == nullptr) UnboundKeys.Add(Item.BodyKey);
		if (!bHasNewSequence) continue;
		const double ObservedOffset = Now - double(Item.CaptureSeconds);
		if (!bHasClockOffset)
		{
			ClockOffsetSeconds = ObservedOffset;
			bHasClockOffset = true;
		}
		else
		{
			ClockOffsetSeconds = FMath::Min(ClockOffsetSeconds + (ObservedOffset - ClockOffsetSeconds) * 0.02,
				ObservedOffset);
		}
		const double ObservedSpacing = Visual.History.IsEmpty() ? double(Item.ReplicationIntervalSeconds)
			: Now - Visual.History.Last().LocalReceiveSeconds;
		FReceivedPose& Pose = Visual.History.AddDefaulted_GetRef();
		Pose.LocalReceiveSeconds = Now;
		Pose.ServerCaptureSeconds = Item.CaptureSeconds;
		Pose.LocalPlaybackSeconds = double(Item.CaptureSeconds) + ClockOffsetSeconds;
		if (Visual.History.Num() > 1)
			Pose.LocalPlaybackSeconds = FMath::Max(Pose.LocalPlaybackSeconds,
				Visual.History[Visual.History.Num() - 2].LocalPlaybackSeconds);
		Pose.Position = Box3DIrisReplication::ToAbsoluteWorldPosition(FVector(Item.Position), NetworkOrigin);
		Pose.Rotation = FQuat(Item.Rotation.X, Item.Rotation.Y, Item.Rotation.Z, Item.Rotation.W).GetNormalized();
		Pose.Sequence = Item.CaptureSequence;
		Pose.OriginRevision = Root->GetOriginRevision();
		Pose.ReplicationIntervalSeconds = Item.ReplicationIntervalSeconds;
		Pose.bAwake = Item.bAwake;
		Visual.PlaybackDelaySeconds = FMath::Clamp(FMath::Max(0.1,
			FMath::Max(double(Item.ReplicationIntervalSeconds) * 1.5, ObservedSpacing * 2.0)), 0.1,
			MaxInterpolationDelaySeconds);
		++ClientTierReceiveCounts[Item.ReplicationIntervalSeconds <= 0.075f ? 0
			: (Item.ReplicationIntervalSeconds <= 0.15f ? 1 : 2)];
		++ClientUniqueReceivedSamples;
		if (Visual.History.Num() > MaxPoseHistory) Visual.History.RemoveAt(0, Visual.History.Num() - MaxPoseHistory, EAllowShrinking::No);
		++ClientReceivedSamples;
		if (bSmokeEnabled && Item.BodyId == SmokeWitnessNetworkBodyId)
			UE_LOG(LogTemp, Display, TEXT("BOX3D_NET_WITNESS_RECEIVED epoch=%s body=%llu seq=%u x=%.3f y=%.3f z=%.3f"),
				*Root->GetWorldEpoch().ToString(EGuidFormats::Digits), static_cast<unsigned long long>(Item.BodyId),
				Item.CaptureSequence, double(Item.Position.X), double(Item.Position.Y), double(Item.Position.Z));
		UE_LOG(LogTemp, VeryVerbose, TEXT("BOX3D_IRIS_RECEIVE body=%llu seq=%u relevance=%u"),
			static_cast<unsigned long long>(Item.BodyId), Item.CaptureSequence, Item.RelevanceRevision);
	}
}

ABox3DAsyncPresentationActor* UBox3DIrisReplicationSubsystem::ResolveVisual(const FBox3DIrisBodyItem& Item)
{
	if (const TWeakObjectPtr<ABox3DAsyncPresentationActor>* Existing = AuthoredActorsByKey.Find(Item.BodyKey))
	{
		return Existing->Get();
	}
	if (Item.bAuthoredVisual) return nullptr;
	if (FClientVisualState* Existing = ClientVisuals.Find(Item.BodyKey))
	{
		if (ABox3DAsyncPresentationActor* Actor = Existing->Actor.Get()) return Actor;
	}
	if (GetWorld() == nullptr) return nullptr;
	const FVector AbsolutePosition = Box3DIrisReplication::ToAbsoluteWorldPosition(FVector(Item.Position), NetworkOrigin);
	const FVector Position = Box3DIrisReplication::ToLocalWorldPosition(AbsolutePosition,
		FVector(GetWorld()->OriginLocation));
	const FQuat Rotation(Item.Rotation.X, Item.Rotation.Y, Item.Rotation.Z, Item.Rotation.W);
	return SpawnVisual(Item.BodyKey, Item.VisualAsset, FTransform(Rotation, Position, FVector(Item.VisualScale)));
}

ABox3DAsyncPresentationActor* UBox3DIrisReplicationSubsystem::SpawnVisual(FGuid BodyKey,
	const FSoftObjectPath& VisualAsset, const FTransform& Transform)
{
	UStaticMesh* Mesh = Cast<UStaticMesh>(VisualAsset.ResolveObject());
	if (Mesh == nullptr && VisualAsset.IsValid())
	{
		if (!PendingAssetLoads.Contains(BodyKey))
		{
			TSharedPtr<FStreamableHandle> Handle = UAssetManager::GetStreamableManager().RequestAsyncLoad(VisualAsset);
			if (Handle.IsValid()) PendingAssetLoads.Add(BodyKey, MoveTemp(Handle));
		}
		return nullptr;
	}
	PendingAssetLoads.Remove(BodyKey);
	if (Mesh == nullptr || GetWorld() == nullptr) return nullptr;
	ABox3DAsyncPresentationActor* Actor = GetWorld()->SpawnActorDeferred<ABox3DAsyncPresentationActor>(
		ABox3DAsyncPresentationActor::StaticClass(), Transform, nullptr, nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (Actor == nullptr) return nullptr;
	Actor->SetNetworkReplica(true);
	Actor->PresentationMesh->SetStaticMesh(Mesh);
	Actor->CollisionMode = EBox3DAsyncPresentationMode::AuthoredStaticCollision;
	Actor->FinishSpawning(Transform);
	SpawnedReplicaVisuals.Add(Actor);
	return Actor;
}

void UBox3DIrisReplicationSubsystem::ApplyVisualHistory(double NowSeconds)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Box3DIris_ApplyVisualHistory);
	for (TPair<FGuid, FClientVisualState>& Pair : ClientVisuals)
	{
		ABox3DAsyncPresentationActor* Actor = Pair.Value.Actor.Get();
		const TArray<FReceivedPose>& History = Pair.Value.History;
		if (!IsValid(Actor) || History.IsEmpty()) continue;
		FVector Position = History.Last().Position;
		FQuat Rotation = History.Last().Rotation;
		if (History.Num() >= 2)
		{
			const double PlaybackTime = FMath::Max(Pair.Value.LastPlaybackSeconds,
				NowSeconds - Pair.Value.PlaybackDelaySeconds);
			Pair.Value.LastPlaybackSeconds = PlaybackTime;
			ClientMaxDisplayAgeSeconds = FMath::Max(ClientMaxDisplayAgeSeconds,
				NowSeconds - History.Last().LocalReceiveSeconds);
			if (History.Last().bAwake && PlaybackTime > History.Last().LocalPlaybackSeconds + KINDA_SMALL_NUMBER)
				++ClientPlaybackUnderruns;
			for (int32 Index = 1; Index < History.Num(); ++Index)
			{
				const FReceivedPose& Before = History[Index - 1];
				const FReceivedPose& After = History[Index];
				if (PlaybackTime < Before.LocalPlaybackSeconds)
				{
					Position = Before.Position;
					Rotation = Before.Rotation;
					break;
				}
				if (PlaybackTime <= After.LocalPlaybackSeconds)
				{
					const double Duration = After.LocalPlaybackSeconds - Before.LocalPlaybackSeconds;
					const float Alpha = Duration > 0.0
						? Box3DIrisReplication::ComputePlaybackAlpha(PlaybackTime,
							Before.LocalPlaybackSeconds, After.LocalPlaybackSeconds) : 1.0f;
					Position = FMath::Lerp(Before.Position, After.Position, Alpha);
					Rotation = FQuat::Slerp(Before.Rotation, After.Rotation, Alpha).GetNormalized();
					break;
				}
			}
		}
		const UWorld* World = GetWorld();
		const FVector LocalPosition = World != nullptr
			? Box3DIrisReplication::ToLocalWorldPosition(Position, FVector(World->OriginLocation)) : Position;
		Actor->PresentationMesh->SetWorldLocationAndRotation(LocalPosition, Rotation, false, nullptr,
			ETeleportType::TeleportPhysics);
		Actor->SetActorHiddenInGame(false);
	}
}

void UBox3DIrisReplicationSubsystem::StartSmokeFixture()
{
	UWorld* World = GetWorld();
	if (World == nullptr || World->GetNetMode() == NM_Client)
	{
		bSmokeStarted = World != nullptr;
		return;
	}
	UBox3DAsyncSubsystem* Physics = World->GetSubsystem<UBox3DAsyncSubsystem>();
	if (Physics == nullptr || !Physics->TryRegisterCreationConsumer(SmokeCreationConsumer)) return;
	bSmokeStarted = true;
	const FSoftObjectPath SphereAsset(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	for (int32 Index = 0; Index < SmokeBodyTarget; ++Index)
	{
		FBox3DAsyncBodyDesc Desc;
		Desc.Motion = EBox3DAsyncBodyMotion::Dynamic;
		const double X = Index == 0 ? 0.0 : (Index == 1 ? -5000.0 : 2000.0 + (Index - 2) * 60.0);
		Desc.Transform = FTransform(FVector(X, 0.0, 45.0));
		Desc.InitialVelocity = Index < SmokeMovingBodies ? FVector(SmokeWitnessSpeed, 0.0, 0.0) : FVector::ZeroVector;
		Desc.GravityScale = 0.0f;
		FBox3DAsyncBodyShapeDesc Shape;
		Shape.Kind = EBox3DAsyncBodyShapeKind::Sphere;
		Shape.Radius = 20.0f;
		Shape.Options.CategoryBits = SharedCollisionBit;
		Shape.Options.MaskBits = 0;
		Shape.Options.Density = 1.0f;
		Desc.Shapes.Add(MoveTemp(Shape));
		FSmokeQueuedBody& Queued = SmokeQueuedBodies.AddDefaulted_GetRef();
		Queued.Description = MoveTemp(Desc);
		Queued.BodyKey = FGuid::NewGuid();
		Queued.VisualAsset = SphereAsset;
		Queued.Scale = FVector::OneVector;
	}
	UE_LOG(LogTemp, Display, TEXT("BOX3D_NET_SMOKE_START role=%s bodies=%d duration=%.1f"),
		*SmokeRole, SmokeBodyTarget, SmokeEndSeconds - SmokeStartSeconds);
}

void UBox3DIrisReplicationSubsystem::PumpSmokeCreationResults()
{
	if (SmokeCreationConsumer.Id == 0) return;
	UBox3DAsyncSubsystem* Physics = GetWorld() ? GetWorld()->GetSubsystem<UBox3DAsyncSubsystem>() : nullptr;
	if (Physics == nullptr) return;
	for (auto It = SmokePendingBodies.CreateIterator(); It; ++It)
	{
		FBox3DAsyncCreationResult Result;
		const FBox3DAsyncBodyHandle Handle = It.Value().Handle;
		if (!Physics->TryTakeCreationResult(SmokeCreationConsumer, Handle, Result)) continue;
		if (Result.bSucceeded)
		{
			AppendSmokeBody(Result.BodyHandle, It.Value().BodyKey, It.Value().VisualAsset,
				It.Value().Scale);
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("BOX3D_NET_SMOKE_CREATE_FAILED body=%llu"),
				static_cast<unsigned long long>(Handle.Id));
		}
		It.RemoveCurrent();
	}
	while (!SmokeQueuedBodies.IsEmpty())
	{
		FSmokeQueuedBody& Queued = SmokeQueuedBodies[0];
		FBox3DAsyncBodyHandle Handle;
		if (!Physics->TryCreateBody(SmokeCreationConsumer, Queued.Description, Handle)) break;
		FSmokePendingBody& Pending = SmokePendingBodies.Add(Handle.Id);
		Pending.Handle = Handle;
		Pending.BodyKey = Queued.BodyKey;
		Pending.VisualAsset = Queued.VisualAsset;
		Pending.Scale = Queued.Scale;
		SmokeQueuedBodies.RemoveAt(0, 1, EAllowShrinking::No);
	}
}

void UBox3DIrisReplicationSubsystem::AppendSmokeBody(FBox3DAsyncBodyHandle Body, FGuid BodyKey,
	const FSoftObjectPath& VisualAsset, const FVector& Scale)
{
	RegisterActorlessSharedBody(Body, BodyKey, VisualAsset, Scale, true);
	if (const uint64* NetworkId = NetworkBodyIdsByPhysicsId.Find(Body.Id); NetworkId != nullptr
		&& SmokeWitnessNetworkBodyId == 0) SmokeWitnessNetworkBodyId = *NetworkId;
	if (bSmokeLifecycle && SmokeLifecycleBodyId == 0) SmokeLifecycleBodyId = Body.Id;
}

void UBox3DIrisReplicationSubsystem::FinishSmokeRun()
{
	if (bSmokeFinished) return;
	bSmokeFinished = true;
	if (GetWorld() != nullptr && GetWorld()->GetNetMode() != NM_Client)
	{
		uint64 Staged = DisconnectedStagedSamples, Near = DisconnectedNearSamples;
		uint64 Mid = DisconnectedMidSamples, Far = DisconnectedFarSamples;
		uint64 OutBytes = DisconnectedSteadyOutBytes, BaselineBytes = DisconnectedBaselineOutBytes;
		uint64 InitialItems = DisconnectedInitialItems, Culls = DisconnectedCullRemovals;
		uint64 Reentries = DisconnectedReentries, Despawns = DisconnectedDespawnRemovals;
		uint64 Due = DisconnectedDueSamples, Unchanged = DisconnectedUnchangedSamples;
		uint64 Deferred = DisconnectedDeferredSamples, Capacity = DisconnectedCapacityAdmissions;
		for (const FConnectionState& State : Connections)
		{
			Staged += State.StagedSamples; Near += State.NearSamples; Mid += State.MidSamples; Far += State.FarSamples;
			InitialItems += State.InitialItems; Culls += State.CullRemovals;
			Reentries += State.Reentries; Despawns += State.DespawnRemovals;
			Due += State.DueSamples; Unchanged += State.UnchangedSamples;
			Deferred += State.DeferredSamples; Capacity += State.CapacityAdmissions;
			if (const UNetConnection* Connection = State.Connection.Get())
			{
				const uint64 CurrentBytes = uint64(FMath::Max(0, Connection->OutTotalBytes));
				const uint64 SteadyMark = State.SteadyOutBytes != 0 ? State.SteadyOutBytes : State.InitialOutBytes;
				BaselineBytes += SteadyMark >= State.InitialOutBytes ? SteadyMark - State.InitialOutBytes : 0;
				OutBytes += CurrentBytes >= SteadyMark ? CurrentBytes - SteadyMark : 0;
			}
			const double MeasurementStart = FMath::Max(State.ConnectionStartSeconds + 0.5, SmokeStartSeconds + 2.0);
			const double MeasurementSeconds = FMath::Max(0.1, SmokeEndSeconds - MeasurementStart);
			const double WitnessHz = double(State.WitnessSamples > 0 ? State.WitnessSamples - 1 : 0) / MeasurementSeconds;
			const uint64 CurrentOutBytes = State.Connection.IsValid()
				? uint64(FMath::Max<int64>(0, State.Connection->OutTotalBytes)) : State.LastOutBytes;
			const uint64 SteadyMark = State.SteadyOutBytes != 0 ? State.SteadyOutBytes : State.InitialOutBytes;
			const uint64 RootBaselineBytes = SteadyMark >= State.InitialOutBytes ? SteadyMark - State.InitialOutBytes : 0;
			const uint64 RootSteadyBytes = CurrentOutBytes >= SteadyMark ? CurrentOutBytes - SteadyMark : 0;
			if (State.SmokeRole.Contains(TEXT("Near"), ESearchCase::IgnoreCase) && WitnessHz < 10.0)
				UE_LOG(LogTemp, Warning, TEXT("BOX3D_NET_SMOKE_WARNING near_witness_hz=%.2f expected>=10"), WitnessHz);
			if (State.SmokeRole.Contains(TEXT("Far"), ESearchCase::IgnoreCase) && WitnessHz < 3.0)
				UE_LOG(LogTemp, Warning, TEXT("BOX3D_NET_SMOKE_WARNING far_witness_hz=%.2f expected>=3"), WitnessHz);
			UE_LOG(LogTemp, Display,
			TEXT("BOX3D_NET_SMOKE_ROOT role=%s initial=%llu samples=%llu due=%llu unchanged=%llu deferred=%llu capacity=%llu witness_samples=%llu witness_hz=%.2f near=%llu mid=%llu far=%llu culls=%llu reentries=%llu despawns=%llu baseline_connection_bytes=%llu steady_connection_bytes=%llu"),
				*State.SmokeRole, static_cast<unsigned long long>(State.InitialItems),
			static_cast<unsigned long long>(State.StagedSamples), static_cast<unsigned long long>(State.DueSamples),
			static_cast<unsigned long long>(State.UnchangedSamples), static_cast<unsigned long long>(State.DeferredSamples),
			static_cast<unsigned long long>(State.CapacityAdmissions),
				static_cast<unsigned long long>(State.WitnessSamples), WitnessHz,
				static_cast<unsigned long long>(State.NearSamples), static_cast<unsigned long long>(State.MidSamples),
				static_cast<unsigned long long>(State.FarSamples), static_cast<unsigned long long>(State.CullRemovals),
				static_cast<unsigned long long>(State.Reentries), static_cast<unsigned long long>(State.DespawnRemovals),
				static_cast<unsigned long long>(RootBaselineBytes), static_cast<unsigned long long>(RootSteadyBytes));
		}
		const bool bSmokePassed = SmokeRootsObserved > 0 && InitialItems > 0 && Staged > 0
			&& (!bSmokeLifecycle || (Culls > 0 && Reentries > 0 && Despawns > 0));
		if (!bSmokePassed) UE_LOG(LogTemp, Warning, TEXT("BOX3D_NET_SMOKE_WARNING insufficient_samples roots=%llu staged=%llu lifecycle=%d culls=%llu reentries=%llu despawns=%llu"),
			static_cast<unsigned long long>(SmokeRootsObserved), static_cast<unsigned long long>(Staged), bSmokeLifecycle ? 1 : 0,
			static_cast<unsigned long long>(Culls), static_cast<unsigned long long>(Reentries),
			static_cast<unsigned long long>(Despawns));
		UE_LOG(LogTemp, Display,
			TEXT("BOX3D_NET_SMOKE_RESULT role=Server passed=%d bodies=%d roots=%d initial=%llu staged=%llu due=%llu unchanged=%llu deferred=%llu capacity=%llu near=%llu mid=%llu far=%llu culls=%llu reentries=%llu despawns=%llu baseline_connection_bytes=%llu steady_connection_bytes=%llu total_connection_bytes=%llu"),
			bSmokePassed ? 1 : 0, AuthorityBodies.Num(), int32(SmokeRootsObserved), static_cast<unsigned long long>(InitialItems),
			static_cast<unsigned long long>(Staged),
			static_cast<unsigned long long>(Due), static_cast<unsigned long long>(Unchanged),
			static_cast<unsigned long long>(Deferred), static_cast<unsigned long long>(Capacity),
			static_cast<unsigned long long>(Near), static_cast<unsigned long long>(Mid),
			static_cast<unsigned long long>(Far), static_cast<unsigned long long>(Culls),
			static_cast<unsigned long long>(Reentries), static_cast<unsigned long long>(Despawns),
			static_cast<unsigned long long>(BaselineBytes), static_cast<unsigned long long>(OutBytes),
			static_cast<unsigned long long>(DisconnectedOutBytes));
	}
	else
	{
		UNetDriver* Driver = GetWorld() ? GetWorld()->GetNetDriver() : nullptr;
		const UNetConnection* Connection = Driver ? Driver->ServerConnection : nullptr;
		const uint64 InBytes = Connection ? uint64(FMath::Max<int64>(0, Connection->InTotalBytes)) : 0;
		const uint64 OutBytes = Connection ? uint64(FMath::Max<int64>(0, Connection->OutTotalBytes)) : 0;
		if (SmokeRole.IsEmpty() || ClientReceivedSamples == 0)
			UE_LOG(LogTemp, Warning, TEXT("BOX3D_NET_SMOKE_WARNING role=%s received=%llu visuals=%d"),
				*SmokeRole, static_cast<unsigned long long>(ClientReceivedSamples), ClientVisuals.Num());
		UE_LOG(LogTemp, Display,
			TEXT("BOX3D_NET_SMOKE_RESULT role=%s passed=%d received=%llu unique=%llu tier20=%llu tier10=%llu tier5=%llu display_age_max=%.3f underruns=%llu visuals=%d baseline_in=%llu steady_in=%llu baseline_out=%llu steady_out=%llu"),
			*SmokeRole, !SmokeRole.IsEmpty() && ClientReceivedSamples > 0 ? 1 : 0,
			static_cast<unsigned long long>(ClientReceivedSamples),
			static_cast<unsigned long long>(ClientUniqueReceivedSamples),
			static_cast<unsigned long long>(ClientTierReceiveCounts[0]),
			static_cast<unsigned long long>(ClientTierReceiveCounts[1]),
			static_cast<unsigned long long>(ClientTierReceiveCounts[2]), ClientMaxDisplayAgeSeconds,
			static_cast<unsigned long long>(ClientPlaybackUnderruns), ClientVisuals.Num(),
			static_cast<unsigned long long>(ClientInBytesAtStart),
			static_cast<unsigned long long>(InBytes >= ClientInBytesAtStart ? InBytes - ClientInBytesAtStart : 0),
			static_cast<unsigned long long>(ClientOutBytesAtStart),
			static_cast<unsigned long long>(OutBytes >= ClientOutBytesAtStart ? OutBytes - ClientOutBytesAtStart : 0));
	}
	FPlatformMisc::RequestExit(false);
}
