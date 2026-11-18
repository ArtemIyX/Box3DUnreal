#pragma once

#include "CoreMinimal.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Actor.h"
#include "Data/Enums/Box3DReplicationPolicy.h"
#include "Data/Enums/Box3DAsyncPresentationMode.h"
#include "Box3DAsyncPresentationActor.generated.h"

class ABox3DAsyncDirector;

namespace Box3DAsyncPresentationCollision
{
	struct FFilter
	{
		uint64 CategoryBits = 0;
		uint64 MaskBits = 0;
	};

	inline bool IsSharedDynamic(EBox3DReplicationPolicy Policy, EBox3DAsyncPresentationMode Mode)
	{
		return Policy == EBox3DReplicationPolicy::Shared
			&& Mode == EBox3DAsyncPresentationMode::AuthoredDynamicCollision;
	}

	inline FFilter GetFilter(EBox3DReplicationPolicy Policy, EBox3DAsyncPresentationMode Mode)
	{
		constexpr uint64 SharedDynamic = 1ull;
		constexpr uint64 LocalDynamic = 2ull;
		constexpr uint64 StaticWorld = 4ull;
		if (Mode == EBox3DAsyncPresentationMode::AuthoredStaticCollision)
			return {StaticWorld, SharedDynamic | LocalDynamic};
		if (IsSharedDynamic(Policy, Mode)) return {SharedDynamic, SharedDynamic | StaticWorld};
		return {LocalDynamic, LocalDynamic | StaticWorld};
	}

	inline bool CanCollide(const FFilter& A, const FFilter& B)
	{
		return (A.CategoryBits & B.MaskBits) != 0 && (B.CategoryBits & A.MaskBits) != 0;
	}
}

/** Visual actor driven by Box3DAsync; its mesh never participates in Chaos collision. */
UCLASS(BlueprintType, Blueprintable)
class BOX3DREPLICATION_API ABox3DAsyncPresentationActor : public AActor
{
	GENERATED_BODY()

public:
	ABox3DAsyncPresentationActor();

public:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Box3D Async")
	TObjectPtr<UStaticMeshComponent> PresentationMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Box3D Async",
		meta = (ToolTip = "Uses UStaticMesh authored simple collision in Box3DAsync for static or dynamic bodies. The visual component remains NoCollision in Chaos."))
	EBox3DAsyncPresentationMode CollisionMode = EBox3DAsyncPresentationMode::AuthoredStaticCollision;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Box3D Async",
		meta = (ToolTip = "Initial Box3DAsync velocity in centimeters per second; used only in dynamic mode."))
	FVector InitialVelocity = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Box3D Async|Replication",
		meta = (ToolTip = "Shared bodies are simulated by authority and presented from Iris snapshots; local-only bodies never enter a network root."))
	EBox3DReplicationPolicy ReplicationPolicy = EBox3DReplicationPolicy::LocalOnly;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Box3D Async|Replication",
		meta = (ToolTip = "Stable per-map identity for a placed shared body. Duplicated keys are rejected by the replication registry."))
	FGuid ReplicationKey;

	UFUNCTION(BlueprintCallable, Category = "Box3D Async")
	void RetryBox3DRegistration();

public:
	void SetActorLocRot_Optimized(const FVector& InLoc, const FRotator& InRotator);
	void SetNetworkReplica(bool bInNetworkReplica);
	bool IsNetworkReplica() const { return bNetworkReplica; }
protected:
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	enum class ERegistrationState : uint8 { Waiting, Queued, Active, Failed };
	friend class ABox3DAsyncDirector;
	TWeakObjectPtr<ABox3DAsyncDirector> RegisteredDirector;
	uint64 Box3DBodyId = 0;
	bool bNetworkReplica = false;
	ERegistrationState RegistrationState = ERegistrationState::Waiting;
	bool bWaitingForCapacity = false;
	int32 StaticCollisionAdmissionRetries = 0;
	double NextRegistrationAttemptTimeSeconds = 0.0;
	void SetCollisionDisabled();
};
