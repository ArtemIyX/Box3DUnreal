#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/Box3DIrisBodyArray.h"
#include "Data/Enums/Box3DReplicationPolicy.h"
#include "Iris/ReplicationSystem/NetObjectFactory.h"
#include "UObject/SoftObjectPath.h"
#include "Box3DIrisReplication.generated.h"

UCLASS(Transient)
class BOX3DREPLICATION_API UBox3DIrisReplicationRoot final : public UObject
{
	GENERATED_BODY()

public:
	UBox3DIrisReplicationRoot();
	virtual bool IsSupportedForNetworking() const override { return true; }
	virtual UWorld* GetWorld() const override;
	virtual void RegisterReplicationFragments(UE::Net::FFragmentRegistrationContext& Context,
		UE::Net::EFragmentRegistrationFlags RegistrationFlags) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	void SetReplicationWorld(UWorld* InWorld) { ReplicationWorld = InWorld; }
	void SetNetworkMetadata(const FGuid& InWorldEpoch, const FVector& InNetworkOrigin, uint32 InOriginRevision);
	const FGuid& GetWorldEpoch() const { return WorldEpoch; }
	const FVector& GetNetworkOrigin() const { return NetworkOrigin; }
	uint32 GetOriginRevision() const { return OriginRevision; }
	uint64 GetReceiveRevision() const { return ReceiveRevision; }
	void ActivateInitialState();
	void DetachAndClear();
	const FBox3DIrisBodyArray& GetBodyArray() const { return BodyArray; }
	FBox3DIrisBodyArray& GetBodyArray() { return BodyArray; }

private:
	friend struct FBox3DIrisBodyArray;
	void OnReceivedItems();

	UPROPERTY(Replicated)
	FBox3DIrisBodyArray BodyArray;
	UPROPERTY(Replicated)
	FGuid WorldEpoch;
	UPROPERTY(Replicated)
	FVector NetworkOrigin = FVector::ZeroVector;
	UPROPERTY(Replicated)
	uint32 OriginRevision = 0;
	TWeakObjectPtr<UWorld> ReplicationWorld;
	uint64 ReceiveRevision = 0;
	bool bInitialStateReady = false;
};

UCLASS(Transient)
class BOX3DREPLICATION_API UBox3DIrisNetObjectFactory final : public UNetObjectFactory
{
	GENERATED_BODY()

public:
	virtual FInstantiateResult InstantiateReplicatedObjectFromHeader(const FInstantiateContext& Context,
		const UE::Net::FNetObjectCreationHeader* Header) override;
	virtual void PostInit(const FPostInitContext& Context) override;
	virtual void DetachedFromReplication(const FDestroyedContext& Context) override;
	virtual TOptional<FWorldInfoData> GetWorldInfo(const FWorldInfoContext& Context) const override;
	virtual float GetPollFrequency(UE::Net::FNetRefHandle RootObjectHandle, UObject* RootObjectInstance) override;
	virtual TUniquePtr<UE::Net::FNetObjectCreationHeader> CreateAndFillHeader(UE::Net::FNetRefHandle Handle) override;
	virtual bool SerializeHeader(const UE::Net::FCreationHeaderContext& Context,
		const UE::Net::FNetObjectCreationHeader* Header) override;
	virtual TUniquePtr<UE::Net::FNetObjectCreationHeader> CreateAndDeserializeHeader(
		const UE::Net::FCreationHeaderContext& Context) override;
};

namespace Box3DIrisReplication
{
	struct FStageBudget
	{
		int32 Lifecycle = 0;
		int32 Routine = 0;
	};
	BOX3DREPLICATION_API extern const FName FactoryName;
	BOX3DREPLICATION_API bool TryQuantizePositionToCentimeter(const FVector& Value, FVector& OutQuantized);
	BOX3DREPLICATION_API bool ShouldSuspendForAuthorityOriginShift(const FVector& FrozenOrigin,
		const FVector& CurrentOrigin, bool bAlreadySuspended);
	BOX3DREPLICATION_API FVector ToAbsoluteWorldPosition(const FVector& LocalPosition, const FVector& WorldOrigin);
	BOX3DREPLICATION_API FVector ToLocalWorldPosition(const FVector& AbsolutePosition, const FVector& WorldOrigin);
	BOX3DREPLICATION_API int32 ComputeNextCandidateCursor(int32 StartIndex, int32 CandidateCount,
		int32 TotalCount, int32 FirstDeferredCandidate);
	BOX3DREPLICATION_API bool IsAdmissionPreferred(double CandidateDistance, uint64 CandidateId,
		double ResidentDistance, uint64 ResidentId);
	BOX3DREPLICATION_API FStageBudget ComputeStageBudget(int32 LifecycleCount, int32 RoutineCount,
		int32 TotalBudget, int32 MaxLifecycleBudget);
	BOX3DREPLICATION_API double ComputeTierInterval(double Distance, double PreviousInterval = 0.0);
	BOX3DREPLICATION_API bool IsDeadlineDue(double Now, double Deadline);
	BOX3DREPLICATION_API double AdvanceDeadline(double Now, double Interval, double Deadline);
	BOX3DREPLICATION_API bool IsCaptureSequenceNewer(uint32 NewSequence, uint32 PreviousSequence);
	BOX3DREPLICATION_API bool ShouldResetPresentation(uint64 BodyId, uint32 RelevanceRevision,
		uint32 DiscontinuityRevision, uint64 PreviousBodyId, uint32 PreviousRelevanceRevision,
		uint32 PreviousDiscontinuityRevision, bool bHasPrevious);
	BOX3DREPLICATION_API float ComputePlaybackAlpha(double PlaybackTime, double BeforeTime, double AfterTime);
	BOX3DREPLICATION_API double PreserveMonotonicPlaybackTime(double CandidateTime, double PreviousTime);
}
