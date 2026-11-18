#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/Box3DBPIdentity.h"
#include "Box3DBPHit.generated.h"

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
