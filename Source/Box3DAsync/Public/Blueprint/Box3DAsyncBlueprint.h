#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintAsyncActionBase.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "World/Box3DAsyncSubsystem.h"
#include "Box3DAsyncBlueprint.generated.h"

/** IDs preserve all 64 bits in Blueprint's signed integer representation. */
USTRUCT(BlueprintType)
struct BOX3DASYNC_API FBox3DBPIdentity
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadWrite, Category="Box3D")
	FGuid World;
	UPROPERTY(BlueprintReadWrite, Category="Box3D")
	int64 Id = 0;
};

UENUM(BlueprintType)
enum class EBox3DBPQueryKind : uint8 { Line, Sweep, Overlap, AabbCandidates };

UENUM(BlueprintType)
enum class EBox3DBPQueryShape : uint8 { Sphere, Capsule, Box, Convex };

UENUM(BlueprintType)
enum class EBox3DBPQueryMode : uint8 { Any, Closest, Multi };

UENUM(BlueprintType)
enum class EBox3DBPQueryStatus : uint8 { Hit, Miss, Truncated, Cancelled, Failed, WorldEnded };

UENUM(BlueprintType)
enum class EBox3DBPQueryAdmission : uint8
{
	Accepted, InvalidInput, Unsupported, InvalidConsumer, WrongWorld, Capacity, Unavailable
};

UENUM(BlueprintType)
enum class EBox3DBPQueryFailure : uint8 { None, IdentityUnavailable, WorkLimit, WorkerUnavailable };

UENUM(BlueprintType)
enum class EBox3DBPEventStatus : uint8 { Pending, Baseline, Events, NeedsResync, Unavailable, Closed };

UENUM(BlueprintType)
enum class EBox3DBPEventKind : uint8 { ContactBegin, ContactEnd, Impact, SensorEnter, SensorExit };

UENUM(BlueprintType)
enum class EBox3DBPEndCause : uint8 { NativeEnd, Removed };

USTRUCT(BlueprintType)
struct BOX3DASYNC_API FBox3DBPQuery
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	EBox3DBPQueryKind Kind = EBox3DBPQueryKind::Line;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	EBox3DBPQueryShape Shape = EBox3DBPQueryShape::Sphere;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	EBox3DBPQueryMode Mode = EBox3DBPQueryMode::Closest;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	FVector Start = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	FVector End = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	FRotator Rotation = FRotator::ZeroRotator;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	float Radius = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	float HalfHeight = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	FVector HalfExtents = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	TArray<FVector> ConvexPoints;
	/** Signed integers carry the complete 64-bit mask; -1 selects all bits. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	int64 CategoryBits = -1;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	int64 MaskBits = -1;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	bool bStatic = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	bool bDynamic = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	bool bSensors = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	bool bSolids = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	int32 MaxHits = 1;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	int64 UserTag = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	TArray<FBox3DBPIdentity> IgnoredBodies;
};

USTRUCT(BlueprintType)
struct BOX3DASYNC_API FBox3DBPSample
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category="Box3D")
	int64 Step = 0;
	UPROPERTY(BlueprintReadOnly, Category="Box3D")
	double ScheduledMonotonicSeconds = 0;
	UPROPERTY(BlueprintReadOnly, Category="Box3D")
	int64 ClockEpoch = 0;
};

USTRUCT(BlueprintType)
struct BOX3DASYNC_API FBox3DBPHit
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category="Box3D")
	FBox3DBPIdentity Body;
	UPROPERTY(BlueprintReadOnly, Category="Box3D")
	FBox3DBPIdentity Shape;
	UPROPERTY(BlueprintReadOnly, Category="Box3D")
	int32 ShapeType = -1;
	UPROPERTY(BlueprintReadOnly, Category="Box3D")
	float Fraction = 1;
	UPROPERTY(BlueprintReadOnly, Category="Box3D")
	float Distance = 0;
	UPROPERTY(BlueprintReadOnly, Category="Box3D")
	FVector Point = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly, Category="Box3D")
	FVector Normal = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly, Category="Box3D")
	int64 MaterialId = 0;
	UPROPERTY(BlueprintReadOnly, Category="Box3D")
	int32 TriangleIndex = -1;
	UPROPERTY(BlueprintReadOnly, Category="Box3D")
	int32 ChildIndex = -1;
};

USTRUCT(BlueprintType)
struct BOX3DASYNC_API FBox3DBPQueryResult
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category="Box3D")
	EBox3DBPQueryStatus Status = EBox3DBPQueryStatus::Failed;
	/** Rejected work has no physics sample and reports its admission reason here. */
	UPROPERTY(BlueprintReadOnly, Category="Box3D")
	EBox3DBPQueryAdmission Admission = EBox3DBPQueryAdmission::Accepted;
	UPROPERTY(BlueprintReadOnly, Category="Box3D")
	EBox3DBPQueryFailure Failure = EBox3DBPQueryFailure::None;
	UPROPERTY(BlueprintReadOnly, Category="Box3D")
	EBox3DBPQueryKind Kind = EBox3DBPQueryKind::Line;
	UPROPERTY(BlueprintReadOnly, Category="Box3D")
	int64 UserTag = 0;
	UPROPERTY(BlueprintReadOnly, Category="Box3D")
	FBox3DBPSample Sample;
	UPROPERTY(BlueprintReadOnly, Category="Box3D")
	TArray<FBox3DBPHit> Hits;
};

