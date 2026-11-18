#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "World/Box3DAsyncSubsystem.h"
#include "Box3DAsyncDirector.generated.h"

class ABox3DAsyncPresentationActor;
class UWorld;

/** 
 * Place one in a world to drive Box3DAsync presentation actors on the game thread.
 * This director is the sole reader of that world's subsystem creation-result queue.
 * Other clients retrieve results through DrainUnclaimedCreationResults.
 * If the director ends, a game-thread cleanup ticker retries rejected removals
 * without consuming results; a replacement director adopts those IDs and results.
 */
UCLASS(Blueprintable)
class BOX3DREPLICATION_API ABox3DAsyncDirector : public AActor
{
	GENERATED_BODY()

public:
	ABox3DAsyncDirector();

	/** Track an actor. Its collision body is queued only after its native and Blueprint BeginPlay have run. */
	UFUNCTION(BlueprintCallable, Category = "Box3D Async")
	bool RegisterPresentationActor(ABox3DAsyncPresentationActor* Actor);

	/** Remove a tracked actor body; a game-thread cleanup ticker retries if the subsystem queue is full. */
	UFUNCTION(BlueprintCallable, Category = "Box3D Async")
	void UnregisterPresentationActor(ABox3DAsyncPresentationActor* Actor);

	/** Drain results not owned by this director, including results preserved across director replacement. */
	void DrainUnclaimedCreationResults(TArray<FBox3DAsyncCreationResult>& OutResults);

	/** Demonstrate an independent async line-query owner and physics-event subscription in the log. */
	UPROPERTY(EditAnywhere, Category = "Box3D Async|Example")
	bool bEnableQueryEventExample = false;

	/** Create an invisible sensor and falling visitor to demonstrate opt-in events. */
	UPROPERTY(EditAnywhere, Category = "Box3D Async|Example", meta = (EditCondition = "bEnableQueryEventExample"))
	bool bCreateExampleEventFixture = false;

	/** Distance in centimeters along this director's forward axis. */
	UPROPERTY(EditAnywhere, Category = "Box3D Async|Example", meta = (ClampMin = "1.0", EditCondition = "bEnableQueryEventExample"))
	float ExampleTraceDistance = 1000.0f;

	UPROPERTY(EditAnywhere, Category = "Box3D Async|Example", meta = (ClampMin = "0.1", EditCondition = "bEnableQueryEventExample"))
	float ExampleTraceInterval = 1.0f;

	static ABox3DAsyncDirector* FindForWorld(const UWorld* World);

public:
	virtual void Tick(float DeltaSeconds) override;
	
protected:
	virtual void BeginPlay() override;
	
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void ActivateDirector();
	void OnActorSpawned(AActor* SpawnedActor);
	bool TryQueueActor(ABox3DAsyncPresentationActor* Actor);
	bool IsCapacitySaturated() const;
	void DrainCreationResults();
	void ApplySampledPoses();
	void RetryPendingRemovals();
	void RemoveAllRegisteredActors();
	void TickQueryEventExample();
	void StopQueryEventExample();
	void RemoveExampleEventFixture();

	TWeakObjectPtr<UBox3DAsyncSubsystem> AsyncSubsystem;
	TSet<TWeakObjectPtr<ABox3DAsyncPresentationActor>> TrackedActors;
	TMap<uint64, TWeakObjectPtr<ABox3DAsyncPresentationActor>> BodyActors;
	TSet<uint64> OwnedBodyCreationIds;
	TArray<FBox3DAsyncCreationResult> UnclaimedCreationResults;
	TSet<uint64> PendingRemovalIds;
	FDelegateHandle ActorSpawnedHandle;
	FBox3DAsyncQueryConsumer ExampleQueryConsumer;
	FBox3DAsyncQueryHandle ExampleQueryRequest;
	FBox3DAsyncEventSubscription ExampleEventSubscription;
	TSet<FBox3DAsyncBodyId> ExampleFixtureBodies;
	double NextExampleTraceTime = 0.0;
	bool bExampleFixtureAttempted = false;
	bool bIsActiveDirector = false;
};
