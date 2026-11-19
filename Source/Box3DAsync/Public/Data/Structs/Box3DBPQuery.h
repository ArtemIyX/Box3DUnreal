#pragma once

#include "CoreMinimal.h"
#include "Data/Enums/Box3DBPQueryKind.h"
#include "Data/Enums/Box3DBPQueryMode.h"
#include "Data/Enums/Box3DBPQueryShape.h"
#include "Data/Structs/Box3DBPIdentity.h"
#include "Box3DBPQuery.generated.h"

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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D|Async")
	bool bKinematic = true;
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
