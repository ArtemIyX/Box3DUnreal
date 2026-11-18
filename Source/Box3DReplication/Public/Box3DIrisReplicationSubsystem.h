#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "World/Box3DAsyncSubsystem.h"
#include "Box3DIrisReplication.h"
#include "Box3DIrisReplicationSubsystem.generated.h"

class ABox3DAsyncPresentationActor;
class UNetConnection;
class UNetDriver;
struct FStreamableHandle;

UCLASS()
class BOX3DREPLICATION_API UBox3DIrisReplicationSubsystem final : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual TStatId GetStatId() const override;

	bool RegisterAuthoredPresentationActor(ABox3DAsyncPresentationActor* Actor);
	void UnregisterAuthoredPresentationActor(ABox3DAsyncPresentationActor* Actor);
	void RegisterSharedBody(FBox3DAsyncBodyHandle Body, ABox3DAsyncPresentationActor* Actor);
	void RegisterActorlessSharedBody(FBox3DAsyncBodyHandle Body, FGuid BodyKey,
		const FSoftObjectPath& VisualAsset, const FVector& Scale, bool bDynamic);
	void UnregisterSharedBody(FGuid BodyKey, FBox3DAsyncBodyId PhysicsBodyId);
	void NotifyBodyDiscontinuity(FGuid BodyKey, FBox3DAsyncBodyId PhysicsBodyId);
	void AddClientRoot(UBox3DIrisReplicationRoot* Root);
	void RemoveClientRoot(UBox3DIrisReplicationRoot* Root);

