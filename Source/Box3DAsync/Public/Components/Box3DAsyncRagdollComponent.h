#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Data/Enums/Box3DAsyncRagdollStatus.h"
#include "Data/Structs/Box3DAsyncRagdollHandle.h"
#include "Data/Structs/Box3DBPBodyState.h"
#include "Box3DAsyncRagdollComponent.generated.h"

class USkeletalMeshComponent;
class USkeletalMesh;
class UBox3DAsyncRagdollSubsystem;

/** Optional skeletal presentation adapter for an async ragdoll group. */
UCLASS(ClassGroup=(Physics), meta=(BlueprintSpawnableComponent))
class BOX3DASYNC_API UBox3DAsyncRagdollComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UBox3DAsyncRagdollComponent();
	/** Admission only. Poll status; creation can fail asynchronously. Uses the mesh Physics Asset. */
	UFUNCTION(BlueprintCallable, Category="Box3D|Async|Ragdoll")
	bool StartRagdoll(USkeletalMeshComponent* Mesh, FVector InitialVelocity);
	UFUNCTION(BlueprintCallable, Category="Box3D|Async|Ragdoll")
	void StopRagdoll();
	UFUNCTION(BlueprintPure, Category="Box3D|Async|Ragdoll")
	EBox3DAsyncRagdollStatus GetRagdollStatus() const;
	UFUNCTION(BlueprintCallable, Category="Box3D|Async|Ragdoll")
	bool GetBoneState(FName Bone, FBox3DBPBodyState& OutState) const;
	UFUNCTION(BlueprintCallable, Category="Box3D|Async|Ragdoll")
	bool AddImpulse(FVector Impulse);
	UFUNCTION(BlueprintCallable, Category="Box3D|Async|Ragdoll")
	bool AddImpulseAtBone(FName Bone, FVector Impulse, FVector WorldPoint);
	void BuildLocalPose(TArray<FTransform>& OutLocalPose);
	USkeletalMeshComponent* GetTargetMesh() const { return TargetMesh.Get(); }
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
	void RestorePresentation();
	TWeakObjectPtr<USkeletalMeshComponent> TargetMesh;
	TWeakObjectPtr<USkeletalMesh> TargetAsset;
	TWeakObjectPtr<UBox3DAsyncRagdollSubsystem> Ragdolls;
	UPROPERTY(Transient)
	TObjectPtr<UClass> SavedAnimClass = nullptr;
	FBox3DAsyncRagdollHandle Group;
	TArray<FTransform> BaseLocalPose;
	TArray<FTransform> BaseComponentPose;
	uint8 SavedAnimationMode = 0;
	uint8 SavedVisibilityTick = 0;
	bool bPresentationInstalled = false;
	bool bStopRequested = false;
	EBox3DAsyncRagdollStatus LastStatus = EBox3DAsyncRagdollStatus::Unavailable;
};
