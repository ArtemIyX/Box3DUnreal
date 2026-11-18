#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Data/Enums/Box3DAsyncRagdollStatus.h"
#include "Data/Structs/Box3DAsyncRagdollHandle.h"
#include "Data/Structs/Box3DAsyncRagdollDesc.h"
#include "Data/Structs/Box3DAsyncRagdollPose.h"
#include "Box3DAsyncRagdollSubsystem.generated.h"

/** Game-thread orchestration of worker-owned ragdoll groups. No native ID leaves the async worker. */
UCLASS()
class BOX3DASYNC_API UBox3DAsyncRagdollSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()
public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	bool TryCreateRagdoll(const FBox3DAsyncRagdollDesc& Desc, FBox3DAsyncRagdollHandle& OutRagdoll);
	EBox3DAsyncRagdollStatus GetRagdollStatus(FBox3DAsyncRagdollHandle Ragdoll) const;
	bool TryRemoveRagdoll(FBox3DAsyncRagdollHandle Ragdoll, bool bForgetWhenRemoved = false);
	/** Releases an already removed or failed group identity. */
	bool ForgetRagdoll(FBox3DAsyncRagdollHandle Ragdoll);
	/** All bone transforms come from one completed pose frame. */
	bool TryGetLatestRagdollPose(FBox3DAsyncRagdollHandle Ragdoll, FBox3DAsyncRagdollPose& OutPose) const;
	bool TryGetBoneState(FBox3DAsyncRagdollHandle Ragdoll, FName Bone, FBox3DAsyncBodyState& OutState) const;
	bool TryApplyRagdollImpulse(FBox3DAsyncRagdollHandle Ragdoll, FVector Impulse);
	bool TrySubmitBoneCommand(FBox3DAsyncRagdollHandle Ragdoll, FName Bone, const FBox3DAsyncBodyCommand& Command);
private:
	struct FGroup
	{
		FBox3DAsyncRagdollDesc Desc;
		FBox3DAsyncCreationConsumer Consumer;
		TArray<FBox3DAsyncBodyHandle> Bodies;
		TArray<uint8> BodyResults;
		TArray<uint8> BodyRemovals;
		TArray<FBox3DAsyncJointHandle> Joints;
		int32 NextBody = 0;
		int32 NextJoint = 0;
		double StartedSeconds = 0.0;
		bool bFailure = false;
		bool bForgetWhenRemoved = false;
		EBox3DAsyncRagdollStatus Status = EBox3DAsyncRagdollStatus::CreatingBodies;
	};
	TMap<uint64, FGroup> Groups;
	FGuid WorldId;
	uint64 NextId = 1;
	int32 NextCollisionGroup = -1;
	int32 MaxGroups = 64;
	int32 MaxBones = 64;
	int32 MaxJoints = 128;
	int32 MaxShapes = 512;
	int32 MaxConvexPoints = 32768;
	double CreationTimeout = 10.0;
	TWeakObjectPtr<UBox3DAsyncSubsystem> Physics;
};