private:
	struct FAuthorityBody
	{
		uint64 NetworkBodyId = 0;
		FBox3DAsyncBodyHandle PhysicsHandle;
		FGuid BodyKey;
		TWeakObjectPtr<ABox3DAsyncPresentationActor> Actor;
		TWeakObjectPtr<ABox3DAsyncPresentationActor> HostVisual;
		FSoftObjectPath VisualAsset;
		FVector3f Scale = FVector3f::OneVector;
		FBox3DAsyncPose LatestPose;
		uint64 CaptureSerial = 0;
		double CaptureMonotonicSeconds = 0.0;
		double ReplicationIntervalSeconds = 0.0;
		bool bAuthoredVisual = false;
		bool bAwake = true;
		bool bHasPose = false;
		uint32 DiscontinuityRevision = 0;
	};

	struct FConnectionState
	{
		TWeakObjectPtr<UNetConnection> Connection;
		TWeakObjectPtr<UNetDriver> Driver;
		TObjectPtr<UBox3DIrisReplicationRoot> Root = nullptr;
		UE::Net::FNetRefHandle RootHandle;
		TMap<uint64, int32> ItemIndices;
		TMap<uint64, double> NextDeadlines;
		TMap<uint64, double> Intervals;
		TMap<uint64, double> Distances;
		TSet<uint64> EverRelevantBodyIds;
		uint64 InitialOutBytes = 0;
		uint64 SteadyOutBytes = 0;
		uint64 StagedSamples = 0;
		uint64 CompletedOutBytes = 0;
		uint64 InitialItems = 0;
		uint64 WitnessSamples = 0;
		uint64 LastOutBytes = 0;
		double ConnectionStartSeconds = 0.0;
		uint64 NearSamples = 0;
		uint64 MidSamples = 0;
		uint64 FarSamples = 0;
		uint64 CullRemovals = 0;
		uint64 Reentries = 0;
		uint64 DespawnRemovals = 0;
		uint64 CapacityRemovals = 0;
		uint64 DueSamples = 0;
		uint64 UnchangedSamples = 0;
		uint64 DeferredSamples = 0;
		uint64 CapacityAdmissions = 0;
		FString SmokeRole;
		int32 DiscoveryCursor = 0;
		uint64 LifecycleCursorBodyId = 0;
		uint32 NextRelevanceRevision = 0;
	};

	struct FReceivedPose
	{
		double LocalReceiveSeconds = 0.0;
		double ServerCaptureSeconds = 0.0;
		double LocalPlaybackSeconds = 0.0;
		FVector Position = FVector::ZeroVector;
		FQuat Rotation = FQuat::Identity;
		uint32 Sequence = 0;
		uint32 OriginRevision = 0;
		float ReplicationIntervalSeconds = 0.2f;
		bool bAwake = true;
	};

	struct FClientVisualState
	{
		TWeakObjectPtr<ABox3DAsyncPresentationActor> Actor;
		TArray<FReceivedPose> History;
		uint32 RelevanceRevision = 0;
		uint32 DiscontinuityRevision = 0;
		uint64 BodyId = 0;
		double PlaybackDelaySeconds = 0.1;
		double LastPlaybackSeconds = 0.0;
	};

	struct FClientRootState
	{
		uint64 ReceiveRevision = MAX_uint64;
		FGuid WorldEpoch;
		FVector NetworkOrigin = FVector::ZeroVector;
		uint32 OriginRevision = MAX_uint32;
		TSet<FGuid> BodyKeys;
		TSet<FGuid> UnboundBodyKeys;
	};

	void StageAuthorityForConnections();
	void UpdateAuthorityFrames();
	void UpdateHostVisuals();
	void RetireHostVisual(FAuthorityBody& Body);
	bool CheckAuthorityWorldOrigin();
	void SuspendAuthorityReplicationForOriginShift();
	void RemoveDisconnectedConnections();
	void ApplyClientRoots();
	void ApplyVisualHistory(double NowSeconds);
	void ReconcileRoot(UBox3DIrisReplicationRoot* Root, TSet<FGuid>& PresentKeys, TSet<FGuid>& UnboundKeys);
	ABox3DAsyncPresentationActor* ResolveVisual(const FBox3DIrisBodyItem& Item);
	ABox3DAsyncPresentationActor* SpawnVisual(FGuid BodyKey, const FSoftObjectPath& VisualAsset,
		const FTransform& Transform);
	void RemoveRoot(UBox3DIrisReplicationRoot* Root);
	bool TryStartRoot(UNetConnection* Connection, FConnectionState& State);
	void RemoveBodyFromConnection(FAuthorityBody& Body, FConnectionState& State, bool bDespawn = false);
	void RemoveBodiesFromConnection(const TArray<uint64>& BodyIds, FConnectionState& State,
		bool bDespawn, bool bCapacity = false);
	void OnWorldPostActorTick(UWorld* World, ELevelTick TickType, float DeltaSeconds);
	void StartSmokeFixture();
	void PumpSmokeCreationResults();
	void FinishSmokeRun();
	void AppendSmokeBody(FBox3DAsyncBodyHandle Body, FGuid BodyKey, const FSoftObjectPath& VisualAsset,
		const FVector& Scale);
	bool GetConnectionViewLocation(const UNetConnection* Connection, FVector& OutLocation) const;

	UPROPERTY()
	TArray<TObjectPtr<UBox3DIrisReplicationRoot>> StrongRoots;
	UPROPERTY()
	TArray<TObjectPtr<ABox3DAsyncPresentationActor>> SpawnedReplicaVisuals;
	TMap<FGuid, TWeakObjectPtr<ABox3DAsyncPresentationActor>> AuthoredActorsByKey;
	TMap<uint64, FAuthorityBody> AuthorityBodies;
	TArray<uint64> OrderedAuthorityBodyIds;
	TMap<uint64, uint64> NetworkBodyIdsByPhysicsId;
	TArray<FConnectionState> Connections;
	TArray<TWeakObjectPtr<UBox3DIrisReplicationRoot>> ClientRoots;
	TMap<TWeakObjectPtr<UBox3DIrisReplicationRoot>, FClientRootState> ClientRootStates;
	TMap<FGuid, FClientVisualState> ClientVisuals;
	TMap<FGuid, uint64> PhysicsBodyIdsByKey;
	struct FSmokePendingBody
	{
		FBox3DAsyncBodyHandle Handle;
		FGuid BodyKey;
		FSoftObjectPath VisualAsset;
		FVector Scale = FVector::OneVector;
	};
	struct FSmokeQueuedBody
	{
		FBox3DAsyncBodyDesc Description;
		FGuid BodyKey;
		FSoftObjectPath VisualAsset;
		FVector Scale = FVector::OneVector;
	};
	TMap<uint64, FSmokePendingBody> SmokePendingBodies;
	TArray<FSmokeQueuedBody> SmokeQueuedBodies;
	TMap<FGuid, TSharedPtr<FStreamableHandle>> PendingAssetLoads;
	FBox3DAsyncCreationConsumer SmokeCreationConsumer;
	FDelegateHandle PostActorTickHandle;
	FGuid WorldEpoch;
	// Captured once at Initialize. The worker world remains in this coordinate frame for the session.
	FVector ServerWorldOrigin = FVector::ZeroVector;
	FVector NetworkOrigin = FVector::ZeroVector;
	bool bAuthorityOriginMismatch = false;
	bool bOriginMismatchLogged = false;
	uint32 OriginRevision = 0;
	uint64 RootChangeSerial = 0;
	uint64 NextNetworkBodyId = 1;
	double ClockOffsetSeconds = 0.0;
	double SessionStartSeconds = 0.0;
	double SmokeEndSeconds = 0.0;
	double SmokeStartSeconds = 0.0;
	uint64 ClientReceivedSamples = 0;
	uint64 ClientUniqueReceivedSamples = 0;
	uint64 ClientTierReceiveCounts[3] = {};
	uint64 ClientPlaybackUnderruns = 0;
	double ClientMaxDisplayAgeSeconds = 0.0;
	uint64 DisconnectedStagedSamples = 0;
	uint64 DisconnectedInitialItems = 0;
	uint64 DisconnectedNearSamples = 0;
	uint64 DisconnectedMidSamples = 0;
	uint64 DisconnectedFarSamples = 0;
	uint64 DisconnectedCullRemovals = 0;
	uint64 DisconnectedReentries = 0;
	uint64 DisconnectedDespawnRemovals = 0;
	uint64 DisconnectedDueSamples = 0;
	uint64 DisconnectedUnchangedSamples = 0;
	uint64 DisconnectedDeferredSamples = 0;
	uint64 DisconnectedCapacityAdmissions = 0;
	uint64 SmokeRootsObserved = 0;
	uint64 SmokeWitnessNetworkBodyId = 0;
	uint64 DisconnectedOutBytes = 0;
	uint64 DisconnectedBaselineOutBytes = 0;
	uint64 DisconnectedSteadyOutBytes = 0;
	uint64 SmokeLifecycleBodyId = 0;
	uint64 ClientOutBytesAtStart = 0;
	uint64 ClientInBytesAtStart = 0;
	bool bClientByteBaselineSet = false;
	int32 SmokeBodyTarget = 0;
	int32 SmokeResidentCap = 512;
	float SmokeWitnessSpeed = 30.0f;
	int32 SmokeMovingBodies = 1;
	int32 ResidentRowCap = 512;
	int32 SmokeDeltaCompression = 0;
	FString SmokeRole;
	bool bHasClockOffset = false;
	bool bSmokeEnabled = false;
	bool bSmokeStarted = false;
	bool bSmokeFinished = false;
	bool bSmokeLifecycle = false;
	bool bSmokeSweepView = false;
	bool bSmokeLifecycleDespawned = false;
};
