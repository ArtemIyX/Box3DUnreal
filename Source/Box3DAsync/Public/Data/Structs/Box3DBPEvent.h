#pragma once

#include "CoreMinimal.h"
#include "Data/Enums/Box3DBPEndCause.h"
#include "Data/Enums/Box3DBPEventKind.h"
#include "Data/Structs/Box3DBPIdentity.h"
#include "Box3DBPEvent.generated.h"

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
