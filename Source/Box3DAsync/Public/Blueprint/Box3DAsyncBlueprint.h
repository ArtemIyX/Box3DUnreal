#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintAsyncActionBase.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "World/Box3DAsyncSubsystem.h"
#include "Data/Structs/Box3DBPQuery.h"
#include "Data/Structs/Box3DBPQueryResult.h"
#include "Data/Structs/Box3DBPEventFilter.h"
#include "Data/Structs/Box3DBPEventDelivery.h"
#include "Box3DAsyncBlueprint.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FBox3DBPQueryCompleted, const FBox3DBPQueryResult&, Result);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FBox3DBPEventDelivered, const FBox3DBPEventDelivery&, Delivery);

/** One-shot query against the selected world's Async Box3D backend. */
UCLASS()
class BOX3DASYNC_API UBox3DBPAsyncQuery : public UBlueprintAsyncActionBase
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintAssignable)
	FBox3DBPQueryCompleted Completed;
	/** Rejection produces Failed; accepted work produces exactly one terminal status while Owner is valid. */
	UFUNCTION(BlueprintCallable, meta=(BlueprintInternalUseOnly="true", WorldContext="WorldContextObject"),
		Category="Box3D|Async")
	static UBox3DBPAsyncQuery* QueryBox3DAsync(UObject* WorldContextObject, UObject* Owner, const FBox3DBPQuery& Query);
	virtual void Activate() override;
	UFUNCTION(BlueprintCallable, Category="Box3D|Async")
	void Cancel();
	virtual void SetReadyToDestroy() override;

private:
	TWeakObjectPtr<UObject> WeakOwner;
	TWeakObjectPtr<UBox3DAsyncSubsystem> Physics;
	FBox3DBPQuery Input;
	FBox3DAsyncQueryConsumer Consumer;
	FBox3DAsyncQueryHandle Request;
	bool bFinished = false;
	void Complete(const FBox3DAsyncQueryResult& Result,
	              EBox3DAsyncQueryAdmission Admission = EBox3DAsyncQueryAdmission::Accepted);
};

/** Retain this object in a Blueprint variable while subscribed. Stop or release it to unsubscribe. */
UCLASS(BlueprintType)
class BOX3DASYNC_API UBox3DBPEventSubscription : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintAssignable)
	FBox3DBPEventDelivered Delivered;
	UFUNCTION(BlueprintCallable, Category="Box3D|Async")
	void Stop();
	virtual void BeginDestroy() override;
	bool Start(UBox3DAsyncSubsystem* InPhysics, UObject* Owner, const FBox3DBPEventFilter& Filter);

private:
	TWeakObjectPtr<UBox3DAsyncSubsystem> Physics;
	FBox3DAsyncEventSubscription Subscription;
};

UCLASS()
class BOX3DASYNC_API UBox3DBPAsyncLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Returns null when the Async backend is unavailable or the subscription is rejected. */
	UFUNCTION(BlueprintCallable, meta=(WorldContext="WorldContextObject"), Category="Box3D|Async")
	static UBox3DBPEventSubscription* SubscribeBox3DAsyncEvents(UObject* WorldContextObject, UObject* Owner,
	                                                            const FBox3DBPEventFilter& Filter);
};