USTRUCT(BlueprintType)
struct BOX3DASYNC_API FBox3DBPEventFilter
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	FBox3DBPIdentity Body;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	FBox3DBPIdentity Shape;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	bool bContacts = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	bool bSensors = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	bool bImpacts = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	bool bBodyPairs = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	float MinimumImpactSpeed = 0;
};

USTRUCT(BlueprintType)
struct BOX3DASYNC_API FBox3DBPEvent
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category="Box3D")
	EBox3DBPEventKind Kind = EBox3DBPEventKind::ContactBegin;
	UPROPERTY(BlueprintReadOnly, Category="Box3D")
	EBox3DBPEndCause EndCause = EBox3DBPEndCause::NativeEnd;
	UPROPERTY(BlueprintReadOnly, Category="Box3D")
	FBox3DBPIdentity BodyA;
	UPROPERTY(BlueprintReadOnly, Category="Box3D")
	FBox3DBPIdentity BodyB;
	UPROPERTY(BlueprintReadOnly, Category="Box3D")
	FBox3DBPIdentity ShapeA;
	UPROPERTY(BlueprintReadOnly, Category="Box3D")
	FBox3DBPIdentity ShapeB;
	UPROPERTY(BlueprintReadOnly, Category="Box3D")
	FVector Point = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly, Category="Box3D")
	FVector Normal = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly, Category="Box3D")
	float ApproachSpeed = 0;
	UPROPERTY(BlueprintReadOnly, Category="Box3D")
	int64 MaterialA = 0;
	UPROPERTY(BlueprintReadOnly, Category="Box3D")
	int64 MaterialB = 0;
};

USTRUCT(BlueprintType)
struct BOX3DASYNC_API FBox3DBPEventDelivery
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category="Box3D")
	EBox3DBPEventStatus Status = EBox3DBPEventStatus::Pending;
	UPROPERTY(BlueprintReadOnly, Category="Box3D")
	int64 FirstMissedSequence = 0;
	UPROPERTY(BlueprintReadOnly, Category="Box3D")
	int64 LastMissedSequence = 0;
	UPROPERTY(BlueprintReadOnly, Category="Box3D")
	int64 Sequence = 0;
	UPROPERTY(BlueprintReadOnly, Category="Box3D")
	FBox3DBPSample Sample;
	UPROPERTY(BlueprintReadOnly, Category="Box3D")
	TArray<FBox3DBPEvent> Events;
};

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
