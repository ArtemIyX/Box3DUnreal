#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Kismet/BlueprintAsyncActionBase.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "World/Box3DAsyncSubsystem.h"
#include "Data/Structs/Box3DBPQuery.h"
#include "Data/Structs/Box3DBPQueryResult.h"
#include "Data/Structs/Box3DBPEventFilter.h"
#include "Data/Structs/Box3DBPEventDelivery.h"
#include "Data/Structs/Box3DBPBodyCommand.h"
#include "Data/Structs/Box3DBPBodyDesc.h"
#include "Data/Structs/Box3DBPBodyCreationResult.h"
#include "Data/Structs/Box3DBPBodyState.h"
#include "Data/Structs/Box3DBPFieldDesc.h"
#include "Data/Structs/Box3DBPJointDesc.h"
#include "Data/Structs/Box3DBPJointState.h"
#include "Data/Structs/Box3DBPJointBreakEvent.h"
#include "Box3DAsyncBlueprint.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FBox3DBPBodyCreated, const FBox3DBPBodyCreationResult&, Result);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FBox3DBPQueryCompleted, const FBox3DBPQueryResult&, Result);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FBox3DBPEventDelivered, const FBox3DBPEventDelivery&, Delivery);

/** Body creation reports the worker acknowledgment. Owner destruction cancels and retires accepted work. */
UCLASS()
class BOX3DASYNC_API UBox3DBPCreateBody : public UBlueprintAsyncActionBase
{
	GENERATED_BODY()
public:
	UPROPERTY(BlueprintAssignable)
	FBox3DBPBodyCreated Completed;
	UFUNCTION(BlueprintCallable, meta=(BlueprintInternalUseOnly="true", WorldContext="WorldContextObject"), Category="Box3D|Async")
	static UBox3DBPCreateBody* CreateBox3DAsyncBody(UObject* WorldContextObject, UObject* Owner, const FBox3DBPBodyDesc& Desc);
	virtual void Activate() override;
	UFUNCTION(BlueprintCallable, Category="Box3D|Async")
	void Cancel();
private:
	bool Poll(float DeltaTime);
	void Finish(bool bSucceeded);
	TWeakObjectPtr<UObject> WeakOwner;
	TWeakObjectPtr<UBox3DAsyncSubsystem> Physics;
	TWeakObjectPtr<UWorld> WeakWorld;
	FBox3DBPBodyDesc Input;
	FBox3DAsyncCreationConsumer Consumer;
	FBox3DAsyncBodyHandle Body;
	FBox3DAsyncCreationResult CreationResult;
	FTSTicker::FDelegateHandle Ticker;
	bool bCancel = false;
	bool bSucceeded = false;
	bool bHaveResult = false;
};

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
	/** Returns whether a command was admitted; it does not guarantee native execution. */
	UFUNCTION(BlueprintCallable, meta=(WorldContext="WorldContextObject"), Category="Box3D|Async")
	static bool SubmitBodyCommand(UObject* WorldContextObject, const FBox3DBPIdentity& Body, const FBox3DBPBodyCommand& Command);

	/** Returns the latest completed worker state when available. */
	UFUNCTION(BlueprintCallable, meta=(WorldContext="WorldContextObject"), Category="Box3D|Async")
	static bool GetLatestBodyState(UObject* WorldContextObject, const FBox3DBPIdentity& Body, FBox3DBPBodyState& OutState);

	/** Create a persistent worker-side field; remove it when its owner ends. */
	UFUNCTION(BlueprintCallable, meta=(WorldContext="WorldContextObject"), Category="Box3D|Async|Fields")
	static bool CreateField(UObject* WorldContextObject, const FBox3DBPFieldDesc& Desc, FBox3DBPIdentity& OutField);

	UFUNCTION(BlueprintCallable, meta=(WorldContext="WorldContextObject"), Category="Box3D|Async|Fields")
	static bool UpdateField(UObject* WorldContextObject, const FBox3DBPIdentity& Field, const FBox3DBPFieldDesc& Desc);

	UFUNCTION(BlueprintCallable, meta=(WorldContext="WorldContextObject"), Category="Box3D|Async|Fields")
	static bool RemoveField(UObject* WorldContextObject, const FBox3DBPIdentity& Field);

	/** Joint creation is queued; poll GetJointStatus before relying on it. */
	UFUNCTION(BlueprintCallable, meta=(WorldContext="WorldContextObject"), Category="Box3D|Async|Joints")
	static bool CreateJoint(UObject* WorldContextObject, const FBox3DBPJointDesc& Desc, FBox3DBPIdentity& OutJoint);

	UFUNCTION(BlueprintCallable, meta=(WorldContext="WorldContextObject"), Category="Box3D|Async|Joints")
	static EBox3DAsyncJointStatus GetJointStatus(UObject* WorldContextObject, const FBox3DBPIdentity& Joint);

	UFUNCTION(BlueprintCallable, meta=(WorldContext="WorldContextObject"), Category="Box3D|Async|Joints")
	static bool SubmitJointCommand(UObject* WorldContextObject, const FBox3DBPIdentity& Joint, const FBox3DAsyncJointCommand& Command);

	UFUNCTION(BlueprintCallable, meta=(WorldContext="WorldContextObject"), Category="Box3D|Async|Joints")
	static bool GetLatestJointState(UObject* WorldContextObject, const FBox3DBPIdentity& Joint, FBox3DBPJointState& OutState);

	/** Update joint properties; type, body endpoints and local frames do not change. */
	UFUNCTION(BlueprintCallable, meta=(WorldContext="WorldContextObject"), Category="Box3D|Async|Joints")
	static bool UpdateJoint(UObject* WorldContextObject, const FBox3DBPIdentity& Joint, const FBox3DBPJointDesc& Settings);

	/** Drain the single raw break stream; bGap remains true after an overflow. */
	UFUNCTION(BlueprintCallable, meta=(WorldContext="WorldContextObject"), Category="Box3D|Async|Joints")
	static void DrainJointBreakEvents(UObject* WorldContextObject, TArray<FBox3DBPJointBreakEvent>& OutEvents, bool& bGap);

	UFUNCTION(BlueprintCallable, meta=(WorldContext="WorldContextObject"), Category="Box3D|Async|Joints")
	static bool RemoveJoint(UObject* WorldContextObject, const FBox3DBPIdentity& Joint);

	/** Queue removal of a body created by this world; retry if the queue is full. */
	UFUNCTION(BlueprintCallable, meta=(WorldContext="WorldContextObject"), Category="Box3D|Async")
	static bool RemoveBody(UObject* WorldContextObject, const FBox3DBPIdentity& Body);

	/** Returns null when the Async backend is unavailable or the subscription is rejected. */
	UFUNCTION(BlueprintCallable, meta=(WorldContext="WorldContextObject"), Category="Box3D|Async")
	static UBox3DBPEventSubscription* SubscribeBox3DAsyncEvents(UObject* WorldContextObject, UObject* Owner,
	                                                            const FBox3DBPEventFilter& Filter);
};
